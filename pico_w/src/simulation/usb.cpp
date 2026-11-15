// SPDX-License-Identifier: MIT
#include "gecko/usb.hpp"
#include "gecko/identity.hpp"
#include <algorithm>
#include <cinttypes>
#include <cstdio>
namespace gecko::runtime {
std::size_t format_usb_info(const UsbInfo& s,std::array<char,513>& out) noexcept {
  out.fill(0);
  if(s.boot_id.size()!=32 || std::any_of(s.boot_id.begin(),s.boot_id.end(),[](char c){return !((c>='0' && c<='9') || (c>='a' && c<='f'));}) ||
     !identifier(s.device_id,s.provisioned?1:0,64) || s.ip.empty() || s.ip.size()>15) return 0;
  unsigned value=0,digits=0,dots=0;
  for(char c:s.ip) {
    if(c=='.') {if(digits==0 || ++dots>3) return 0;value=digits=0;}
    else if(c>='0' && c<='9') {value=value*10+static_cast<unsigned>(c-'0');if(++digits>3 || value>255) return 0;}
    else return 0;
  }
  if(dots!=3 || digits==0) return 0;
  const auto n=std::snprintf(out.data(),out.size(),
    "{\"schema_version\":1,\"firmware_version\":\"%s\",\"build_id\":\"%s\",\"boot_id\":\"%.*s\",\"device_id\":\"%.*s\",\"provisioned\":%s,\"wifi\":\"%s\",\"ip\":\"%.*s\",\"port\":8765,\"wifi_attempts\":%" PRIu32 ",\"wifi_reconnects\":%" PRIu32 ",\"mode\":\"simulation\",\"physical_i2c\":false,\"actuator_execution\":false,\"gpio\":\"inactive\"}\n",
    firmware_version,firmware_build_id,static_cast<int>(s.boot_id.size()),s.boot_id.data(),static_cast<int>(s.device_id.size()),s.device_id.empty()?"":s.device_id.data(),
    s.provisioned?"true":"false",wifi_name(s.wifi),static_cast<int>(s.ip.size()),s.ip.data(),s.attempts,s.reconnects);
  if(n<0 || static_cast<std::size_t>(n)>=out.size()) {out.fill(0);return 0;}
  return static_cast<std::size_t>(n);
}
}
