// SPDX-License-Identifier: MIT
#include "gecko/model.hpp"
#include <algorithm>
#include <string_view>
namespace gecko::simulation {
// advance() bounds time to leave room for every scheduled interval.
static std::uint64_t later(std::uint64_t at,std::uint64_t delta) noexcept { return at+delta; }
SessionConfig configuration(Scenario scenario) {
  SessionConfig c;
  if(scenario==Scenario::Unavailable) return c;
  c.mode=TransportMode::SyntheticReadOnly; c.decode_mode=DecodeMode::SyntheticCandidate;
  c.profile=CandidateProfile::InYtV51; c.expected_versions={82,81};
  c.stale_after_us=2000000;
  if(scenario==Scenario::Recovery) c.go_interval_us=1000000;
  return c;
}
Packet incoming(std::size_t size, std::uint8_t flag=0) {
  Packet p; p.size=size; p.data[0]=p.data[5]=0x17;
  p.data[1]=9; p.data[6]=0x0a; p.data[7]=flag; return p;
}
Packet file(bool config) {
  auto p=incoming(33);
  const std::string_view name=config?"inYT_C82.xml":"inYT_S81.xml";
  std::copy(name.begin(),name.end(),p.data.begin()+16); return p;
}
Packet clock_packet() {
  auto p=incoming(22); p.data[12]=8; p.data[13]=0x4b;
  p.data[14]=26; p.data[15]=11; p.data[16]=15; p.data[18]=12;
  p.data[21]=xor_bytes(p.bytes().slice(0,21).value).value; return p;
}
Packet lo() { auto p=incoming(15); p.data[12]=1; p.data[13]=0x4c; p.data[14]=0x4f; return p; }

const char* state_name(SessionState state) noexcept {
  switch(state) {
    case SessionState::Disabled:return "disabled";
    case SessionState::Disconnected:return "disconnected";
    case SessionState::GoPending:return "go_pending";
    case SessionState::AwaitingFiles:return "awaiting_files";
    case SessionState::AwaitingClock:return "awaiting_clock";
    case SessionState::AwaitingLo:return "awaiting_lo";
    case SessionState::Ready:return "ready";
    case SessionState::Backoff:return "backoff";
    case SessionState::Fault:return "fault";
  }
  return "fault";
}
const char* error_name(Error error) noexcept {
  switch(error) {
    case Error::None:return nullptr;
    case Error::AddressNack:return "address_nack";
    case Error::DataNack:return "data_nack";
    case Error::ArbitrationLost:return "arbitration_lost";
    case Error::ShortRead:return "short_read";
    case Error::RestoreFailed:return "restore_failed";
    case Error::StaleState:return "stale_state";
    case Error::Timeout:return "timeout";
    default:return "model_error";
  }
}
const char* scenario_name(Scenario value) noexcept {
  switch(value) {
    case Scenario::Healthy:return "healthy";
    case Scenario::Unavailable:return "unavailable";
    case Scenario::Stale:return "stale";
    case Scenario::Recovery:return "recovery";
    case Scenario::InvalidTemperature:return "invalid_temperature";
  }
  return "unavailable";
}
  Model::Model(Scenario scenario) noexcept : scenario_(scenario), config_(configuration(scenario)), session_(transport_,config_) {
    if(scenario_!=Scenario::Unavailable) handshake(0,1);
    require(session_.start(0));
    next_go_=config_.go_interval_us;
  }
  bool Model::advance(std::uint64_t target_ms) noexcept {
    if(target_ms<now_ms_ || target_ms-now_ms_>60000 || target_ms>UINT64_MAX/1000-60000 || error_!=Error::None) return false;
    while(now_ms_<target_ms) {
      ++now_ms_;
      const auto now=now_ms_*1000;
      (void)session_.tick(now);
      require(transport_.scenario_error());
      if(error_!=Error::None) return false;
      if(planned_ && transport_.scenario_complete()) {
        require(transport_.retire_completed_script()); planned_=false;
      }
      if(planned_ || scenario_==Scenario::Unavailable) continue;
      if(session_.state()==SessionState::Backoff) {
        const auto due=later(now,session_.backoff_duration_us());
        const auto base=later(due,999)/1000*1000;
        if(scenario_==Scenario::Recovery) {
          handshake(base,session_.session_id()+1); next_go_=later(base,config_.go_interval_us);
        } else failed_go(base);
      } else if(session_.state()==SessionState::Ready && scenario_!=Scenario::Stale) {
        const auto sample=later(now,500000);
        if(later(sample,2000)>=next_go_) {
          if(scenario_==Scenario::Recovery && !injected_) { failed_go(next_go_); injected_=true; }
          else handshake(next_go_,session_.session_id()+1);
          next_go_=later(next_go_,config_.go_interval_us);
        } else status(sample,session_.session_id());
      }
    }
    return error_==Error::None;
  }

  void Model::write(Packet p,std::uint64_t at,Error outcome) noexcept {
    TxPlan plan; plan.expected=p; plan.earliest_us=plan.latest_us=at;
    plan.duration_us={1000,1000,1000,1000}; plan.outcome=outcome;
    require(transport_.add_tx(plan)); planned_=true;
  }
  void Model::read(Packet p,std::uint64_t at,std::uint64_t epoch,std::optional<std::uint32_t> index) noexcept {
    require(transport_.add_rx({p,{at,epoch,transport_.loss_generation(),index},0x17,true})); planned_=true;
  }
  void Model::status(std::uint64_t at,std::uint64_t epoch) noexcept {
    std::array<std::uint8_t,162> data{};
    data[21]=2; data[22]=0x9a;
    if(scenario_!=Scenario::InvalidTemperature) {data[23]=2; data[24]=0x91;}
    data[53]=1;
    std::size_t offset=0;
    for(std::uint32_t i=0;i<3;++i) {
      const std::size_t count=i==2?38:62;
      auto p=incoming(16+count,1); p.data[9]=i==2?0:1; p.data[14]=0x52; p.data[15]=0x51;
      std::copy_n(data.begin()+static_cast<std::ptrdiff_t>(offset),count,p.data.begin()+16);
      read(p,later(at,i*1000U),epoch,i); offset+=count;
    }
  }
  void Model::handshake(std::uint64_t base,std::uint64_t epoch) noexcept {
    write(encode_go().value,base);
    for(unsigned i=1;i<=3;++i) write(encode_ack().value,later(base,i*5000U));
    read(file(true),later(base,5000),epoch); read(file(false),later(base,10000),epoch);
    read(clock_packet(),later(base,15000),epoch); read(lo(),later(base,20000),epoch);
    status(later(base,25000),epoch);
  }
  void Model::failed_go(std::uint64_t at) noexcept { write(encode_go().value,at,Error::AddressNack); }

} // namespace gecko::simulation
