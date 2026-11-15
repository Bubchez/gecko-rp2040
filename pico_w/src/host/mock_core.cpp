// SPDX-License-Identifier: MIT
#include "gecko/session.hpp"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace gecko;
enum class Scenario { Healthy, Unavailable, Stale, Recovery, InvalidTemperature };

std::uint64_t later(std::uint64_t at, std::uint64_t delta) {
  if(delta>UINT64_MAX-at) throw std::runtime_error("clock_range");
  return at+delta;
}
void require(Error e) { if(e!=Error::None) throw std::runtime_error("model_error"); }
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

const char* state_name(SessionState state) {
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
const char* error_name(Error error) {
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
template<typename T> void optional(const std::optional<T>& value) {
  if(value) std::cout<<*value; else std::cout<<"null";
}

class Model {
 public:
  explicit Model(Scenario scenario): scenario_(scenario), config_(configuration(scenario)), session_(transport_,config_) {
    if(scenario_!=Scenario::Unavailable) handshake(0,1);
    require(session_.start(0));
    next_go_=config_.go_interval_us;
  }
  bool advance(std::uint64_t target_ms) {
    if(target_ms<now_ms_ || target_ms-now_ms_>60000 || target_ms>UINT64_MAX/1000) return false;
    while(now_ms_<target_ms) {
      ++now_ms_;
      const auto now=now_ms_*1000;
      (void)session_.tick(now);
      if(transport_.scenario_error()!=Error::None) throw std::runtime_error("scenario_error");
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
    return true;
  }
  void snapshot() const {
    const auto s=session_.snapshot(now_ms_*1000);
    const auto v=session_.versions();
    const auto& stats=session_.stats(); const auto& t=transport_.stats();
    std::cout<<"{\"schema_version\":1,\"now_ms\":"<<now_ms_<<",\"session_state\":\""<<state_name(session_.state())
      <<"\",\"session\":"<<session_.session_id()<<",\"config_version\":"<<static_cast<unsigned>(v.config)
      <<",\"status_version\":"<<static_cast<unsigned>(v.status)<<",\"stale_after_ms\":"<<config_.stale_after_us/1000
      <<",\"revision\":"<<s.revision<<",\"data_session\":"<<s.data_session
      <<",\"ready\":"<<s.ready<<",\"stale\":"<<s.stale<<",\"hardware_verified\":false,\"status_age_us\":";
    optional(s.status_age_us); std::cout<<",\"completed_age_us\":"; optional(s.completed_age_us);
    std::cout<<",\"frame_age_us\":"; optional(s.candidate_frame_age_us);
    std::cout<<",\"observed\":";
    if(!s.observed) std::cout<<"null";
    else {
      const auto& o=*s.observed;
      std::cout<<"{\"temperature_c\":"; optional(o.actual.celsius);
      std::cout<<",\"target_temperature_c\":"; optional(o.target.celsius);
      std::cout<<",\"heater_on\":"<<o.heater_on<<",\"circulation_on\":"<<o.circulation_on
        <<",\"light_demand_on\":"<<o.light_demand_on<<",\"standby\":"; optional(o.standby);
      std::cout<<",\"pumps\":[";
      for(unsigned i=0;i<4;++i) {
        if(i!=0) std::cout<<',';
        std::cout<<"{\"output\":"<<static_cast<unsigned>(o.pumps[i])<<",\"demand\":"
          <<((o.user_demand>>(i*2))&3U)<<'}';
      }
      std::cout<<"]}";
    }
    std::cout<<",\"stats\":{\"attempts\":"<<stats.attempts<<",\"failures\":"<<stats.failures
      <<",\"invalid_rx\":"<<stats.invalid_rx<<",\"ignored_rx\":"<<stats.ignored_rx
      <<",\"stale_events\":"<<stats.stale_events<<",\"assembly_aborts\":"<<stats.assembly_aborts
      <<",\"statuses\":"<<stats.statuses<<",\"tx_submitted\":"<<t.submitted<<",\"tx_completed\":"<<t.completed
      <<",\"rx_dropped\":"<<t.dropped_rx<<",\"queue_overflows\":"<<t.queue_overflows
      <<",\"checkpoints\":"<<t.checkpoints<<",\"retired_trace_events\":"<<t.retired_trace_events<<"},\"last_error\":";
    const auto error=error_name(session_.last_error());
    if(error) std::cout<<'"'<<error<<'"'; else std::cout<<"null";
    std::cout<<'}'<<std::endl;
  }
 private:
  void write(Packet p,std::uint64_t at,Error outcome=Error::None) {
    TxPlan plan; plan.expected=p; plan.earliest_us=plan.latest_us=at;
    plan.duration_us={1000,1000,1000,1000}; plan.outcome=outcome;
    require(transport_.add_tx(plan)); planned_=true;
  }
  void read(Packet p,std::uint64_t at,std::uint64_t epoch,std::optional<std::uint32_t> index={}) {
    require(transport_.add_rx({p,{at,epoch,transport_.loss_generation(),index},0x17,true})); planned_=true;
  }
  void status(std::uint64_t at,std::uint64_t epoch) {
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
  void handshake(std::uint64_t base,std::uint64_t epoch) {
    write(encode_go().value,base);
    for(unsigned i=1;i<=3;++i) write(encode_ack().value,later(base,i*5000U));
    read(file(true),later(base,5000),epoch); read(file(false),later(base,10000),epoch);
    read(clock_packet(),later(base,15000),epoch); read(lo(),later(base,20000),epoch);
    status(later(base,25000),epoch);
  }
  void failed_go(std::uint64_t at) { write(encode_go().value,at,Error::AddressNack); }
  Scenario scenario_; SessionConfig config_; SimulatedTransport transport_;
  ReadOnlySession session_;
  std::uint64_t now_ms_{0},next_go_{0}; bool planned_{false},injected_{false};
};
} // namespace

int main(int argc,char** argv) {
  std::string scenario="healthy";
  if(argc==3 && std::string_view(argv[1])=="--scenario") scenario=argv[2];
  else if(argc!=1) return 2;
  Scenario selected;
  if(scenario=="healthy") selected=Scenario::Healthy;
  else if(scenario=="unavailable") selected=Scenario::Unavailable;
  else if(scenario=="stale") selected=Scenario::Stale;
  else if(scenario=="recovery") selected=Scenario::Recovery;
  else if(scenario=="invalid_temperature") selected=Scenario::InvalidTemperature;
  else return 2;
  std::cout.imbue(std::locale::classic()); std::cout<<std::boolalpha<<std::setprecision(17);
  try {
    Model model(selected);
    char buffer[96];
    while(std::cin.getline(buffer,sizeof(buffer)) || !std::cin.eof()) {
      if(std::cin.fail()) {
        std::cin.clear(); std::cin.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
        std::cout<<"{\"backend_error\":\"invalid_command\"}"<<std::endl; continue;
      }
      const auto extracted=static_cast<std::size_t>(std::cin.gcount());
      const auto length=extracted-(!std::cin.eof() && extracted!=0 ? 1U : 0U);
      std::string_view line(buffer,length);
      if(!line.empty() && line.back()=='\r') line.remove_suffix(1);
      if(line.find('\0')!=std::string_view::npos) {
        std::cout<<"{\"backend_error\":\"invalid_command\"}"<<std::endl; continue;
      }
      if(line=="SNAPSHOT") {model.snapshot();continue;}
      std::uint64_t target=0;
      if(line.substr(0,8)=="ADVANCE ") {
        const auto number=line.substr(8);
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),target);
        if(parsed.ec==std::errc{} && parsed.ptr==number.data()+number.size() && model.advance(target)) {
          model.snapshot();continue;
        }
      }
      std::cout<<"{\"backend_error\":\"invalid_command\"}"<<std::endl;
    }
  } catch(const std::exception&) {std::cout<<"{\"backend_error\":\"model_failure\"}"<<std::endl;return 1;}
}
