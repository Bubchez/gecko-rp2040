// SPDX-License-Identifier: MIT
#include "gecko/runtime.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
namespace gecko::runtime {
namespace {
template<std::size_t N> std::string_view field(const std::array<char,N>& s) noexcept {
  const auto p=std::find(s.begin(),s.end(),'\0');
  if(p==s.end() || std::any_of(p,s.end(),[](char c){return c!='\0';})) return {};
  return {s.data(),static_cast<std::size_t>(p-s.begin())};
}
bool printable(std::string_view s,std::size_t lo,std::size_t hi) noexcept {
  return s.size()>=lo && s.size()<=hi && std::all_of(s.begin(),s.end(),[](char c){return c>=32 && c<=126;});
}
std::uint32_t crc(const std::uint8_t* bytes,std::size_t n) noexcept {
  std::uint32_t v=UINT32_MAX;
  for(std::size_t i=0;i<n;++i) {
    v^=bytes[i];
    for(unsigned b=0;b<8;++b) v=(v>>1)^((v&1U)?0xedb88320U:0U);
  }
  return ~v;
}
void put32(std::uint8_t* p,std::uint32_t v) noexcept { for(unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(v>>(i*8)); }
std::uint32_t get32(const std::uint8_t* p) noexcept {
  std::uint32_t v=0; for(unsigned i=0;i<4;++i) v|=static_cast<std::uint32_t>(p[i])<<(i*8); return v;
}
template<std::size_t N> bool text(std::string_view s,std::array<char,N>& out) noexcept {
  if(s.size()>=N) return false;
  out.fill(0); std::copy(s.begin(),s.end(),out.begin()); return true;
}
int hex(char c) noexcept {
  if(c>='0' && c<='9') return c-'0';
  if(c>='a' && c<='f') return c-'a'+10;
  if(c>='A' && c<='F') return c-'A'+10;
  return -1;
}
template<std::size_t N> bool unhex(std::string_view s,std::array<char,N>& out) noexcept {
  if(s.size()%2!=0 || s.size()/2>=N) return false;
  out.fill(0);
  for(std::size_t i=0;i<s.size()/2;++i) {
    const auto a=hex(s[2*i]),b=hex(s[2*i+1]); if(a<0 || b<0) return false;
    const auto value=a*16+b;if(value<32 || value>126) return false;
    out[i]=static_cast<char>(value);
  }
  return true;
}
void increment(std::uint32_t& value) noexcept { if(value!=UINT32_MAX) ++value; }
}
bool identifier(std::string_view s,std::size_t lo,std::size_t hi) noexcept {
  return s.size()>=lo && s.size()<=hi && std::all_of(s.begin(),s.end(),[](char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_';
  });
}
bool Credentials::valid() const noexcept {
  return printable(field(ssid),1,32) && printable(field(password),8,63) &&
    identifier(field(token),32,128) && identifier(field(device));
}
bool encode_record(const Credentials& c,std::uint32_t seq,ConfigRecord& r) noexcept {
  if(!c.valid() || seq==0) return false;
  r.fill(0); std::memcpy(r.data(),"GKSIM09",7); r[8]=1; put32(r.data()+9,seq);
  std::memcpy(r.data()+16,c.ssid.data(),33); std::memcpy(r.data()+49,c.password.data(),64);
  std::memcpy(r.data()+113,c.token.data(),129); std::memcpy(r.data()+242,c.device.data(),65);
  put32(r.data()+508,crc(r.data(),508)); return true;
}
bool decode_record(const ConfigRecord& r,Credentials& c,std::uint32_t& seq) noexcept {
  if(std::memcmp(r.data(),"GKSIM09",7)!=0 || r[7]!=0 || r[8]!=1 || get32(r.data()+508)!=crc(r.data(),508)) return false;
  if(std::any_of(r.begin()+13,r.begin()+16,[](std::uint8_t b){return b!=0;}) ||
     std::any_of(r.begin()+307,r.begin()+508,[](std::uint8_t b){return b!=0;})) return false;
  Credentials candidate;
  std::memcpy(candidate.ssid.data(),r.data()+16,33); std::memcpy(candidate.password.data(),r.data()+49,64);
  std::memcpy(candidate.token.data(),r.data()+113,129); std::memcpy(candidate.device.data(),r.data()+242,65);
  const auto n=get32(r.data()+9);
  if(!candidate.valid() || n==0) return false;
  c=candidate; seq=n; return true;
}
int newest_record(const ConfigRecord& a,const ConfigRecord& b) noexcept {
  Credentials c; std::uint32_t x=0,y=0;
  const auto good_a=decode_record(a,c,x),good_b=decode_record(b,c,y);
  if(!good_a && !good_b) return -1;
  return good_a && (!good_b || x>=y)?0:1;
}
bool parse_provision(std::string_view line,Credentials& out) noexcept {
  constexpr std::string_view prefix="PROVISION ";
  if(line.substr(0,prefix.size())!=prefix || line.size()>512) return false;
  line.remove_prefix(prefix.size()); std::array<std::string_view,4> parts{};
  for(std::size_t i=0;i<parts.size();++i) {
    const auto at=line.find(' ');
    if(i<3) { if(at==std::string_view::npos) return false; parts[i]=line.substr(0,at); line.remove_prefix(at+1); }
    else {if(at!=std::string_view::npos) return false;parts[i]=line;}
  }
  Credentials c;
  if(!unhex(parts[0],c.ssid) || !unhex(parts[1],c.password) || !text(parts[2],c.token) || !text(parts[3],c.device) || !c.valid()) return false;
  out=c; return true;
}
const char* wifi_name(WifiState state) noexcept {
  switch(state) {
    case WifiState::Unprovisioned:return "unprovisioned";
    case WifiState::Connecting:return "connecting";
    case WifiState::Online:return "online";
    case WifiState::Backoff:return "backoff";
  }
  return "unprovisioned";
}
void WifiPolicy::configure(bool valid,std::uint64_t now) noexcept {
  state_=valid?WifiState::Backoff:WifiState::Unprovisioned; deadline_=now; backoff_ms_=1000;
}
WifiAction WifiPolicy::tick(std::uint64_t now,bool up,bool failed) noexcept {
  if(state_==WifiState::Unprovisioned) return WifiAction::None;
  if(state_==WifiState::Backoff) {
    if(now<deadline_) return WifiAction::None;
    state_=WifiState::Connecting; deadline_=now+15000; increment(attempts_); return WifiAction::Connect;
  }
  if(state_==WifiState::Connecting && up) {
    state_=WifiState::Online; backoff_ms_=1000; return WifiAction::None;
  }
  if((state_==WifiState::Connecting && (failed || now>=deadline_)) || (state_==WifiState::Online && !up)) {
    if(state_==WifiState::Online) increment(reconnects_);
    state_=WifiState::Backoff; deadline_=now+backoff_ms_;
    backoff_ms_=std::min<std::uint32_t>(backoff_ms_*2,60000); return WifiAction::Disconnect;
  }
  return WifiAction::None;
}
} // namespace gecko::runtime
