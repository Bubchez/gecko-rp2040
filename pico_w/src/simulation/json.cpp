// SPDX-License-Identifier: MIT
#include "gecko/runtime.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gecko::runtime {
namespace {
// Iterative JSON recognizer. A 512-byte message cannot exceed these capacities.
// Key equality compares decoded Unicode scalar values (including escaped keys).
class Json {
 public:
  explicit Json(std::string_view s) noexcept : s_(s) {}
  JsonCheck run() noexcept;
 private:
  struct Span { std::uint16_t a{0},b{0}; };
  struct Key { Span span{}; std::uint16_t object{0}; };
  struct Context { std::uint8_t state{0}; std::uint16_t id{0}; bool request_id{false}; };
  bool string(Span&) noexcept;
  bool scalar() noexcept;
  bool value() noexcept;
  void space() noexcept { while(pos_<s_.size() && (s_[pos_]==' ' || s_[pos_]=='\n' || s_[pos_]=='\r' || s_[pos_]=='\t')) ++pos_; }
  bool equal(Span,Span) const noexcept;
  bool key_is(Span span,std::string_view key) const noexcept;
  static bool point(std::string_view,std::size_t&,std::uint32_t&) noexcept;
  std::string_view s_;
  std::size_t pos_{0},depth_{0},keys_{0};
  std::uint16_t next_object_{0};
  std::array<Context,256> stack_{};
  std::array<Key,128> names_{};
  JsonCheck result_{};
};
int hex(char c) noexcept {
  if(c>='0' && c<='9') return c-'0';
  if(c>='a' && c<='f') return c-'a'+10;
  if(c>='A' && c<='F') return c-'A'+10;
  return -1;
}
bool Json::point(std::string_view s,std::size_t& at,std::uint32_t& cp) noexcept {
  if(at>=s.size()) return false;
  auto byte=[&](std::size_t i){return static_cast<std::uint8_t>(s[i]);};
  const auto b=byte(at++);
  if(b=='\\') {
    if(at>=s.size()) return false;
    const auto e=s[at++];
    switch(e) {
      case '"':case '\\':case '/':cp=static_cast<std::uint8_t>(e);return true;
      case 'b':cp=8;return true; case 'f':cp=12;return true;
      case 'n':cp=10;return true; case 'r':cp=13;return true;case 't':cp=9;return true;
      default:break;
    }
    if(e!='u') return false;
    auto unit=[&](std::uint32_t& v) {
      if(s.size()-at<4) return false;
      v=0;
      for(unsigned i=0;i<4;++i) {const auto h=hex(s[at++]); if(h<0) return false;v=v*16+static_cast<unsigned>(h);} return true;
    };
    if(!unit(cp)) return false;
    if(cp>=0xd800 && cp<=0xdbff) {
      if(s.size()-at<6 || s[at++]!='\\' || s[at++]!='u') return false;
      std::uint32_t low=0; if(!unit(low) || low<0xdc00 || low>0xdfff) return false;
      cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
    } else if(cp>=0xdc00 && cp<=0xdfff) return false;
    return true;
  }
  if(b<0x20 || b=='"') return false;
  if(b<0x80) {cp=b;return true;}
  unsigned rest=0; std::uint32_t minimum=0;
  if(b>=0xc2 && b<=0xdf) {rest=1;minimum=0x80;cp=b&0x1fU;}
  else if(b>=0xe0 && b<=0xef) {rest=2;minimum=0x800;cp=b&0x0fU;}
  else if(b>=0xf0 && b<=0xf4) {rest=3;minimum=0x10000;cp=b&7U;}
  else return false;
  if(s.size()-at<rest) return false;
  while(rest--!=0) {const auto c=byte(at++);if((c&0xc0U)!=0x80) return false;cp=(cp<<6)|(c&0x3fU);}
  return cp>=minimum && cp<=0x10ffff && !(cp>=0xd800 && cp<=0xdfff);
}
bool Json::string(Span& span) noexcept {
  if(pos_>=s_.size() || s_[pos_++]!='"') return false;
  span.a=static_cast<std::uint16_t>(pos_);
  while(pos_<s_.size() && s_[pos_]!='"') {std::uint32_t cp=0;if(!point(s_,pos_,cp)) return false;}
  span.b=static_cast<std::uint16_t>(pos_);
  if(pos_==s_.size()) return false;
  ++pos_; return true;
}
bool Json::equal(Span a,Span b) const noexcept {
  auto x=static_cast<std::size_t>(a.a),y=static_cast<std::size_t>(b.a);
  while(x<a.b && y<b.b) {
    std::uint32_t u=0,v=0;
    if(!point(s_,x,u) || !point(s_,y,v) || u!=v) return false;
  }
  return x==a.b && y==b.b;
}
bool Json::key_is(Span span,std::string_view key) const noexcept {
  auto at=static_cast<std::size_t>(span.a);std::size_t n=0;
  while(at<span.b) {std::uint32_t c=0;if(!point(s_,at,c) || n==key.size() || c!=static_cast<unsigned char>(key[n++])) return false;}
  return n==key.size();
}
bool Json::scalar() noexcept {
  if(pos_>=s_.size()) return false;
  if(s_[pos_]=='"') {
    Span span; if(!string(span)) return false;
    if(depth_==1 && stack_[0].request_id) {
      auto at=static_cast<std::size_t>(span.a);std::size_t n=0; bool ok=true;
      while(at<span.b) {std::uint32_t cp=0;if(!point(s_,at,cp) || cp>127 || n==64) {ok=false;break;} result_.request_id[n++]=static_cast<char>(cp);}
      if(!ok || !identifier({result_.request_id.data(),n})) result_.request_id.fill(0);
    }
    return true;
  }
  for(const auto word:{std::string_view("null"),std::string_view("true"),std::string_view("false")}) {
    if(s_.substr(pos_,word.size())==word) {pos_+=word.size();return true;}
  }
  const auto start=pos_;
  if(s_[pos_]=='-') ++pos_;
  if(pos_>=s_.size()) return false;
  auto digit=[&](){return pos_<s_.size() && s_[pos_]>='0' && s_[pos_]<='9';};
  if(s_[pos_]=='0') ++pos_;
  else {if(!digit() || s_[pos_]=='0') return false;
    while(digit()) ++pos_;}
  bool floating=false;
  if(pos_<s_.size() && s_[pos_]=='.') {floating=true;++pos_;if(!digit()) return false;
    while(digit()) ++pos_;}
  if(pos_<s_.size() && (s_[pos_]=='e' || s_[pos_]=='E')) {
    floating=true;++pos_;if(pos_<s_.size() && (s_[pos_]=='+' || s_[pos_]=='-')) ++pos_;
    if(!digit()) return false;
    while(digit()) ++pos_;
  }
  if(floating) {
    std::array<char,513> number{};const auto n=pos_-start;
    std::memcpy(number.data(),s_.data()+start,n);
    if(!std::isfinite(std::strtod(number.data(),nullptr))) return false;
  }
  return pos_>start;
}
bool Json::value() noexcept {
  space();if(pos_>=s_.size()) return false;
  if(s_[pos_]=='{' || s_[pos_]=='[') {
    if(depth_==stack_.size()) return false;
    const auto object=s_[pos_++]=='{';
    stack_[depth_++]={static_cast<std::uint8_t>(object?0:4),++next_object_,false};return true;
  }
  return scalar();
}
JsonCheck Json::run() noexcept {
  if(s_.size()>512) return {};
  space();result_.object=pos_<s_.size() && s_[pos_]=='{';
  if(!value()) return {};
  while(depth_!=0) {
    space();if(pos_>=s_.size()) return {};
    auto& c=stack_[depth_-1]; const auto ch=s_[pos_];
    // object: 0 initial key/end,1 key,2 colon,3 value,6 comma/end
    // array: 4 initial value/end,5 value,7 comma/end
    if(c.state==0 || c.state==1) {
      if(c.state==0 && ch=='}') {++pos_;--depth_;continue;}
      Span name;if(!string(name) || keys_==names_.size()) return {};
      for(std::size_t i=0;i<keys_;++i) if(names_[i].object==c.id && equal(name,names_[i].span)) return {};
      names_[keys_++]={name,c.id};c.request_id=key_is(name,"request_id");c.state=2;
    } else if(c.state==2) {if(ch!=':') return {};++pos_;c.state=3;}
    else if(c.state==3 || c.state==4 || c.state==5) {
      if(c.state==4 && ch==']') {++pos_;--depth_;continue;}
      c.state=static_cast<std::uint8_t>(c.state==3?6:7);if(!value()) return {};
    } else {
      const bool object=c.state==6;
      if(ch==(object?'}':']')) {++pos_;--depth_;}
      else if(ch==',') {++pos_;c.state=static_cast<std::uint8_t>(object?1:5);}
      else return {};
    }
  }
  space();result_.valid=pos_==s_.size();if(!result_.valid) return {};
  return result_;
}
}
JsonCheck check_json(std::string_view s) noexcept { return Json(s).run(); }
} // namespace gecko::runtime
