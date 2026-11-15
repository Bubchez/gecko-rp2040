// SPDX-License-Identifier: MIT
#include "gecko/runtime.hpp"
#include "gecko/identity.hpp"
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <limits>
namespace gecko::runtime {
namespace {
void increment(std::uint32_t& n) noexcept {if(n!=UINT32_MAX) ++n;}
class Writer {
 public:
  explicit Writer(Response& r) noexcept:r_(r) {r_.size=0;}
  void raw(std::string_view s) noexcept {
    if(!good_ || s.size()>max_response-r_.size) {good_=false;return;}
    std::memcpy(r_.body.data()+r_.size,s.data(),s.size());r_.size+=s.size();r_.body[r_.size]=0;
  }
  void string(std::string_view s) noexcept {
    raw("\"");
    for(char c:s) {
      if(c=='"' || c=='\\') {raw("\\");raw({&c,1});}
      else if(static_cast<unsigned char>(c)<32) {char b[7];std::snprintf(b,sizeof(b),"\\u%04x",static_cast<unsigned char>(c));raw(b);}
      else raw({&c,1});
    }
    raw("\"");
  }
  void value(bool v) noexcept {raw(v?"true":"false");}
  void value(std::uint64_t v) noexcept {char b[24];std::snprintf(b,sizeof(b),"%" PRIu64,v);raw(b);}
  void value(double v) noexcept {char b[32];std::snprintf(b,sizeof(b),"%.17g",v);raw(b);}
  template<typename T> void optional(const std::optional<T>& v) noexcept {if(v) value(*v);else raw("null");}
  void count(std::uint64_t n) noexcept {value(std::min(n,static_cast<std::uint64_t>(UINT32_MAX)));}
  bool good() const noexcept {return good_;}
 private:
  Response& r_;bool good_{true};
};
bool lower_equal(std::string_view a,std::string_view b) noexcept {
  if(a.size()!=b.size()) return false;
  for(std::size_t i=0;i<a.size();++i) {auto c=a[i];if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');if(c!=b[i]) return false;}
  return true;
}
std::string_view trim(std::string_view s) noexcept {
  while(!s.empty() && (s.front()==' ' || s.front()=='\t')) s.remove_prefix(1);
  while(!s.empty() && (s.back()==' ' || s.back()=='\t')) s.remove_suffix(1);
  return s;
}
struct Header {std::string_view value{};unsigned count{0};void add(std::string_view s) noexcept {value=s;++count;}};
void error(Response& r,unsigned status,std::string_view code,std::string_view id={}) noexcept {
  r.status=status;r.authenticate=status==401;r.retry=status==429 || status==503;
  Writer w(r);w.raw("{\"api_version\":\"1.0\",\"error\":{\"code\":");w.string(code);w.raw(",\"message\":\"");
  for(char c:code) {if(c=='_') c=' ';w.raw({&c,1});}
  w.raw("\",\"request_id\":");if(id.empty()) w.raw("null");else w.string(id);
  w.raw(",\"retryable\":");w.value(r.retry);w.raw("}}");
}
const std::array<std::string_view,4> reminders{"rinse_filter","clean_filter","change_water","spa_checkup"};
constexpr std::string_view unavailable_paths="[\"/blower_on\",\"/waterfall_on\",\"/program\",\"/lock_mode\",\"/pack_type\",\"/pump_timer_minutes\",\"/spa_clock/local_datetime\",\"/spa_clock/local_time\",\"/spa_clock/timezone\",\"/reminders/rinse_filter/due_date\",\"/reminders/clean_filter/due_date\",\"/reminders/change_water/due_date\",\"/reminders/spa_checkup/due_date\"]";
}
bool Request::append(const void* bytes,std::size_t n) noexcept {
  if(size>data.size()-1 || n>data.size()-1-size || (bytes==nullptr && n!=0)) return false;
  if(n!=0) std::memcpy(data.data()+size,bytes,n);
  size+=n;data[size]=0;return true;
}
bool Api::configure(const Credentials& c,std::string_view boot) noexcept {
  if(!c.valid() || boot.size()!=32 || std::any_of(boot.begin(),boot.end(),[](char b){return !((b>='0' && b<='9') || (b>='a' && b<='f'));})) return false;
  credentials_=c;std::copy(boot.begin(),boot.end(),boot_.begin());boot_[32]=0;configured_=true;return true;
}
void Api::publish(const simulation::Model& m,std::uint64_t wall) noexcept {
  const auto s=m.snapshot();
  if(m.error()!=Error::None || s.hardware_verified || s.revision>UINT32_MAX || (cached_ && wall<published_ms_)) {cached_=false;return;}
  snapshot_=s;session_stats_=m.session().stats();transport_stats_=m.transport().stats();versions_=m.session().versions();
  state_=m.session().state();error_=m.session().last_error();scenario_=m.scenario();model_ms_=m.now_ms();
  stale_after_ms_=m.config().stale_after_us/1000;published_ms_=wall;cached_=true;
}
void Api::document(std::string_view path,Response& r,std::uint64_t now) noexcept {
  r.status=200;Writer w(r);
  w.raw("{\"api_version\":\"1.0\",\"boot_id\":");w.string(boot_.data());
  if(path=="/api/v1/health") {w.raw(",\"service\":\"gecko-pico-bridge\",\"uptime_ms\":");w.value(now);w.raw(",\"live\":true}");return;}
  w.raw(",\"transport\":\"simulated\",\"mode\":\"simulation\"");
  const auto& s=snapshot_;
  if(path=="/api/v1/capabilities") {
    w.raw(",\"device_id\":");w.string(credentials_.device.data());
    w.raw(",\"firmware_version\":");w.string(firmware_version);
    w.raw(",\"firmware_build_id\":");w.string(firmware_build_id);
    w.raw(",\"read_only\":true,\"physical_control_enabled\":false,\"profile\":{\"target_model\":\"IN.YE-3-H3.0 / YE-3-CE V2\",\"config_file\":");
    const bool bound=versions_.config==82 && versions_.status==81;
    w.raw(bound?"\"inYT_C82.xml\",\"status_file\":\"inYT_S81.xml\",\"layout\":\"inyt-v51-candidate\",\"validation\":\"synthetic\"}":"null,\"status_file\":null,\"layout\":null,\"validation\":\"unknown\"}");
    w.raw(",\"read_fields\":[\"temperatures\",\"heater\",\"standby\",\"light_demand\",\"circulation\",\"pumps\"],\"commands\":{");
    bool first=true;for(auto name:{"light.set","temperature.set","program.set","pump.set"}) {
      if(!first) w.raw(",");
      first=false;w.string(name);w.raw(":{\"supported\":false,\"enabled\":false,\"reason\":\"read_only\"}");
    }
    w.raw("},\"equipment\":{\"pump1\":null,\"pump2\":null,\"pump3\":null,\"pump4\":null},\"limits\":{\"command_queue\":0,\"command_history\":0,\"idempotency_retention_ms\":0,\"max_body_bytes\":512,\"max_header_bytes\":2048,\"max_uri_bytes\":128,\"max_response_bytes\":8192,\"max_connections\":2,\"requests_per_second\":5,\"request_burst\":10}}");
  } else if(path=="/api/v1/status") {
    const auto elapsed=now-published_ms_;
    auto age=[&](const std::optional<std::uint64_t>& us) {if(us) w.value(*us/1000+elapsed);else w.raw("null");};
    const bool stale=s.stale || !s.status_age_us || *s.status_age_us/1000+elapsed>=stale_after_ms_;
    const bool ready=s.ready && !stale;
    const auto bus=ready?"healthy":state_==SessionState::Fault?"fault":state_==SessionState::Disabled?"disabled":s.observed?"degraded":"unknown";
    w.raw(",\"device_id\":");w.string(credentials_.device.data());w.raw(",\"state_revision\":");w.value(s.revision);
    w.raw(",\"connection\":{\"bus_state\":");w.string(bus);w.raw(",\"session_state\":");w.string(simulation::state_name(state_));
    w.raw(",\"last_valid_frame_age_ms\":");age(s.candidate_frame_age_us);
    w.raw(",\"last_status_age_ms\":");age(s.status_age_us);w.raw(",\"status_completed_age_ms\":");age(s.completed_age_us);
    w.raw(",\"stale_after_ms\":");w.value(stale_after_ms_);w.raw(",\"ready\":");w.value(ready);
    w.raw("},\"quality\":{\"valid\":");w.value(s.observed.has_value());w.raw(",\"stale\":");w.value(stale);
    w.raw(",\"source\":");w.string(s.observed?"synthetic":"unknown");w.raw(",\"invalid_fields\":[");
    if(s.observed) {
      bool first=true;
      auto invalid=[&](bool bad,std::string_view name){if(bad){if(!first) w.raw(",");
      first=false;w.string(name);}};
      invalid(!s.observed->actual.celsius,"/temperature_c");invalid(!s.observed->target.celsius,"/target_temperature_c");invalid(!s.observed->standby,"/standby");
    }
    w.raw("],\"unavailable_fields\":");w.raw(unavailable_paths);w.raw(",\"cached_fields\":[]},\"state\":{\"temperature_c\":");
    if(s.observed) w.optional(s.observed->actual.celsius);else w.raw("null");
    w.raw(",\"target_temperature_c\":");if(s.observed) w.optional(s.observed->target.celsius);else w.raw("null");
    w.raw(",\"heater_on\":");if(s.observed) w.value(s.observed->heater_on);else w.raw("null");
    w.raw(",\"standby\":");if(s.observed) w.optional(s.observed->standby);else w.raw("null");
    w.raw(",\"light_demand_on\":");if(s.observed) w.value(s.observed->light_demand_on);else w.raw("null");
    w.raw(",\"circulation_on\":");if(s.observed) w.value(s.observed->circulation_on);else w.raw("null");
    w.raw(",\"blower_on\":null,\"waterfall_on\":null,\"program\":null,\"lock_mode\":null,\"pack_type\":null,\"pump_timer_minutes\":null,\"pumps\":[");
    if(s.observed) {
      constexpr std::array<const char*,4> outputs{"off","high","low","unknown"},demands{"off","low","high","unknown"};
      for(unsigned i=0;i<4;++i) {
        if(i!=0) w.raw(",");
        w.raw("{\"id\":\"pump");w.value(static_cast<std::uint64_t>(i+1));w.raw("\",\"state\":");
        const auto output=static_cast<unsigned>(s.observed->pumps[i]);w.string(outputs[output<4?output:3]);
        w.raw(",\"user_demand\":");w.string(demands[(static_cast<unsigned>(s.observed->user_demand)>>(2*i))&3U]);w.raw("}");
      }
    }
    w.raw("],\"spa_clock\":{\"local_datetime\":null,\"local_time\":null,\"timezone\":null},\"reminders\":{");
    bool first=true;for(auto name:reminders) {if(!first) w.raw(",");
      first=false;w.string(name);w.raw(":{\"due_date\":null,\"source\":\"unknown\",\"age_ms\":null}");}
    w.raw("}}}");
  } else {
    const auto& c=session_stats_;const auto& t=transport_stats_;
    w.raw(",\"scenario\":");w.string(simulation::scenario_name(scenario_));w.raw(",\"simulation_time_ms\":");w.value(model_ms_);w.raw(",\"counters\":{");
    bool first=true;
    auto count=[&](std::string_view key,std::uint64_t n){if(!first) w.raw(",");
      first=false;w.string(key);w.raw(":");w.count(n);};
    count("attempts",c.attempts);count("failures",c.failures);count("invalid_rx",c.invalid_rx);count("ignored_rx",c.ignored_rx);
    count("stale_events",c.stale_events);count("assembly_aborts",c.assembly_aborts);count("statuses",c.statuses);
    count("tx_submitted",t.submitted);count("tx_completed",t.completed);count("rx_dropped",t.dropped_rx);count("queue_overflows",t.queue_overflows);
    count("checkpoints",t.checkpoints);count("retired_trace_events",t.retired_trace_events);
    w.raw("},\"http\":{");first=true;count("unauthorized",unauthorized_);count("denied_commands",denied_);count("rate_limited",limited_);
    w.raw("},\"timing_us\":{\"max_irq_service\":null,\"max_role_switch\":null,\"max_handshake_ack\":null},\"memory\":{\"min_free_bytes\":null,\"stack_headroom_bytes\":null},\"last_error\":");
    const auto e=simulation::error_name(error_);if(e) {w.raw("{\"code\":");w.string(e);w.raw(",\"age_ms\":null}");}else w.raw("null");
    w.raw(",\"wifi\":{\"state\":");w.string(wifi_name(diagnostics_.wifi));w.raw(",\"attempts\":");w.count(diagnostics_.wifi_attempts);
    w.raw(",\"reconnects\":");w.count(diagnostics_.wifi_reconnects);w.raw("},\"safety\":{\"gecko_gpio\":\"inactive\",\"physical_i2c_compiled\":false,\"actuator_execution_compiled\":false}}");
  }
  if(!w.good()) error(r,500,"internal_error");
}
void Api::respond(const Request& request,Response& r,std::uint64_t now) noexcept {
  r.size=0;r.status=0;r.allow=nullptr;r.authenticate=false;r.retry=false;r.head=false;
  if(request.size>request.data.size()-1) {error(r,431,"headers_too_large");return;}
  if(now<request.started_ms || now-request.started_ms>=2000) {error(r,408,"request_timeout");return;}
  const std::string_view bytes(request.data.data(),request.size);
  const auto line_end=bytes.find("\r\n");
  if(line_end==std::string_view::npos) {if(bytes.size()>256) error(r,414,"uri_too_long");return;}
  const auto line=bytes.substr(0,line_end);const auto one=line.find(' '),two=one==std::string_view::npos?one:line.find(' ',one+1);
  if(one==std::string_view::npos || two==std::string_view::npos || one==0 || two==one+1 || line.find(' ',two+1)!=std::string_view::npos || line.substr(two+1)!="HTTP/1.1") {error(r,400,"invalid_http");return;}
  const auto method=line.substr(0,one),path=line.substr(one+1,two-one-1);r.head=method=="HEAD";
  if(path.size()>128 || line.size()>254) {error(r,414,"uri_too_long");return;}
  if(path.front()!='/' || path.substr(0,2)=="//" || path.find_first_of("?#%")!=std::string_view::npos ||
      std::any_of(line.begin(),line.end(),[](char c){return c<32 || c>126;})) {error(r,400,"invalid_http");return;}
  const auto header_end=bytes.find("\r\n\r\n");
  if(header_end==std::string_view::npos) {if(bytes.size()>max_headers) error(r,431,"headers_too_large");return;}
  const auto body_at=header_end+4;
  if(body_at>max_headers) {error(r,431,"headers_too_large");return;}
  Header host,auth,length,type,encoding,transfer,expect;
  auto at=line_end+2;
  while(at<header_end) {
    const auto end=bytes.find("\r\n",at);const auto h=bytes.substr(at,end-at);const auto colon=h.find(':');
    if(colon==std::string_view::npos || colon==0 || std::any_of(h.begin(),h.end(),[](char c){return (c<32 && c!='\t') || c>126;})) {error(r,400,"invalid_http");return;}
    const auto name=h.substr(0,colon),v=trim(h.substr(colon+1));
    constexpr std::string_view punctuation="!#$%&'*+-.^_`|~";
    if(std::any_of(name.begin(),name.end(),[&](char c){return !((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || punctuation.find(c)!=std::string_view::npos);})) {error(r,400,"invalid_http");return;}
    if(lower_equal(name,"host")) host.add(v);else if(lower_equal(name,"authorization")) auth.add(v);
    else if(lower_equal(name,"content-length")) length.add(v);else if(lower_equal(name,"content-type")) type.add(v);
    else if(lower_equal(name,"content-encoding")) encoding.add(v);else if(lower_equal(name,"transfer-encoding")) transfer.add(v);else if(lower_equal(name,"expect")) expect.add(v);
    at=end+2;
  }
  if(host.count!=1 || host.value.empty() || length.count>1 || transfer.count || expect.count) {error(r,400,"invalid_http");return;}
  std::size_t body_size=0;
  if(length.count) {
    if(length.value.empty() || length.value.size()>10 || std::any_of(length.value.begin(),length.value.end(),[](char c){return c<'0' || c>'9';})) {error(r,400,"invalid_http");return;}
    // Saturate, since size_t on RP2040 is 32-bit and ten decimal digits may overflow.
    for(char c:length.value) body_size=std::min(body_size*10+static_cast<unsigned>(c-'0'),static_cast<std::size_t>(513));
  }
  if(body_size>max_body) {error(r,413,"body_too_large");return;}
  const bool health=method=="GET" && path=="/api/v1/health";
  if(!health) {
    const auto sep=auth.value.find(' ');
    bool ok=configured_ && auth.count==1 && sep!=std::string_view::npos && lower_equal(auth.value.substr(0,sep),"bearer");
    const auto supplied=sep==std::string_view::npos?std::string_view{}:auth.value.substr(sep+1);
    const auto expected=std::string_view(credentials_.token.data());
    unsigned diff=static_cast<unsigned>(supplied.size()^expected.size());
    // Always compare all 128 possible token positions; no early content-dependent exit.
    for(std::size_t i=0;i<128;++i) diff|=static_cast<unsigned>((i<supplied.size()?static_cast<unsigned char>(supplied[i]):0U)^(i<expected.size()?static_cast<unsigned char>(expected[i]):0U));
    ok=ok && diff==0;
    if(!ok) {increment(unauthorized_);error(r,401,"unauthorized");return;}
  }
  const bool history=path.substr(0,17)=="/api/v1/commands/" && identifier(path.substr(17));
  const char* required=nullptr;
  if(path=="/api/v1/health" || path=="/api/v1/status" || path=="/api/v1/capabilities" || path=="/api/v1/diagnostics" || history) required="GET";
  else if(path=="/api/v1/commands") required="POST";
  if(!required) {error(r,404,"not_found");return;}
  if(method!=required) {error(r,405,"method_not_allowed");r.allow=required;return;}
  if(method=="GET" && body_size) {error(r,400,"body_not_allowed");return;}
  // Refill only once a complete valid request is ready, so fragmented POSTs do not consume tokens repeatedly.
  if(method=="POST") {
    if(!length.count) {error(r,400,"invalid_http");return;}
    if(type.count!=1 || (!lower_equal(type.value,"application/json") && !lower_equal(type.value,"application/json; charset=utf-8")) || encoding.count) {error(r,415,"unsupported_media_type");return;}
    if(bytes.size()-body_at<body_size) return;
  }
  if(!health) {
    if(now<refill_ms_) {error(r,503,"bridge_unavailable");return;}
    const auto delta=std::min(now-refill_ms_,static_cast<std::uint64_t>(2000));refill_ms_=now;
    allowance_milli_=std::min<std::uint32_t>(10000,allowance_milli_+static_cast<std::uint32_t>(delta*5));
    if(allowance_milli_<1000) {increment(limited_);error(r,429,"rate_limited");return;}allowance_milli_-=1000;
  }
  if(history) {error(r,404,"unknown_request");return;}
  if(method=="POST") {
    const auto json=check_json(bytes.substr(body_at,body_size));
    if(!json.valid) {error(r,400,"invalid_json");return;}
    if(!json.object) {error(r,422,"invalid_parameters");return;}
    increment(denied_);error(r,403,"read_only",json.request_id.data());return;
  }
  if(!health && (!cached_ || now<published_ms_ || now-published_ms_>=1000)) {error(r,503,"bridge_unavailable");return;}
  document(path,r,now);
}
} // namespace gecko::runtime
