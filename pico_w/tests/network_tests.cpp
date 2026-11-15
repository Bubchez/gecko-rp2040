// SPDX-License-Identifier: MIT
// Test the actual firmware adapter against injected lwIP callbacks and time.
#include "network.hpp"
#include "lwip/tcp.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace gecko::runtime;
#define CHECK(c) do {if(!(c)){std::cerr<<__LINE__<<": " #c "\n";std::exit(1);}}while(false)
namespace {
std::uint64_t now=100000;
std::array<tcp_pcb,16> pcbs;
tcp_pcb* listener=nullptr;
bool pressure=false,close_pressure=false;
unsigned freed=0;
}
std::uint64_t time_us_64() {return now;}
tcp_pcb* tcp_new_ip_type(unsigned) {for(auto& p:pcbs) if(!p.active){p={};p.active=true;return &p;}return nullptr;}
err_t tcp_bind(tcp_pcb*,const void*,u16_t) {return ERR_OK;}
tcp_pcb* tcp_listen_with_backlog(tcp_pcb* p,unsigned) {listener=p;return p;}
void tcp_accept(tcp_pcb* p,err_t (*f)(void*,tcp_pcb*,err_t)) {p->accept=f;}
void tcp_arg(tcp_pcb* p,void* v) {p->arg=v;}
void tcp_recv(tcp_pcb* p,err_t (*f)(void*,tcp_pcb*,pbuf*,err_t)) {p->recv=f;}
void tcp_sent(tcp_pcb* p,err_t (*f)(void*,tcp_pcb*,u16_t)) {p->sent=f;}
void tcp_err(tcp_pcb* p,void (*f)(void*,err_t)) {p->error=f;}
void tcp_abort(tcp_pcb* p) {p->aborted=true;p->active=false;if(p->error) p->error(p->arg,ERR_ABRT);}
err_t tcp_close(tcp_pcb* p) {if(close_pressure) return ERR_MEM;p->closed=true;p->active=false;return ERR_OK;}
u16_t tcp_sndbuf(tcp_pcb* p) {return p->available;}
err_t tcp_write(tcp_pcb* p,const void* data,u16_t n,unsigned flags) {
  CHECK(flags==TCP_WRITE_FLAG_COPY);if(pressure) return ERR_MEM;CHECK(n<=p->available);
  p->wire.append(static_cast<const char*>(data),n);p->available=static_cast<u16_t>(p->available-n);return ERR_OK;
}
err_t tcp_output(tcp_pcb*) {return ERR_OK;}
void tcp_recved(tcp_pcb*,u16_t) {}
void pbuf_free(pbuf*) {++freed;}
namespace {
tcp_pcb* connect() {
  auto* p=tcp_new_ip_type(IPADDR_TYPE_V4);CHECK(p);CHECK(listener && listener->accept);
  const auto e=listener->accept(nullptr,p,ERR_OK);CHECK(e==ERR_OK || e==ERR_ABRT);return p;
}
void feed(tcp_pcb* p,std::string& text,std::size_t chunk) {
  for(std::size_t at=0;at<text.size();at+=chunk) {
    const auto n=static_cast<u16_t>(std::min(chunk,text.size()-at));pbuf buffer{text.data()+at,n,n,nullptr};
    CHECK(p->recv);const auto e=p->recv(p->arg,p,&buffer,ERR_OK);CHECK(e==ERR_OK || e==ERR_ABRT);
  }
}
void drain(tcp_pcb* p) {
  unsigned calls=0;
  while(p->active) {
    CHECK(++calls<100);CHECK(p->sent);const auto callback=p->sent;auto* arg=p->arg;
    p->available=128;const auto e=callback(arg,p,128);CHECK(e==ERR_OK || e==ERR_ABRT);
  }
  CHECK(p->arg==nullptr);CHECK(p->error==nullptr);
}
}
int main() {
  Api api;Credentials c;
  CHECK(parse_provision("PROVISION 74657374 70617373776f7264 0123456789abcdef0123456789abcdef net-1",c));
  CHECK(api.configure(c,"00000000000000000000000000000000"));
  CHECK(firmware::server_start(api));auto* first=connect();auto* second=connect();auto* third=connect();CHECK(third->aborted);
  std::string health="GET /api/v1/health HTTP/1.1\r\nHost: test\r\n\r\n";
  pressure=true;now+=1000;feed(first,health,1);CHECK(first->wire.empty());pressure=false;
  firmware::server_poll();CHECK(first->active);CHECK(!first->wire.empty());
  // Peer half-close must not discard a queued response.
  CHECK(first->recv(first->arg,first,nullptr,ERR_OK)==ERR_OK);drain(first);CHECK(first->closed);
  const auto split=first->wire.find("\r\n\r\n");CHECK(split!=std::string::npos);
  CHECK(first->wire.substr(0,13)=="HTTP/1.1 200 ");CHECK(first->wire.find("Cache-Control: no-store\r\n")!=std::string::npos);
  const auto cl=first->wire.find("Content-Length: ");CHECK(cl!=std::string::npos);
  const auto length=static_cast<std::size_t>(std::stoul(first->wire.substr(cl+16)));CHECK(length==first->wire.size()-split-4);
  CHECK(check_json(first->wire.substr(split+4)).valid);
  now+=2000000;firmware::server_poll();drain(second);CHECK(second->wire.find("408 Response")!=std::string::npos);
  auto* method=connect();std::string request="PUT /api/v1/status HTTP/1.1\r\nHost: test\r\nAuthorization: Bearer 0123456789abcdef0123456789abcdef\r\n\r\n";
  feed(method,request,20);drain(method);CHECK(method->wire.find("Allow: GET\r\n\r\n")!=std::string::npos);
  auto* stalled=connect();now+=1000;feed(stalled,health,health.size());firmware::server_poll();CHECK(stalled->active);now+=5000000;firmware::server_poll();CHECK(stalled->aborted);
  auto* remote=connect();CHECK(remote->error);remote->error(remote->arg,ERR_ABRT);remote->active=false;
  auto* replacement=connect();CHECK(!replacement->aborted);
  close_pressure=true;feed(replacement,health,20);drain(replacement);CHECK(replacement->aborted);close_pressure=false;
  auto* disconnected=connect();feed(disconnected,health,health.size());firmware::server_stop();CHECK(disconnected->aborted);CHECK(freed>0);
  CHECK(firmware::server_start(api));firmware::server_stop();
  std::cout<<"network callbacks, half-close, slots, backpressure, timeout, error and teardown: passed\n";
}
