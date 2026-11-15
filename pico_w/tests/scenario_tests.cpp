// SPDX-License-Identifier: MIT
#include "gecko/session.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace gecko;
namespace {
std::size_t checks=0;
void check(bool value, const char* expression, int line) {
  ++checks;
  if (!value) throw std::runtime_error("line "+std::to_string(line)+": "+expression);
}
#define CHECK(expr) check(static_cast<bool>(expr),#expr,__LINE__)
constexpr auto candidate=DecodeMode::SyntheticCandidate;
static_assert(!std::is_copy_constructible<SimulatedTransport>::value);
static_assert(!std::is_move_constructible<SimulatedTransport>::value);
static_assert(!std::is_copy_constructible<ReadOnlySession>::value);
static_assert(!std::is_move_constructible<ReadOnlySession>::value);

const char* error_name(Error error) {
  switch(error) {
    case Error::AddressNack: return "address_nack";
    case Error::DataNack: return "data_nack";
    case Error::ArbitrationLost: return "arbitration_lost";
    case Error::ShortRead: return "short_read";
    case Error::Timeout: return "timeout";
    case Error::RestoreFailed: return "restore_failed";
    case Error::StuckSda: return "stuck_sda";
    case Error::StuckScl: return "stuck_scl";
    default: return "other";
  }
}

SessionConfig config() {
  SessionConfig c;
  c.mode=TransportMode::SyntheticReadOnly; c.decode_mode=candidate;
  c.profile=CandidateProfile::InYtV51; c.expected_versions={82,81};
  c.tx_timeout_us=100; c.step_timeout_us=c.status_timeout_us=1000;
  c.stale_after_us=10000; c.go_interval_us=100000;
  c.backoff_initial_us=100; c.backoff_max_us=400;
  return c;
}
Packet incoming(std::size_t size, std::uint8_t flag=0) {
  CHECK(size>=15 && size<=128);
  Packet p; p.size=size; p.data[0]=p.data[5]=0x17;
  p.data[1]=9; p.data[6]=0x0a; p.data[7]=flag;
  return p;
}
Packet file(bool is_config=true, unsigned version=0) {
  auto p=incoming(33);
  const auto name=std::string("inYT_")+(is_config?"C":"S")+
    std::to_string(version==0 ? (is_config?82U:81U) : version)+".xml";
  CHECK(name.size()<=13);
  std::copy(name.begin(),name.end(),p.data.begin()+16);
  return p;
}
Packet clock_packet() {
  auto p=incoming(22); p.data[12]=8; p.data[13]=0x4b;
  p.data[14]=26; p.data[15]=11; p.data[16]=15; p.data[17]=0;
  p.data[18]=12; p.data[19]=34; p.data[20]=56;
  unsigned x=0; for (std::size_t i=0;i<21;++i) x^=p.data[i];
  p.data[21]=static_cast<std::uint8_t>(x); return p;
}
Packet lo() {
  auto p=incoming(15); p.data[12]=1; p.data[13]=0x4c; p.data[14]=0x4f; return p;
}
std::array<std::uint8_t,162> payload() {
  std::array<std::uint8_t,162> p{};
  p[21]=2; p[22]=0x9a; p[23]=2; p[24]=0x91; p[53]=1; return p;
}
Packet fragment(std::size_t offset, std::size_t length, bool more=true) {
  const auto data=payload();
  CHECK(offset+length<=data.size() && length<=112);
  auto p=incoming(16+length,1); p.data[9]=more?1:0; p.data[14]=0x52; p.data[15]=0x51;
  std::copy(data.begin()+static_cast<std::ptrdiff_t>(offset),
            data.begin()+static_cast<std::ptrdiff_t>(offset+length),p.data.begin()+16);
  return p;
}
TxPlan tx(Packet bytes, std::uint64_t at) {
  TxPlan plan; plan.expected=bytes; plan.earliest_us=plan.latest_us=at; return plan;
}
TxRequest request(Packet bytes, std::uint64_t deadline=1000) {
  TxRequest out; out.write=bytes; out.id=1; out.session=1; out.deadline_us=deadline; return out;
}
RxPlan rx(Packet bytes, std::uint64_t at, std::uint64_t epoch=1,
          std::optional<std::uint32_t> index={}, std::uint64_t loss=0) {
  return {bytes,{at,epoch,loss,index},0x17,true};
}
struct Script {
  std::vector<TxPlan> writes;
  std::vector<RxPlan> reads;
  void install(SimulatedTransport& t) const {
    for (const auto& p:writes) CHECK(t.add_tx(p)==Error::None);
    for (const auto& p:reads) CHECK(t.add_rx(p)==Error::None);
  }
};
Script happy(std::uint64_t base=0, std::uint64_t epoch=1, std::uint64_t loss=0) {
  Script out;
  out.writes={tx(encode_go().value,base),tx(encode_ack().value,base+100),
    tx(encode_ack().value,base+200),tx(encode_ack().value,base+300)};
  out.reads={rx(file(),base+100,epoch,{},loss),rx(file(false),base+200,epoch,{},loss),
    rx(clock_packet(),base+300,epoch,{},loss),rx(lo(),base+400,epoch,{},loss),
    rx(fragment(0,62),base+500,epoch,0,loss),rx(fragment(62,62),base+600,epoch,1,loss),
    rx(fragment(124,38,false),base+700,epoch,2,loss)};
  return out;
}
void run(ReadOnlySession& s, std::uint64_t from, std::uint64_t through) {
  for (auto time=from;time<=through;time+=10) (void)s.tick(time);
}
TransportEvent result(SimulatedTransport& t) {
  auto event=t.poll(); CHECK(event && event->kind==EventKind::Tx); return *event;
}

void transport_success() {
  SimulatedTransport t;
  CHECK(!SimulatedTransport::physical_communication_enabled);
  auto plan=tx(encode_go().value,0); plan.read={0x12,0x34};
  CHECK(t.add_tx(plan)==Error::None);
  CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
  CHECK(t.submit(request(encode_go().value),0)==Error::None);
  CHECK(t.role()==Role::MasterWrite);
  CHECK(t.advance(10)==Error::None && t.role()==Role::MasterRead);
  CHECK(t.advance(20)==Error::None && t.role()==Role::Stop);
  CHECK(t.advance(30)==Error::None && t.role()==Role::Restore);
  CHECK(t.advance(40)==Error::None && t.role()==Role::Listen);
  const auto e=result(t);
  CHECK(e.at_us==40 && e.tx.error==Error::None && e.tx.written==15);
  CHECK(e.tx.read_count==2 && e.tx.read[0]==0x12 && e.tx.read[1]==0x34);
  CHECK(e.tx.restored && e.tx.write_count_known && e.tx.possibly_applied());
  CHECK(t.scenario_complete());
  CHECK(t.add_rx(rx(file(),50))==Error::None);
  CHECK(t.advance(50)==Error::None);
  auto r=t.poll(); CHECK(r && r->kind==EventKind::Rx && r->at_us==50);
  bool reply=false;
  for (std::size_t i=0;i<t.trace_size();++i) reply|=t.trace()[i].role==Role::Reply;
  CHECK(reply && t.role()==Role::Listen && t.scenario_complete());
}

void transport_faults() {
  for (auto error:{Error::AddressNack,Error::DataNack,Error::ArbitrationLost,
      Error::ShortRead,Error::RestoreFailed,Error::Timeout,Error::StuckSda,Error::StuckScl}) {
    SimulatedTransport t;
    auto p=tx(encode_go().value,0); p.outcome=error;
    if (error==Error::ShortRead) {p.fault_phase=Phase::Read; p.read_count=1;}
    else if (error==Error::RestoreFailed) p.fault_phase=Phase::Restore;
    else if (error!=Error::AddressNack) p.partial_write=3;
    CHECK(t.add_tx(p)==Error::None);
    CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
    CHECK(t.submit(request(encode_go().value),0)==Error::None);
    CHECK(t.advance(100)==Error::None);
    const auto r=result(t).tx;
    CHECK(r.error==error && r.restored==(error!=Error::RestoreFailed));
    CHECK(r.possibly_applied()==(error!=Error::AddressNack));
    CHECK(t.role()==(error==Error::RestoreFailed?Role::Released:Role::Listen));
    CHECK(t.scenario_complete());
  }
  for (auto phase:{Phase::Write,Phase::Read,Phase::Stop,Phase::Restore}) {
    SimulatedTransport t;
    auto p=tx(encode_go().value,0); p.outcome=Error::ArbitrationLost; p.fault_phase=phase; p.read_count=0;
    CHECK(t.add_tx(p)==Error::None);
    CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
    CHECK(t.submit(request(encode_go().value),0)==Error::None);
    CHECK(t.advance(100)==Error::None);
    const auto r=result(t).tx;
    CHECK(r.phase==phase && r.error==Error::ArbitrationLost);
    CHECK(r.possibly_applied()==(phase!=Phase::Write));
  }
}

void transport_deadlines() {
  for (unsigned phase=0;phase<4;++phase) {
    SimulatedTransport t;
    auto p=tx(encode_go().value,0); p.duration_us[phase]=1000;
    CHECK(t.add_tx(p)==Error::None);
    CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
    CHECK(t.submit(request(encode_go().value,100),0)==Error::None);
    CHECK(t.advance(1000)==Error::None);
    const auto r=result(t);
    CHECK(r.at_us==100 && r.tx.error==Error::Timeout && r.tx.restored);
    CHECK(r.tx.phase==static_cast<Phase>(phase));
    CHECK(r.tx.possibly_applied());
    CHECK(r.tx.write_count_known==(phase!=0));
  }
  SimulatedTransport t;
  CHECK(t.add_tx(tx(encode_go().value,0))==Error::None);
  CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
  CHECK(t.submit(request(encode_go().value,40),0)==Error::None);
  CHECK(t.advance(40)==Error::None);
  CHECK(result(t).tx.error==Error::Timeout); // Exact deadline precedes completion.
  CHECK(t.add_rx(rx(lo(),50))==Error::None);
  CHECK(t.advance(50)==Error::None); CHECK(t.poll());
  CHECK(t.advance(49)==Error::ClockRegression);
  const auto e=t.poll(); CHECK(e && e->error==Error::ClockRegression && t.role()==Role::Released);
}

void transport_capacity() {
  SimulatedTransport t;
  CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
  for (std::size_t i=0;i<64;++i) CHECK(t.add_rx(rx(file(),100))==Error::None);
  CHECK(t.add_rx(rx(file(),100))==Error::ScenarioCapacity);
  CHECK(t.advance(100)==Error::None);
  const auto loss=t.poll(); CHECK(loss && loss->kind==EventKind::Fault && loss->error==Error::TransportLoss);
  CHECK(t.stats().queue_overflows==1 && t.loss_generation()==1 && !t.poll());
  CHECK(t.stats().dropped_rx==64 && t.stats().queue_dropped_events==64);
  SimulatedTransport p;
  for (std::size_t i=0;i<64;++i) CHECK(p.add_tx(tx(encode_go().value,0))==Error::None);
  CHECK(p.add_tx(tx(encode_go().value,0))==Error::ScenarioCapacity);
  CHECK(p.add_rx(rx(file(),100))==Error::None);
  CHECK(p.add_rx(rx(file(),99))==Error::InvalidValue);
  auto bad=tx(encode_go().value,0); bad.read_count=1;
  SimulatedTransport v;
  CHECK(v.add_tx(bad)==Error::InvalidValue);
  bad=tx(encode_go().value,0); bad.duration_us={UINT64_MAX,1,0,0};
  CHECK(v.add_tx(bad)==Error::ClockRange);
  CHECK(v.activate(TransportMode::SyntheticReadOnly)==Error::None);
  CHECK(v.submit(request(encode_go().value),0)==Error::UnexpectedTx);
  CHECK(v.scenario_error()==Error::UnexpectedTx && !v.scenario_complete());
  SimulatedTransport missing;
  ReadOnlySession missing_session(missing,config());
  CHECK(missing_session.start(0)==Error::UnexpectedTx);
  const auto failures=missing_session.stats().failures;
  (void)missing_session.tick(10); (void)missing_session.tick(20);
  CHECK(missing_session.stats().failures==failures && missing_session.state()==SessionState::Fault);
}

void read_only() {
  SimulatedTransport t;
  CHECK(t.activate(TransportMode::SyntheticReadOnly)==Error::None);
  for (const auto& p:{encode_temperature(37,{82,81}).value,
      encode_switch(SwitchTarget::Light,true,{82,81}).value,
      encode_switch(SwitchTarget::Circulation,false,{82,81}).value,
      encode_program(Program::Away).value,lo(),Packet{}}) {
    CHECK(!SimulatedTransport::read_only_packet(p.bytes()));
    CHECK(t.submit(request(p),0)==Error::ReadOnly);
    CHECK(t.add_tx(tx(p,0))==Error::InvalidValue);
  }
  CHECK(t.stats().submitted==0 && t.stats().denied==6);
  ReadOnlySession default_session(t);
  CHECK(default_session.start(0)==Error::None);
  CHECK(default_session.state()==SessionState::Disabled);
  CHECK(default_session.request_control(encode_go().value.bytes())==Error::ReadOnly);
  CHECK(t.stats().submitted==0);
  SimulatedTransport passive;
  SessionConfig c; c.mode=TransportMode::Passive;
  ReadOnlySession s(passive,c);
  auto script=happy(); script.writes.clear(); script.install(passive);
  CHECK(s.start(0)==Error::None); run(s,10,700);
  CHECK(!s.snapshot(700).ready && !s.snapshot(700).observed);
  CHECK(passive.stats().submitted==0 && passive.role()==Role::Observing);
  for (std::size_t i=0;i<passive.trace_size();++i) CHECK(passive.trace()[i].role!=Role::Reply);
  CHECK(passive.submit(request(encode_go().value),700)==Error::Disabled);
  for (unsigned mismatch=0;mismatch<3;++mismatch) {
    SimulatedTransport invalid;
    auto cfg=config();
    if(mismatch==0) cfg.decode_mode=DecodeMode::DocumentedOnly;
    if(mismatch==1) cfg.expected_versions={0,0};
    if(mismatch==2) cfg.profile=CandidateProfile::InYtV50;
    ReadOnlySession session(invalid,cfg);
    CHECK(session.start(0)!=Error::None && invalid.stats().submitted==0);
  }
}

void handshake() {
  SimulatedTransport t; happy().install(t);
  ReadOnlySession s(t,config()); CHECK(s.start(0)==Error::None);
  CHECK(s.state()==SessionState::GoPending && !s.snapshot(0).ready);
  run(s,10,40); CHECK(s.state()==SessionState::AwaitingFiles);
  run(s,50,140); CHECK(s.state()==SessionState::AwaitingFiles && s.versions().config==0);
  run(s,150,240); CHECK(s.state()==SessionState::AwaitingClock && s.versions().config==82);
  run(s,250,340); CHECK(s.state()==SessionState::AwaitingLo);
  run(s,350,400); CHECK(s.state()==SessionState::Ready && !s.snapshot(400).ready);
  run(s,410,700);
  const auto state=s.snapshot(700);
  CHECK(state.ready && !state.stale && state.synthetic && !state.hardware_verified);
  CHECK(state.observed && state.observed->target.celsius==37 && state.observed->actual.celsius==36.5);
  CHECK(state.revision==1 && state.data_session==1 && state.status_age_us==200 && state.completed_age_us==0);
  CHECK(s.stats().attempts==1 && s.stats().statuses==1 && t.stats().submitted==4);
  CHECK(t.scenario_complete());
  const auto count=t.stats().submitted;
  for (unsigned i=0;i<100;++i) CHECK(s.snapshot(700).ready);
  CHECK(count==t.stats().submitted); // Reading the snapshot does not start traffic.
  CHECK(s.request_control(encode_temperature(37,{82,81}).value.bytes())==Error::ReadOnly);
  CHECK(count==t.stats().submitted && s.snapshot(700).ready);
}

void handshake_order() {
  for (auto premature:{lo(),clock_packet(),file(false)}) {
    SimulatedTransport t;
    CHECK(t.add_tx(tx(encode_go().value,0))==Error::None);
    CHECK(t.add_rx(rx(premature,100))==Error::None);
    ReadOnlySession s(t,config()); CHECK(s.start(0)==Error::None); run(s,10,100);
    CHECK(s.state()==SessionState::Backoff && s.last_error()==Error::HandshakeOrder);
    CHECK(!s.snapshot(100).ready && t.stats().submitted==1 && t.scenario_complete());
  }
  SimulatedTransport t;
  auto script=happy(); std::swap(script.reads[0].packet,script.reads[1].packet);
  script.install(t); auto c=config(); c.file_order=FileOrder::EitherSynthetic;
  ReadOnlySession s(t,c); CHECK(s.start(0)==Error::None); run(s,10,700);
  CHECK(s.snapshot(700).ready && t.scenario_complete());
  SimulatedTransport unknown;
  CHECK(unknown.add_tx(tx(encode_go().value,0))==Error::None);
  CHECK(unknown.add_rx(rx(file(true,83),100))==Error::None);
  ReadOnlySession u(unknown,config()); CHECK(u.start(0)==Error::None); run(u,10,100);
  CHECK(u.last_error()==Error::UnknownVersion && unknown.stats().submitted==1 && u.versions().config==0);
}

void duplicates() {
  SimulatedTransport t;
  Script script;
  script.writes={tx(encode_go().value,0),tx(encode_ack().value,100),
    tx(encode_ack().value,200),tx(encode_ack().value,300),tx(encode_ack().value,400),tx(encode_ack().value,500)};
  script.reads={rx(file(),100),rx(file(),200),rx(file(false),300),rx(clock_packet(),400),rx(clock_packet(),500),
    rx(lo(),600),rx(fragment(0,62),700,1,0),rx(fragment(62,62),800,1,1),rx(fragment(124,38,false),900,1,2)};
  script.install(t); ReadOnlySession s(t,config()); CHECK(s.start(0)==Error::None); run(s,10,900);
  CHECK(s.snapshot(900).ready && s.stats().failures==0 && t.scenario_complete());
  SimulatedTransport limit;
  Script bad;
  bad.writes={tx(encode_go().value,0),tx(encode_ack().value,100),tx(encode_ack().value,200),tx(encode_ack().value,300)};
  bad.reads={rx(file(),100),rx(file(),200),rx(file(),300),rx(file(),400)};
  bad.install(limit); ReadOnlySession capped(limit,config()); CHECK(capped.start(0)==Error::None); run(capped,10,400);
  CHECK(capped.last_error()==Error::HandshakeOrder && limit.stats().submitted==4);
  SimulatedTransport deadline;
  auto c=config(); c.duplicate_limit=8;
  Script stall;
  stall.writes={tx(encode_go().value,0),tx(encode_ack().value,100),tx(encode_ack().value,1000)};
  stall.reads={rx(file(),100),rx(file(),1000)};
  stall.install(deadline); ReadOnlySession d(deadline,c); CHECK(d.start(0)==Error::None); run(d,10,1140);
  CHECK(d.state()==SessionState::Backoff && d.last_error()==Error::Timeout);
  CHECK(deadline.stats().submitted==3); // Duplicate did not move deadline from 140+1000.
}

void freshness() {
  SimulatedTransport t; auto script=happy();
  script.reads.push_back(rx(encode_go().value,1000));
  auto malformed=incoming(33); script.reads.push_back(rx(malformed,2000));
  script.reads.push_back(rx(encode_program(Program::Standard).value,3000));
  script.install(t); ReadOnlySession s(t,config()); CHECK(s.start(0)==Error::None); run(s,10,3000);
  auto state=s.snapshot(3000);
  CHECK(state.status_age_us==2500 && state.candidate_frame_age_us==2300 && state.revision==1);
  CHECK(s.stats().invalid_rx==1 && s.stats().ignored_rx==2);
  CHECK(s.tick(10499)==Error::None && s.snapshot(10499).ready);
  CHECK(s.tick(10500)==Error::StaleState);
  state=s.snapshot(10500);
  CHECK(state.stale && !state.ready && state.observed && state.revision==1);
  CHECK(s.versions().config==0 && s.state()==SessionState::Backoff);
  SimulatedTransport no_status;
  auto p=happy(); p.reads.resize(4); p.install(no_status);
  ReadOnlySession n(no_status,config()); CHECK(n.start(0)==Error::None); run(n,10,1400);
  CHECK(n.state()==SessionState::Backoff && n.last_error()==Error::Timeout && !n.snapshot(1400).observed);
  // The oldest part, not just the final part, must fit inside freshness budget.
  SimulatedTransport old_parts; happy().install(old_parts);
  auto short_life=config(); short_life.stale_after_us=150;
  ReadOnlySession old(old_parts,short_life); CHECK(old.start(0)==Error::None); run(old,10,700);
  CHECK(!old.snapshot(700).ready && !old.snapshot(700).observed && old.stats().statuses==0);
}

void reconnect() {
  SimulatedTransport t; happy().install(t);
  ReadOnlySession s(t,config()); CHECK(s.start(0)==Error::None); run(s,10,700);
  CHECK(s.tick(10500)==Error::StaleState);
  const auto retry=10500+s.backoff_duration_us();
  auto next=happy(retry,2);
  next.reads.insert(next.reads.begin(),rx(file(),retry+50,1)); // Delayed old-epoch data.
  next.install(t);
  CHECK(s.tick(retry)==Error::None);
  CHECK(s.session_id()==2 && s.snapshot(retry).stale && s.snapshot(retry).observed);
  run(s,retry+10,retry+700);
  CHECK(s.snapshot(retry+700).ready && s.snapshot(retry+700).revision==2);
  CHECK(s.last_error()==Error::None && s.stats().stale_events==1 && t.scenario_complete());
  SimulatedTransport periodic;
  happy().install(periodic); happy(2000,2).install(periodic);
  auto c=config(); c.go_interval_us=2000;
  ReadOnlySession p(periodic,c); CHECK(p.start(0)==Error::None); run(p,10,1990);
  CHECK(p.snapshot(1990).ready && periodic.stats().submitted==4);
  CHECK(p.tick(2000)==Error::None && p.state()==SessionState::GoPending && p.snapshot(2000).stale);
  run(p,2010,2700); CHECK(p.snapshot(2700).ready && p.snapshot(2700).revision==2 && periodic.scenario_complete());
}

void session_faults() {
  // Each write in the handshake fails, at every supported failure outcome.
  for (std::size_t failed_tx=0;failed_tx<4;++failed_tx)
    for (auto error:{Error::AddressNack,Error::DataNack,Error::ArbitrationLost,Error::ShortRead,
        Error::Timeout,Error::RestoreFailed,Error::StuckSda,Error::StuckScl}) {
      SimulatedTransport t;
      auto script=happy(); script.writes.resize(failed_tx+1); script.reads.resize(failed_tx);
      auto& fault=script.writes[failed_tx]; fault.outcome=error;
      if(error==Error::ShortRead) {fault.fault_phase=Phase::Read; fault.read_count=1;}
      else if(error==Error::RestoreFailed) fault.fault_phase=Phase::Restore;
      else if(error!=Error::AddressNack) fault.partial_write=2;
      script.install(t); auto c=config(); c.max_failed_attempts=1;
      ReadOnlySession s(t,c); CHECK(s.start(0)==Error::None); run(s,10,700);
      CHECK(s.state()==SessionState::Fault && s.last_error()==error);
      CHECK(!s.snapshot(700).ready && s.versions().config==0 && s.stats().attempts==1);
      CHECK(s.last_tx_result() && s.last_tx_result()->error==error && t.scenario_complete());
      std::cout<<"CASE handshake_tx="<<failed_tx<<" outcome="<<error_name(error)<<" terminal_inhibited=true\n";
    }
  // Missing responses at each handshake stage have bounded recovery, no GO overlap.
  for (std::size_t files=0;files<4;++files) {
    SimulatedTransport t; auto script=happy();
    script.reads.resize(files); script.writes.resize(std::min<std::size_t>(files+1,4)); script.install(t);
    auto c=config(); c.go_interval_us=50; c.max_failed_attempts=1;
    ReadOnlySession s(t,c); CHECK(s.start(0)==Error::None); run(s,10,1500);
    CHECK(s.state()==SessionState::Fault && s.last_error()==Error::Timeout);
    CHECK(s.stats().attempts==1 && t.stats().submitted==script.writes.size());
  }
  // Bounded repeated failures exhaust the retry budget; no looping after Fault.
  SimulatedTransport retries;
  auto fault=tx(encode_go().value,0); fault.outcome=Error::AddressNack;
  CHECK(retries.add_tx(fault)==Error::None);
  ReadOnlySession s(retries,config()); CHECK(s.start(0)==Error::None);
  std::uint64_t time=10;
  for(unsigned attempt=1;attempt<=3;++attempt) {
    (void)s.tick(time);
    CHECK(s.stats().attempts==attempt);
    if(attempt<3) {
      const auto next=time+s.backoff_duration_us(); fault.earliest_us=fault.latest_us=next;
      CHECK(retries.add_tx(fault)==Error::None);
      CHECK(s.tick(next)==Error::None); time=next+10;
    }
  }
  CHECK(s.state()==SessionState::Fault && s.stats().failures==3);
  (void)s.tick(1000000); CHECK(s.stats().attempts==3 && retries.scenario_complete());
  // Each transport outcome can recover through a new epoch and full handshake.
  for (auto error:{Error::AddressNack,Error::DataNack,Error::ArbitrationLost,Error::ShortRead,
                  Error::Timeout,Error::RestoreFailed,Error::StuckSda,Error::StuckScl}) {
    SimulatedTransport recovery;
    auto p=tx(encode_go().value,0); p.outcome=error;
    if(error==Error::ShortRead) {p.fault_phase=Phase::Read; p.read_count=1;}
    else if(error==Error::RestoreFailed) p.fault_phase=Phase::Restore;
    else if(error!=Error::AddressNack) p.partial_write=2;
    CHECK(recovery.add_tx(p)==Error::None);
    ReadOnlySession recovered(recovery,config()); CHECK(recovered.start(0)==Error::None);
    (void)recovered.tick(40);
    CHECK(!recovered.snapshot(40).ready && recovered.versions().config==0);
    const auto next=error==Error::RestoreFailed ? 100 : 40+recovered.backoff_duration_us();
    happy(next,2).install(recovery);
    if(error==Error::RestoreFailed) {
      CHECK(recovery.role()==Role::Inactive && recovered.state()==SessionState::Fault);
      (void)recovered.tick(80); CHECK(recovered.stats().attempts==1);
      CHECK(recovered.start(next)==Error::None); // Explicit host-local restart only.
    } else CHECK(recovered.tick(next)==Error::None);
    run(recovered,next+10,next+700);
    CHECK(recovered.snapshot(next+700).ready && recovered.stats().attempts==2 && recovery.scenario_complete());
    CHECK(recovered.last_error()==Error::None);
    std::cout<<"CASE recovery="<<error_name(error)<<" new_epoch=2 fresh_synthetic_state=true\n";
  }
}

void loss_recovery() {
  // RX while the model owns the bus is an explicit transport loss, not a partial status.
  SimulatedTransport collision;
  CHECK(collision.add_tx(tx(encode_go().value,0))==Error::None);
  CHECK(collision.add_rx(rx(file(),5))==Error::None);
  ReadOnlySession s(collision,config()); CHECK(s.start(0)==Error::None); (void)s.tick(5);
  CHECK(s.state()==SessionState::Backoff && s.last_error()==Error::RxCollision && collision.loss_generation()==1);
  const auto next=5+s.backoff_duration_us(); happy(next,2,1).install(collision);
  CHECK(s.tick(next)==Error::None); run(s,next+10,next+700);
  CHECK(s.snapshot(next+700).ready && collision.scenario_complete());
  // Queue saturation invalidates session/profile before any queued frame can advance it.
  SimulatedTransport overflow;
  CHECK(overflow.add_tx(tx(encode_go().value,0))==Error::None);
  for(unsigned i=0;i<40;++i) CHECK(overflow.add_rx(rx(file(),100))==Error::None);
  ReadOnlySession o(overflow,config()); CHECK(o.start(0)==Error::None); (void)o.tick(40); (void)o.tick(100);
  CHECK(o.state()==SessionState::Backoff && o.last_error()==Error::TransportLoss);
  CHECK(!o.snapshot(100).observed && o.versions().config==0 && overflow.stats().submitted==1);
  // Reassembly loss must not refresh status; the next explicit index zero can recover.
  SimulatedTransport fragments;
  auto script=happy(); script.reads[5].context.synthetic_index=2;
  script.reads.push_back(rx(fragment(0,62),800,1,0));
  script.reads.push_back(rx(fragment(62,62),900,1,1));
  script.reads.push_back(rx(fragment(124,38,false),1000,1,2)); script.install(fragments);
  ReadOnlySession f(fragments,config()); CHECK(f.start(0)==Error::None); run(f,10,700);
  CHECK(!f.snapshot(700).observed && f.stats().assembly_aborts==2);
  run(f,710,1000); CHECK(f.snapshot(1000).ready && f.snapshot(1000).revision==1 && fragments.scenario_complete());
  SimulatedTransport interleaved;
  auto mixed=happy(); mixed.reads.insert(mixed.reads.begin()+5,rx(encode_go().value,550));
  mixed.install(interleaved); ReadOnlySession m(interleaved,config());
  CHECK(m.start(0)==Error::None); run(m,10,700);
  CHECK(!m.snapshot(700).observed && m.stats().assembly_aborts>0);
  SimulatedTransport expired;
  auto gap=happy(); gap.reads.resize(5);
  gap.reads.push_back(rx(fragment(62,62),250510,1,1));
  gap.reads.push_back(rx(fragment(0,62),250600,1,0));
  gap.reads.push_back(rx(fragment(62,62),250700,1,1));
  gap.reads.push_back(rx(fragment(124,38,false),250800,1,2)); gap.install(expired);
  auto long_life=config(); long_life.stale_after_us=long_life.status_timeout_us=long_life.go_interval_us=1000000;
  ReadOnlySession e(expired,long_life); CHECK(e.start(0)==Error::None); run(e,10,500);
  CHECK(e.tick(250500)==Error::None && e.stats().assembly_aborts==1 && !e.snapshot(250500).observed);
  run(e,250510,250800);
  CHECK(e.snapshot(250800).ready && e.snapshot(250800).revision==1 && expired.scenario_complete());
}

void determinism() {
  SimulatedTransport a,b;
  happy().install(a); happy().install(b);
  ReadOnlySession sa(a,config()),sb(b,config());
  CHECK(sa.start(0)==Error::None && sb.start(0)==Error::None);
  run(sa,10,700); run(sb,10,700);
  CHECK(a.trace_size()==b.trace_size());
  for(std::size_t i=0;i<a.trace_size();++i)
    CHECK(a.trace()[i].at_us==b.trace()[i].at_us && a.trace()[i].role==b.trace()[i].role && a.trace()[i].tx_id==b.trace()[i].tx_id);
  CHECK(sa.snapshot(700).revision==sb.snapshot(700).revision && sa.stats().statuses==sb.stats().statuses);
  auto fault=tx(encode_go().value,0); fault.outcome=Error::AddressNack;
  SimulatedTransport x,y;
  CHECK(x.add_tx(fault)==Error::None && y.add_tx(fault)==Error::None);
  ReadOnlySession sx(x,config()),sy(y,config());
  CHECK(sx.start(0)==Error::None && sy.start(0)==Error::None);
  (void)sx.tick(10); (void)sy.tick(10);
  CHECK(sx.backoff_duration_us()==sy.backoff_duration_us());
  CHECK(sx.backoff_duration_us()>=100 && sx.backoff_duration_us()<=125);
}

void temporal_boundaries() {
  SimulatedTransport t;
  ReadOnlySession s(t,config());
  CHECK(s.start(UINT64_MAX-50)==Error::ClockRange && s.state()==SessionState::Fault);
  CHECK(t.stats().submitted==0);
  SimulatedTransport backwards;
  happy().install(backwards); ReadOnlySession b(backwards,config());
  CHECK(b.start(0)==Error::None); run(b,10,700);
  CHECK(b.snapshot(699).stale && !b.snapshot(699).ready);
  CHECK(b.tick(699)==Error::ClockRegression && b.state()==SessionState::Fault && b.snapshot(700).stale);
  CHECK(backwards.role()==Role::Inactive);
  // Late poll cannot use already queued RX to revive an expired handshake.
  SimulatedTransport late;
  auto script=happy(); script.writes.resize(1); script.install(late);
  ReadOnlySession l(late,config()); CHECK(l.start(0)==Error::None);
  (void)l.tick(1040);
  CHECK(l.state()==SessionState::Backoff && l.last_error()==Error::Timeout && late.stats().submitted==1);
  // Transaction metadata is checked independently of payload intent.
  SimulatedTransport invalid;
  CHECK(invalid.add_tx(tx(encode_go().value,0))==Error::None);
  CHECK(invalid.activate(TransportMode::SyntheticReadOnly)==Error::None);
  for(unsigned field=0;field<5;++field) {
    auto q=request(encode_go().value);
    if(field==0) q.address=0x18;
    if(field==1) q.read_length=1;
    if(field==2) q.repeated_start=false;
    if(field==3) q.final_stop=false;
    if(field==4) q.id=0;
    CHECK(invalid.submit(q,0)==Error::InvalidValue);
  }
  CHECK(invalid.stats().submitted==0);
  SimulatedTransport late_start;
  CHECK(late_start.add_tx(tx(encode_go().value,1000))==Error::None);
  ReadOnlySession activated(late_start,config());
  CHECK(activated.start(1000)==Error::None);
  for(std::size_t i=0;i<late_start.trace_size();++i)
    if(late_start.trace()[i].role==Role::Listen) CHECK(late_start.trace()[i].at_us==1000);
}

} // namespace

int main(int argc,char** argv) {
  if(argc!=2) return 2;
  const std::string name=argv[1];
  try {
    if(name=="transport_success") transport_success();
    else if(name=="transport_faults") transport_faults();
    else if(name=="transport_deadlines") transport_deadlines();
    else if(name=="transport_capacity") transport_capacity();
    else if(name=="read_only") read_only();
    else if(name=="handshake") handshake();
    else if(name=="handshake_order") handshake_order();
    else if(name=="duplicates") duplicates();
    else if(name=="freshness") freshness();
    else if(name=="reconnect") reconnect();
    else if(name=="session_faults") session_faults();
    else if(name=="loss_recovery") loss_recovery();
    else if(name=="determinism") determinism();
    else if(name=="temporal_boundaries") temporal_boundaries();
    else return 2;
    std::cout<<"PASS "<<name<<": "<<checks<<" checks; evidence=synthetic; physical_io=false\n";
  } catch(const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; return 1; }
}
