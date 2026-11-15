// SPDX-License-Identifier: MIT
// Minimal independently authored test double, NOT an lwIP implementation.
#pragma once
#include <cstdint>
#include <string>
using err_t=int;
using u16_t=std::uint16_t;
constexpr err_t ERR_OK=0,ERR_MEM=-1,ERR_ABRT=-13;
constexpr unsigned IPADDR_TYPE_V4=0,TCP_WRITE_FLAG_COPY=1;
#define IP_ANY_TYPE nullptr
struct pbuf {void* payload;u16_t len,tot_len;pbuf* next;};
struct tcp_pcb {
  void* arg{nullptr};
  err_t (*recv)(void*,tcp_pcb*,pbuf*,err_t){nullptr};
  err_t (*sent)(void*,tcp_pcb*,u16_t){nullptr};
  err_t (*accept)(void*,tcp_pcb*,err_t){nullptr};
  void (*error)(void*,err_t){nullptr};
  bool active{false},aborted{false},closed{false};
  u16_t available{128};
  std::string wire{};
};
tcp_pcb* tcp_new_ip_type(unsigned);
err_t tcp_bind(tcp_pcb*,const void*,u16_t);
tcp_pcb* tcp_listen_with_backlog(tcp_pcb*,unsigned);
void tcp_accept(tcp_pcb*,err_t (*)(void*,tcp_pcb*,err_t));
void tcp_arg(tcp_pcb*,void*);
void tcp_recv(tcp_pcb*,err_t (*)(void*,tcp_pcb*,pbuf*,err_t));
void tcp_sent(tcp_pcb*,err_t (*)(void*,tcp_pcb*,u16_t));
void tcp_err(tcp_pcb*,void (*)(void*,err_t));
void tcp_abort(tcp_pcb*);
err_t tcp_close(tcp_pcb*);
u16_t tcp_sndbuf(tcp_pcb*);
err_t tcp_write(tcp_pcb*,const void*,u16_t,unsigned);
err_t tcp_output(tcp_pcb*);
void tcp_recved(tcp_pcb*,u16_t);
void pbuf_free(pbuf*);
