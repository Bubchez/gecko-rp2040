// SPDX-License-Identifier: MIT
// Host driver for the exact bounded parser/serializer linked into the Pico image.
#include "gecko/runtime.hpp"
#include <iostream>
#include <sstream>
#include <string>
using namespace gecko;
using namespace gecko::runtime;
int main(int argc,char** argv) {
  simulation::Scenario scenario=simulation::Scenario::Healthy;
  if(argc==2) {
    const std::string name=argv[1];
    if(name=="unavailable") scenario=simulation::Scenario::Unavailable;
    else if(name=="stale") scenario=simulation::Scenario::Stale;
    else if(name=="recovery") scenario=simulation::Scenario::Recovery;
    else if(name=="invalid_temperature") scenario=simulation::Scenario::InvalidTemperature;
    else if(name!="healthy") return 2;
  }
  simulation::Model model(scenario);Api api;Credentials c;
  if(!parse_provision("PROVISION 74657374 70617373776f7264 0123456789abcdef0123456789abcdef gecko-host-mock-001",c) ||
     !api.configure(c,"00000000000000000000000000000000")) return 3;
  api.publish(model,0);
  std::string line;
  while(std::getline(std::cin,line)) {
    std::istringstream input(line);std::string command;input>>command;
    if(command=="ADVANCE") {
      std::uint64_t now=0;input>>now;
      if(!model.advance(now)) return 4;
      api.publish(model,now);std::cout<<"{}"<<std::endl;
    } else if(command=="UNAVAILABLE") {api.unavailable();std::cout<<"{}"<<std::endl;}
    else if(command=="REQUEST") {
      std::uint64_t now=0,start=0;std::string hex;input>>now>>start>>hex;
      Request request;request.reset(start);
      for(std::size_t i=0;i+1<hex.size();i+=2) {
        const auto byte=static_cast<char>(std::stoul(hex.substr(i,2),nullptr,16));if(!request.append(&byte,1)) return 5;
      }
      Response response;api.respond(request,response,now);
      std::cout<<"{\"status\":"<<response.status<<",\"authenticate\":"<<(response.authenticate?"true":"false")
        <<",\"retry\":"<<(response.retry?"true":"false")<<",\"head\":"<<(response.head?"true":"false")<<",\"allow\":";
      if(response.allow) std::cout<<'"'<<response.allow<<'"';else std::cout<<"null";
      std::cout<<",\"body\":";
      if(response.status) std::cout.write(response.body.data(),static_cast<std::streamsize>(response.size));else std::cout<<"null";
      std::cout<<'}'<<std::endl;
    } else if(command=="SIZES") {
      std::cout<<"{\"model\":"<<sizeof(model)<<",\"api\":"<<sizeof(api)<<",\"request\":"<<sizeof(Request)<<",\"response\":"<<sizeof(Response)<<'}'<<std::endl;
    } else return 6;
  }
}
