// SPDX-License-Identifier: MIT
#include "network.hpp"
#include "lwip/tcp.h"
#include "pico/time.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace firmware {
namespace {
using namespace gecko::runtime;
struct Connection {
  tcp_pcb* pcb{nullptr};
  Request request{};
  Response response{};
  std::array<char,256> header{};
  std::size_t header_size{0},queued{0},acked{0};
  std::uint64_t replied_ms{0};
};
std::array<Connection,max_connections> slots;
tcp_pcb* listener=nullptr;
Api* api=nullptr;
std::uint64_t now_ms() noexcept {return time_us_64()/1000;}
void abort(Connection& c) noexcept {
  auto* pcb=c.pcb;c.pcb=nullptr;
  if(pcb) {tcp_arg(pcb,nullptr);tcp_err(pcb,nullptr);tcp_abort(pcb);}
}
bool send(Connection& c) noexcept {
  if(!c.pcb || c.response.status==0) return true;
  const auto total=c.header_size+(c.response.head?0:c.response.size);
  while(c.queued<total) {
    const bool header=c.queued<c.header_size;
    const auto offset=header?c.queued:c.queued-c.header_size;
    const auto available=header?c.header_size-offset:total-c.queued;
    const auto n=std::min(available,static_cast<std::size_t>(tcp_sndbuf(c.pcb)));
    if(n==0) break;
    const char* data=header?c.header.data()+offset:c.response.body.data()+offset;
    // COPY isolates lwIP lifetime from the reusable application buffers.
    const auto e=tcp_write(c.pcb,data,static_cast<u16_t>(n),TCP_WRITE_FLAG_COPY);
    if(e==ERR_MEM) break;
    if(e!=ERR_OK) {abort(c);return false;}
    c.queued+=n;
  }
  if(c.queued!=0) (void)tcp_output(c.pcb);
  if(c.queued==total && c.acked>=total) {
    // Detach callbacks before close, including when lwIP retains TIME_WAIT.
    auto* pcb=c.pcb;tcp_arg(pcb,nullptr);tcp_recv(pcb,nullptr);tcp_sent(pcb,nullptr);tcp_err(pcb,nullptr);
    const auto result=tcp_close(pcb);
    if(result!=ERR_OK) tcp_abort(pcb);
    c.pcb=nullptr;return result==ERR_OK;
  }
  return true;
}
void prepare(Connection& c) noexcept {
  const char* extra=c.response.authenticate?"WWW-Authenticate: Bearer\r\n":c.response.retry?"Retry-After: 1\r\n":"";
  const int n=std::snprintf(c.header.data(),c.header.size(),
    "HTTP/1.1 %u Response\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: %u\r\n%s%s%s\r\n",
    c.response.status,static_cast<unsigned>(c.response.size),extra,c.response.allow?"Allow: ":"",c.response.allow?c.response.allow:"");
  if(n<0 || static_cast<std::size_t>(n)>=c.header.size()) {abort(c);return;}
  c.header_size=static_cast<std::size_t>(n);
  // Allow's line needs its own CRLF in addition to the empty header terminator.
  if(c.response.allow) {
    if(c.header_size+2>=c.header.size()) {abort(c);return;}
    std::memcpy(c.header.data()+c.header_size,"\r\n",2);c.header_size+=2;
  }
  c.replied_ms=now_ms();(void)send(c);
}
void failed(void* arg,err_t) noexcept {if(arg) static_cast<Connection*>(arg)->pcb=nullptr;}
err_t sent(void* arg,tcp_pcb*,u16_t count) noexcept {
  auto& c=*static_cast<Connection*>(arg);c.acked+=count;return send(c)?ERR_OK:ERR_ABRT;
}
err_t received(void* arg,tcp_pcb*,pbuf* p,err_t e) noexcept {
  auto& c=*static_cast<Connection*>(arg);
  if(!p) {
    if(c.response.status!=0) return ERR_OK; // Peer half-close still permits the response.
    abort(c);return ERR_ABRT;
  }
  if(e!=ERR_OK) {pbuf_free(p);abort(c);return ERR_ABRT;}
  const auto total=p->tot_len;
  if(c.response.status==0) {
    for(auto* part=p;part && c.response.status==0;part=part->next) {
      const auto* bytes=static_cast<const std::uint8_t*>(part->payload);
      for(std::size_t at=0;at<part->len && c.response.status==0;) {
        const auto n=std::min(static_cast<std::size_t>(64),static_cast<std::size_t>(part->len)-at);
        if(!c.request.append(bytes+at,n)) {pbuf_free(p);abort(c);return ERR_ABRT;}
        api->respond(c.request,c.response,now_ms());at+=n;
      }
    }
  }
  tcp_recved(c.pcb,total);pbuf_free(p);
  if(c.response.status!=0 && c.header_size==0) prepare(c);
  return c.pcb?ERR_OK:ERR_ABRT;
}
err_t accepted(void*,tcp_pcb* pcb,err_t e) noexcept {
  if(e!=ERR_OK) {tcp_abort(pcb);return ERR_ABRT;}
  for(auto& c:slots) if(!c.pcb) {
    c.pcb=pcb;c.request.reset(now_ms());c.response.status=0;c.header_size=c.queued=c.acked=0;
    tcp_arg(pcb,&c);tcp_recv(pcb,received);tcp_sent(pcb,sent);tcp_err(pcb,failed);return ERR_OK;
  }
  tcp_abort(pcb);return ERR_ABRT;
}
}
bool server_start(gecko::runtime::Api& app) noexcept {
  if(listener) return true;
  api=&app;auto* pcb=tcp_new_ip_type(IPADDR_TYPE_V4);if(!pcb) return false;
  if(tcp_bind(pcb,IP_ANY_TYPE,8765)!=ERR_OK) {tcp_abort(pcb);return false;}
  listener=tcp_listen_with_backlog(pcb,1);
  if(!listener) {tcp_abort(pcb);return false;}
  tcp_accept(listener,accepted);return true;
}
void server_stop() noexcept {
  if(listener) {
    tcp_arg(listener,nullptr);tcp_accept(listener,nullptr);
    if(tcp_close(listener)!=ERR_OK) tcp_abort(listener);
    listener=nullptr;
  }
  for(auto& c:slots) abort(c);
}
void server_poll() noexcept {
  for(auto& c:slots) if(c.pcb) {
    // Callbacks may have accepted/replied after the main loop sampled its clock.
    const auto now=now_ms();
    if(c.response.status==0) {
      api->respond(c.request,c.response,now);
      if(c.response.status!=0) prepare(c);
    } else if(now-c.replied_ms>=5000) abort(c);
    else (void)send(c);
  }
}
} // namespace firmware
