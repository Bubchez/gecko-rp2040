// SPDX-License-Identifier: MIT
#include "gecko/model.hpp"
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
using namespace gecko::simulation;
template<typename T> void optional(const std::optional<T>& value) {
  if(value) std::cout<<*value; else std::cout<<"null";
}

void print_snapshot(const Model& model) {
    const auto& session_=model.session();
    const auto& transport_=model.transport();
    const auto& config_=model.config();
    const auto now_ms_=model.now_ms();
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
      if(line=="SNAPSHOT") {print_snapshot(model);continue;}
      std::uint64_t target=0;
      if(line.substr(0,8)=="ADVANCE ") {
        const auto number=line.substr(8);
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),target);
        if(parsed.ec==std::errc{} && parsed.ptr==number.data()+number.size() && model.advance(target)) {
          print_snapshot(model);continue;
        }
      }
      std::cout<<"{\"backend_error\":\"invalid_command\"}"<<std::endl;
    }
  } catch(const std::exception&) {std::cout<<"{\"backend_error\":\"model_failure\"}"<<std::endl;return 1;}
}
