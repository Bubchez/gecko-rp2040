// SPDX-License-Identifier: MIT
#include "gecko/runtime.hpp"
#include "network.hpp"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/rand.h"
#include "hardware/flash.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

#if !defined(GECKO_SIMULATION_ONLY) || GECKO_SIMULATION_ONLY != 1
#error "Only simulation firmware is permitted"
#endif
static_assert(!gecko::SimulatedTransport::physical_communication_enabled);
namespace {
using namespace gecko::runtime;
constexpr std::uint32_t config_offset=2*1024*1024-2*FLASH_SECTOR_SIZE;
static_assert(PICO_FLASH_SIZE_BYTES==2*1024*1024);
gecko::simulation::Model model;
Api api;
Credentials credentials;
WifiPolicy wifi;
std::array<ConfigRecord,2> records;
ConfigRecord pending;
std::uint32_t pending_offset=0;
std::array<char,513> serial_line{};
std::size_t serial_size=0;
bool serial_overflow=false,radio_initialized=false,connect_failed=false;
std::array<char,33> boot_id{};
std::uint64_t last_maintenance=0;

std::uint64_t now_ms() noexcept {return time_us_64()/1000;}
void load_records() noexcept {
  for(unsigned i=0;i<2;++i) std::memcpy(records[i].data(),reinterpret_cast<const void*>(XIP_BASE+config_offset+i*FLASH_SECTOR_SIZE),record_size);
}
void program_config(void*) {
  // Invoked only with all flash users/IRQs quiesced by SDK flash_safe_execute.
  flash_range_erase(pending_offset,FLASH_SECTOR_SIZE);
  flash_range_program(pending_offset,pending.data(),pending.size());
}
bool provision(std::string_view line) noexcept {
  Credentials candidate;
  if(!parse_provision(line,candidate)) return false;
  load_records();const auto newest=newest_record(records[0],records[1]);
  std::uint32_t sequence=0;Credentials old;
  if(newest>=0 && !decode_record(records[static_cast<std::size_t>(newest)],old,sequence)) return false;
  if(sequence==UINT32_MAX || !encode_record(candidate,sequence+1,pending)) return false;
  firmware::server_stop();
  if(radio_initialized) {cyw43_arch_deinit();radio_initialized=false;}
  // Reserve two sectors; never erase the last valid record before a replacement exists.
  pending_offset=config_offset+(newest==0?FLASH_SECTOR_SIZE:0);
  const auto result=flash_safe_execute(program_config,nullptr,1000);
  load_records();const auto selected=newest_record(records[0],records[1]);
  Credentials restored;std::uint32_t restored_sequence=0;
  const bool persisted=result==PICO_OK && selected>=0 && decode_record(records[static_cast<std::size_t>(selected)],restored,restored_sequence) && restored_sequence==sequence+1;
  // Reload only a CRC-validated record, even after a failed write. Never announce unpersisted credentials.
  if(selected>=0 && decode_record(records[static_cast<std::size_t>(selected)],restored,restored_sequence)) {
    credentials=restored;(void)api.configure(credentials,boot_id.data());
  } else {credentials={};api.unavailable();}
  wifi.configure(credentials.valid(),now_ms());
  // Maintenance catches up in bounded slices; no timestamp or observation is refreshed here.
  api.unavailable();
  pending.fill(0);candidate={};old={};restored={};records={};
  return persisted;
}
void serial_poll() noexcept {
  // Bounded per-loop work. No USB input echo and no secret-bearing diagnostics.
  for(unsigned n=0;n<64;++n) {
    const auto ch=getchar_timeout_us(0);if(ch==PICO_ERROR_TIMEOUT) break;
    if(ch=='\r') continue;
    if(ch=='\n') {
      const std::string_view line(serial_line.data(),serial_size);
      if(!serial_overflow && line=="STATUS") {
        const char* ip=radio_initialized && netif_default?ip4addr_ntoa(netif_ip4_addr(netif_default)):"unavailable";
        std::printf("SIMULATION_NO_I2C wifi=%s provisioned=%u ip=%s port=8765 gpio4_5=inactive\n",wifi_name(wifi.state()),credentials.valid()?1U:0U,ip);
      } else if(!serial_overflow && provision(line)) std::puts("CONFIG_SAVED");
      else std::puts("CONFIG_REJECTED");
      serial_line.fill(0);serial_size=0;serial_overflow=false;
    } else if(ch<32 || ch>126 || serial_size==serial_line.size()-1) serial_overflow=true;
    else if(!serial_overflow) serial_line[serial_size++]=static_cast<char>(ch);
  }
}
void network_poll() noexcept {
  int status=CYW43_LINK_DOWN;
  if(radio_initialized) {cyw43_arch_poll();status=cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA);}
  const auto action=wifi.tick(now_ms(),status==CYW43_LINK_UP,connect_failed || status<0);
  connect_failed=false;
  if(action==WifiAction::Disconnect) {
    firmware::server_stop();
    if(radio_initialized) (void)cyw43_wifi_leave(&cyw43_state,CYW43_ITF_STA);
  } else if(action==WifiAction::Connect) {
    if(!radio_initialized) {
      // Worldwide regulatory domain (conservative); no country guessed from web input.
      radio_initialized=cyw43_arch_init_with_country(CYW43_COUNTRY_WORLDWIDE)==0;
      if(radio_initialized) cyw43_arch_enable_sta_mode();
    }
    connect_failed=!radio_initialized || cyw43_arch_wifi_connect_async(credentials.ssid.data(),credentials.password.data(),CYW43_AUTH_WPA2_AES_PSK)!=0;
  }
  if(wifi.state()==WifiState::Online) {
    (void)firmware::server_start(api);firmware::server_poll();
  }
  api.diagnostics({wifi.state(),wifi.attempts(),wifi.reconnects()});
}
}
int main() {
  // Explicitly release the tentative Gecko pins. No peripheral function/pulls/output.
  for(unsigned pin:{4U,5U}) {gpio_init(pin);gpio_set_dir(pin,GPIO_IN);gpio_disable_pulls(pin);}
  stdio_init_all();
  for(unsigned i=0;i<4;++i) {
    const auto random=get_rand_32();
    for(unsigned j=0;j<8;++j) boot_id[i*8+j]="0123456789abcdef"[(random>>(j*4))&15U];
  }
  load_records();const auto selected=newest_record(records[0],records[1]);std::uint32_t sequence=0;
  if(selected>=0 && decode_record(records[static_cast<std::size_t>(selected)],credentials,sequence)) (void)api.configure(credentials,boot_id.data());
  records={};wifi.configure(credentials.valid(),now_ms());
  last_maintenance=now_ms();Maintenance maintenance(model,api,last_maintenance);
  std::puts("GECKO SIMULATION_NO_I2C: no physical transport or actuator execution");
  for(;;) {
    const auto now=now_ms();
    if(now-last_maintenance>=5) {
      (void)maintenance.tick(now);
      last_maintenance=now;
    }
    serial_poll();network_poll();tight_loop_contents();
  }
}
