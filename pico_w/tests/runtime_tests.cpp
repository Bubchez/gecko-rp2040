// SPDX-License-Identifier: MIT
#include "gecko/runtime.hpp"
#include "gecko/usb.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace gecko;
using namespace gecko::runtime;
#define CHECK(condition) do { if(!(condition)) {std::cerr<<__LINE__<<": " #condition "\n";std::exit(1);} } while(false)
namespace {
Credentials credentials() {
  Credentials c;CHECK(parse_provision("PROVISION 74657374 70617373776f7264 0123456789abcdef0123456789abcdef unit-1",c));return c;
}
void config() {
  std::array<char,513> info{};
  UsbInfo s{"00000000000000000000000000000000","unit-1","192.168.1.2",true,WifiState::Online,1,0};
  CHECK(format_usb_info(s,info)>0);CHECK(check_json(info.data()).valid);
  const std::string longest_id(64,'x');s.device_id=longest_id;s.ip="255.255.255.255";s.wifi=WifiState::Unprovisioned;s.attempts=s.reconnects=UINT32_MAX;
  const auto info_size=format_usb_info(s,info);CHECK(info_size>64 && info_size<512);CHECK(check_json(info.data()).valid);
  s.ip="999.1.1.1";CHECK(format_usb_info(s,info)==0);CHECK(info[0]==0);
  s.ip="1.2.3.4";s.boot_id="wrong";CHECK(format_usb_info(s,info)==0);
  auto c=credentials();CHECK(c.valid());ConfigRecord a{},b{},invalid{};Credentials out;std::uint32_t seq=99;
  CHECK(!decode_record(invalid,out,seq));CHECK(seq==99);CHECK(!out.valid());
  CHECK(encode_record(c,1,a));CHECK(decode_record(a,out,seq));CHECK(seq==1);CHECK(out.valid());
  CHECK(newest_record(a,b)==0);CHECK(encode_record(c,2,b));CHECK(newest_record(a,b)==1);
  // Every byte is integrity covered; outputs are transactional on failure.
  for(std::size_t i=0;i<a.size();++i) {
    auto broken=a;broken[i]^=1;CHECK(!decode_record(broken,out,seq));CHECK(seq==1);
  }
  for(std::size_t n=0;n<b.size();++n) {auto torn=b;std::fill(torn.begin()+static_cast<std::ptrdiff_t>(n),torn.end(),0xff);CHECK(newest_record(a,torn)==0);}
  CHECK(!encode_record(c,0,b));
  CHECK(!parse_provision("PROVISION 7465737400 70617373776f7264 0123456789abcdef0123456789abcdef unit-1",out));
  CHECK(!parse_provision("PROVISION 74 70617373 short bad id",out));
  c.token.fill('x');CHECK(!c.valid());c=credentials();c.password[2]=0;CHECK(!c.valid());
}
void wifi() {
  WifiPolicy p;CHECK(p.tick(0,false,false)==WifiAction::None);
  p.configure(true,0);CHECK(p.tick(0,false,false)==WifiAction::Connect);CHECK(p.attempts()==1);
  CHECK(p.tick(14999,false,false)==WifiAction::None);CHECK(p.tick(15000,false,false)==WifiAction::Disconnect);
  CHECK(p.tick(15999,false,false)==WifiAction::None);CHECK(p.tick(16000,false,false)==WifiAction::Connect);
  CHECK(p.tick(16001,true,false)==WifiAction::None);CHECK(p.state()==WifiState::Online);
  CHECK(!p.reconnect(UINT64_MAX));
  CHECK(p.tick(16002,false,false)==WifiAction::Disconnect);CHECK(p.reconnects()==1);
  CHECK(p.tick(17002,false,false)==WifiAction::Connect);
  CHECK(p.tick(17003,false,true)==WifiAction::Disconnect);
  std::uint64_t now=17003;
  for(unsigned i=0;i<100;++i) {now+=60001;CHECK(p.tick(now,false,false)==WifiAction::Connect);CHECK(p.tick(now+1,false,true)==WifiAction::Disconnect);}
  p.configure(false,now);CHECK(p.state()==WifiState::Unprovisioned);CHECK(p.tick(now+100000,true,false)==WifiAction::None);
  CHECK(!p.reconnect(now));p.configure(true,now);
  CHECK(p.tick(now,false,false)==WifiAction::Connect);CHECK(p.tick(now+1,true,false)==WifiAction::None);
  const auto before=p.reconnects();CHECK(p.reconnect(now+2));CHECK(p.reconnects()==before+1);
  CHECK(!p.reconnect(now+3));CHECK(p.tick(now+1001,true,false)==WifiAction::None);
  CHECK(p.tick(now+1002,false,false)==WifiAction::Connect);CHECK(p.tick(now+1003,true,false)==WifiAction::None);
}
void json() {
  for(auto s:{"{}","[]","{\"a\":[true,false,null,-0.2e-10]}","{\"a\":{\"a\":1},\"b\":{\"a\":2}}","{\"x\":\"\\ud83d\\ude00\"}","{\"n\":1e-999}"}) CHECK(check_json(s).valid);
  for(auto s:{"","{","{\"a\":1,}","[1,]","[01]","{\"a\":1,\"\\u0061\":2}","{\"x\":{\"a\":0,\"a\":1}}","{\"n\":1e999}","{\"x\":NaN}","{\"x\":\"\\ud800\"}","{\"x\":\"\\udc00\"}","{\"x\":\"\\u000g\"}","true false","{\"x\":\"\xc0\x80\"}"}) CHECK(!check_json(s).valid);
  CHECK(std::string(check_json("{\"request_id\":\"id-1\"}").request_id.data())=="id-1");
  CHECK(std::string(check_json("{\"request_id\":\"id/1\"}").request_id.data()).empty());
  const std::string deep=std::string(255,'[')+"{}"+std::string(255,']');CHECK(deep.size()==512);CHECK(check_json(deep).valid);
  const std::string object="{\"request_id\":\"\\u0061\",\"nested\":{\"request_id\":\"b\"}}";
  CHECK(std::string(check_json(object).request_id.data())=="a");
  std::uint32_t seed=7;
  for(unsigned n=0;n<20000;++n) {
    seed=seed*1664525U+1013904223U;std::string bytes(seed%513,' ');
    for(auto& b:bytes) {seed=seed*1664525U+1013904223U;b=static_cast<char>(seed>>24);}
    (void)check_json(bytes);
  }
}
void ownership() {
  simulation::Model model;CHECK(model.advance(100));Api api;CHECK(api.configure(credentials(),"00000000000000000000000000000000"));api.publish(model,100);
  const auto before=model.session().stats();const auto revision=model.snapshot().revision;
  const std::string wire="GET /api/v1/status HTTP/1.1\r\nHost: test\r\nAuthorization: Bearer 0123456789abcdef0123456789abcdef\r\n\r\n";
  Request r;r.reset(100);CHECK(r.append(wire.data(),wire.size()));Response response;
  api.respond(r,response,100);CHECK(response.status==200);
  api.respond(r,response,1100);CHECK(response.status==503);
  CHECK(model.session().stats().attempts==before.attempts);CHECK(model.snapshot().revision==revision);
  r.reset(0);CHECK(r.append(wire.data(),wire.size()));api.respond(r,response,2000);CHECK(response.status==408);
  std::array<char,2561> too_big{};r.reset(0);CHECK(!r.append(too_big.data(),too_big.size()));CHECK(r.size==0);
  CHECK(!model.advance(99));CHECK(!model.advance(UINT64_MAX/1000));
  Maintenance maintenance(model,api,100);
  CHECK(!maintenance.tick(2500));CHECK(model.now_ms()==200);
  r.reset(2500);CHECK(r.append(wire.data(),wire.size()));api.respond(r,response,2500);CHECK(response.status==503);
  for(unsigned i=0;i<22;++i) CHECK(!maintenance.tick(2500));
  CHECK(maintenance.tick(2500));CHECK(model.now_ms()==2500);
  api.respond(r,response,2500);CHECK(response.status==200);
  CHECK(!maintenance.tick(2499));api.respond(r,response,2500);CHECK(response.status==503);
  r.size=SIZE_MAX;CHECK(!r.append("x",1));api.respond(r,response,2500);CHECK(response.status==431);
}
}
int main(int argc,char** argv) {
  if(argc!=2) return 2;
  const std::string name=argv[1];
  if(name=="config") config();else if(name=="wifi") wifi();else if(name=="json") json();else if(name=="ownership") ownership();else return 2;
  std::cout<<name<<": passed\n";
}
