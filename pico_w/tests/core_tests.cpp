// SPDX-License-Identifier: MIT
#include "gecko/assembly.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace gecko;
namespace {
std::size_t checks = 0;
constexpr auto candidate = DecodeMode::SyntheticCandidate;
constexpr Versions versions{82, 81};

void check(bool condition, const char* expression, int line) {
  ++checks;
  if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)
#define ERROR(expression, expected) CHECK((expression).error == (expected))

Packet hex(std::string_view text) {
  const auto decoded = decode_hex(text);
  CHECK(decoded);
  return decoded.value;
}
bool same(const Packet& a, const Packet& b) {
  return a.size == b.size && std::equal(a.data.begin(), a.data.begin() + static_cast<std::ptrdiff_t>(a.size), b.data.begin());
}
void seal(Packet& p) {
  unsigned checksum = 0;
  for (std::size_t i = 0; i + 1 < p.size; ++i) checksum ^= p.data[i];
  p.data[p.size - 1] = static_cast<std::uint8_t>(checksum);
}
Packet inbound(std::size_t size, std::uint8_t source = 0x0a, std::uint8_t flag = 0) {
  CHECK(size >= 16 && size <= max_frame_bytes);
  Packet p;
  p.size = size;
  p.data[0] = p.data[5] = 0x17;
  p.data[1] = 9; p.data[6] = source; p.data[7] = flag;
  return p;
}
Packet filename(std::string_view name) {
  CHECK(name.size() < 16);
  auto p = inbound(33);
  std::copy(name.begin(), name.end(), p.data.begin() + 16);
  return p;
}
Packet clock_packet() {
  auto p = inbound(22);
  p.data[12] = 8; p.data[13] = 0x4b;
  p.data[14] = 24; p.data[15] = 2; p.data[16] = 29; p.data[17] = 4;
  p.data[18] = 23; p.data[19] = 59; p.data[20] = 58;
  seal(p);
  return p;
}
Packet fragment(Bytes payload, bool more) {
  CHECK(payload.valid() && payload.size > 0 && payload.size <= 112);
  auto p = inbound(16 + payload.size, 0x0a, 1);
  p.data[9] = more ? 1 : 0;
  p.data[14] = 0x52; p.data[15] = 0x51;
  std::copy(payload.data, payload.data + payload.size, p.data.begin() + 16);
  return p;
}
FragmentContext context(std::uint32_t index, std::uint64_t at = 0,
                        std::uint64_t session = 7, std::uint64_t loss = 0) {
  return {at, session, loss, index};
}

void bytes_test() {
  std::array<std::uint8_t, 4> data{0x12,0x34,0x56,0x78};
  Bytes b{data.data(), data.size()};
  CHECK(read_be16(b, 0).value == 0x1234);
  CHECK(read_be16(b, 2).value == 0x5678);
  for (auto offset : {std::size_t{3}, std::size_t{4}, std::numeric_limits<std::size_t>::max()})
    ERROR(read_be16(b, offset), Error::Bounds);
  ERROR(b.slice(1, std::numeric_limits<std::size_t>::max()), Error::Bounds);
  ERROR(read_be16({nullptr,1}, 0), Error::InvalidView);
  ERROR(xor_bytes({nullptr,1}), Error::InvalidView);
  CHECK(xor_bytes({}).value == 0);
  CHECK(Bytes{}.slice(0,0));
  CHECK(b.slice(4,0));
  CHECK(xor_bytes(b).value == 8);
  std::array<std::uint8_t, 4> out{9,9,9,9};
  ERROR(copy_bytes(b, {out.data(),3}), Error::Bounds);
  CHECK(out[0] == 9 && out[3] == 9);
  ERROR(copy_bytes(b, {nullptr,4}), Error::InvalidView);
  CHECK(copy_bytes({},{}).value == 0);
  CHECK(copy_bytes(b, {out.data(),4}).value == 4);
  CHECK(out == data);
  CHECK(copy_bytes({data.data(),3}, {data.data()+1,3}));
  CHECK((data == std::array<std::uint8_t,4>{0x12,0x12,0x34,0x56}));
  Packet forged; forged.size = 129;
  ERROR(decode_frame(forged.bytes()), Error::InvalidView);
  Assembly forged_assembly; forged_assembly.size = 513;
  CHECK(!forged_assembly.bytes().valid());
}

void hex_test() {
  CHECK(hex("00aAfF").data[1] == 0xaa);
  for (auto input : {"G0", "0G", "++", "..", "00\n", "00 11", "000"}) {
    const auto result = decode_hex(input);
    CHECK(!result && result.value.size == 0);
  }
  ERROR(decode_hex(""), Error::Empty);
  CHECK(decode_hex(std::string(256,'f')).value.size == 128);
  ERROR(decode_hex(std::string(258,'f')), Error::TooLong);
  const std::string embedded("00\0F",4);
  ERROR(decode_hex(embedded), Error::InvalidHex);
  for (unsigned c = 0; c < 256; ++c) {
    const std::string input{static_cast<char>(c), '0'};
    const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    CHECK(static_cast<bool>(decode_hex(input)) == is_hex);
  }
}

void proxy_test() {
  CHECK(decode_proxy_rx("RX:2:00ff").value.data[1] == 255);
  CHECK(decode_proxy_rx("RX:128:" + std::string(256,'A')).value.size == 128);
  for (auto line : {"RX:18:00", "RX:200:00", "RX:-1:00", "RX:+1:00", "RX:1x:00",
                   "RX::00", "RX:1", "RX:1:", "RX:1:0000", "RX:1:GG", "RX:0:",
                   "RX:99999999999999999999:00", "TX:00", "RX:1:00\n", "RX:1:00:", "RX:1: 00"}) {
    const auto result = decode_proxy_rx(line);
    CHECK(!result && result.value.size == 0);
  }
  CHECK(!decode_proxy_rx("RX:200:" + std::string(400,'0')));
  for (std::size_t length = 1; length <= 128; ++length) {
    const auto prefix = "RX:" + std::to_string(length) + ":";
    CHECK(decode_proxy_rx(prefix + std::string(length*2,'0')).value.size == length);
    CHECK(!decode_proxy_rx(prefix + std::string(length*2-1,'0')));
    CHECK(!decode_proxy_rx(prefix + std::string(length*2+1,'0')));
  }
}

void temperature_test() {
  struct Golden { double c; std::uint16_t raw; const char* hex; };
  // Complete vectors computed from the documented temperature layout (E03).
  constexpr Golden examples[]{
    {26,468,"170A0000001709000000000007465251000101D495"},
    {36.5,657,"170A000000170900000000000746525100010291D3"},
    {37,666,"170A00000017090000000000074652510001029AD8"},
    {40,720,"170A0000001709000000000007465251000102D092"}
  };
  for (const auto& example : examples) {
    const auto result = encode_temperature(example.c, versions);
    CHECK(result && same(result.value, hex(example.hex)));
    CHECK(read_be16(result.value.bytes(),18).value == example.raw);
    CHECK(xor_bytes(result.value.bytes()).value == 0);
    const auto frame = decode_frame(result.value.bytes());
    CHECK(frame && frame.value.kind == FrameKind::Temperature);
    CHECK(frame.value.integrity == Integrity::XorChecked && !frame.value.synthetic_candidate);
  }
  for (int half = 52; half <= 80; ++half) {
    const auto p = encode_temperature(static_cast<double>(half)/2, {255,1});
    CHECK(p && decode_frame(p.value.bytes()));
    CHECK(read_be16(p.value.bytes(),18).value == half*9);
  }
  for (auto c : {25.999, 40.001, -0.0, -1.0, 36.6, 37.0000001,
                 std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max()})
    ERROR(encode_temperature(c, versions), Error::InvalidValue);
  ERROR(encode_temperature(37,{0,81}), Error::UnknownVersion);
  ERROR(encode_temperature(37,{82,0}), Error::UnknownVersion);
  auto p = encode_temperature(37,versions).value;
  p.data[19] = 0x9b; seal(p);
  ERROR(decode_frame(p.bytes()), Error::InvalidValue);
}

void switches_test() {
  CHECK(same(encode_switch(SwitchTarget::Light,true,versions).value,
             hex("170A000000170900000000000646525101330173")));
  CHECK(same(encode_switch(SwitchTarget::Circulation,false,versions).value,
             hex("170A0000001709000000000006465251016B002A")));
  for (auto target : {SwitchTarget::Light, SwitchTarget::Circulation})
    for (bool on : {false,true}) {
      const auto p = encode_switch(target,on,versions);
      CHECK(p && decode_frame(p.value.bytes()).value.kind == FrameKind::Switch);
      CHECK(p.value.data[18] == static_cast<unsigned>(on));
    }
  ERROR(encode_switch(static_cast<SwitchTarget>(99),false,versions), Error::InvalidValue);
  ERROR(encode_switch(SwitchTarget::Light,false,{}), Error::UnknownVersion);
  auto p = encode_switch(SwitchTarget::Light,false,versions).value;
  p.data[18] = 2; seal(p);
  ERROR(decode_frame(p.bytes()), Error::InvalidValue);
  // Exhaustively check preserving the other six demand bits, independent of speed semantics.
  for (unsigned old = 0; old < 256; ++old)
    for (unsigned pump = 0; pump < 4; ++pump)
      for (unsigned demand = 0; demand < 3; ++demand) {
        const auto result = replace_pump_demand(static_cast<std::uint8_t>(old),pump,static_cast<PumpDemand>(demand));
        CHECK(result);
        const unsigned mask = 3U << (pump*2);
        CHECK((result.value & ~mask) == (old & ~mask));
        CHECK(((result.value & mask) >> (pump*2)) == demand);
      }
  ERROR(replace_pump_demand(0,4,PumpDemand::Off), Error::InvalidValue);
  ERROR(replace_pump_demand(0,0,static_cast<PumpDemand>(3)), Error::InvalidValue);
}

void program_test() {
  constexpr const char* golden[]{
    "170B00000017090000000000044E03D0009B", "170B00000017090000000000044E03D0019A",
    "170B00000017090000000000044E03D00299", "170B00000017090000000000044E03D00398",
    "170B00000017090000000000044E03D0049F"};
  for (unsigned i = 0; i < 5; ++i) {
    const auto p = encode_program(static_cast<Program>(i));
    CHECK(p && same(p.value,hex(golden[i])));
    CHECK(decode_frame(p.value.bytes()).value.kind == FrameKind::Program);
  }
  ERROR(encode_program(static_cast<Program>(5)), Error::InvalidValue);
  auto p = encode_program(Program::Standard).value;
  p.data[16] = 255; seal(p);
  ERROR(decode_frame(p.bytes()), Error::InvalidValue);
  p = {}; p.size = 18; p.data[16] = 4; p.data[17] = 255;
  CHECK(!decode_frame(p.bytes(),candidate));
}

void fixed_frames_test() {
  const auto go = encode_go().value;
  const auto ack = encode_ack().value;
  CHECK(same(go,hex("17000000001709000000000001474F")));
  CHECK(same(ack,hex("170A00000017090000000000010002")));
  CHECK(decode_frame(go.bytes()).value.kind == FrameKind::Go);
  CHECK(decode_frame(ack.bytes()).value.kind == FrameKind::Ack);
  CHECK(decode_frame(go.bytes()).value.integrity == Integrity::FixedLiteral);
  // The documented GO literal happens to XOR to zero. That is not identification.
  CHECK(xor_bytes(go.bytes()).value == 0);
  auto xor_collision = go;
  xor_collision.data[8] ^= 1; xor_collision.data[9] ^= 1;
  CHECK(xor_bytes(xor_collision.bytes()).value == 0);
  CHECK(!decode_frame(xor_collision.bytes()));
  auto lo = inbound(16); lo.size = 15; lo.data[12]=1; lo.data[13]=0x4c; lo.data[14]=0x4f;
  ERROR(decode_frame(lo.bytes()), Error::UnverifiedProtocol);
  CHECK(decode_frame(lo.bytes(),candidate).value.kind == FrameKind::Lo);
  CHECK(decode_frame(lo.bytes(),candidate).value.integrity == Integrity::Unverified);
}

void frame_rejection_test() {
  std::vector<std::uint8_t> blank(129,0);
  for (std::size_t size = 0; size <= blank.size(); ++size)
    CHECK(!decode_frame({blank.data(),size},candidate));
  ERROR(decode_frame({nullptr,1}), Error::InvalidView);
  for (const auto& original : {encode_go().value, encode_ack().value,
      encode_temperature(37,versions).value, encode_program(Program::Standard).value,
      encode_switch(SwitchTarget::Light,true,versions).value}) {
    for (std::size_t size = 0; size < original.size; ++size)
      CHECK(!decode_frame({original.data.data(),size},candidate));
    auto trailing = original; trailing.data[trailing.size++] = 0;
    CHECK(!decode_frame(trailing.bytes()));
    for (std::size_t i = 0; i < original.size; ++i)
      for (unsigned bit = 0; bit < 8; ++bit) {
        auto corrupt = original; corrupt.data[i] ^= static_cast<std::uint8_t>(1U << bit);
        CHECK(!decode_frame(corrupt.bytes()));
      }
  }
  auto p = encode_temperature(37,versions).value;
  p.data[1] = 9; seal(p);
  CHECK(!decode_frame(p.bytes()));
  p = encode_temperature(37,versions).value;
  p.data[14] = 0; seal(p);
  ERROR(decode_frame(p.bytes()), Error::UnknownVersion);
  ERROR(decode_frame(encode_go().value.bytes(),static_cast<DecodeMode>(99)), Error::InvalidValue);
}

void filename_test() {
  for (auto name : {"inYT_C82.xml","inYT_S81.xml","inYT_C1.xml","inYT_S255.xml"}) {
    const auto p = filename(name);
    ERROR(decode_frame(p.bytes()), Error::UnverifiedProtocol);
    const auto f = decode_frame(p.bytes(),candidate);
    CHECK(f && f.value.kind == FrameKind::VersionFile);
    CHECK(std::string(f.value.file.name.data()) == name);
    CHECK(f.value.integrity == Integrity::Unverified && f.value.synthetic_candidate);
  }
  for (auto name : {"inYT_C0.xml","inYT_C01.xml","inYT_C256.xml","inYT_C9x.xml",
                   "inYT_X82.xml","inYT_C82.zip","foo_C82.xml","inYT_C.xml"})
    CHECK(!decode_frame(filename(name).bytes(),candidate));
  auto no_terminator = inbound(33);
  std::fill(no_terminator.data.begin()+16,no_terminator.data.begin()+33,'A');
  CHECK(!decode_frame(no_terminator.bytes(),candidate));
  auto arbitrary = inbound(33);
  CHECK(!decode_frame(arbitrary.bytes(),candidate));
  // Candidate parsing must not represent an unverified XML trailer as checked.
  auto unverified = filename("inYT_C82.xml");
  unverified.data[32] = 0xff;
  const auto modeled = decode_frame(unverified.bytes(),candidate);
  CHECK(modeled && modeled.value.integrity == Integrity::Unverified);
}

void clock_test() {
  auto p = clock_packet();
  ERROR(decode_frame(p.bytes()), Error::UnverifiedProtocol);
  auto result = decode_frame(p.bytes(),candidate);
  CHECK(result && result.value.clock.date->year == 2024);
  CHECK(result.value.clock.date->day == 29 && result.value.clock.hour == 23);
  CHECK(result.value.integrity == Integrity::XorChecked && result.value.synthetic_candidate);
  for (const auto& change : std::vector<std::pair<std::size_t,std::uint8_t>>{
      {14,23},{15,0},{15,13},{16,30},{17,7},{18,24},{19,60},{20,60}}) {
    auto bad = p; bad.data[change.first] = change.second; seal(bad);
    CHECK(!decode_frame(bad.bytes(),candidate));
  }
  p.data[14] = p.data[15] = p.data[16] = p.data[17] = 255; seal(p);
  result = decode_frame(p.bytes(),candidate);
  CHECK(result && !result.value.clock.date && !result.value.clock.weekday);
  p.data[15] = 1; seal(p);
  CHECK(!decode_frame(p.bytes(),candidate));
  p = clock_packet(); p.data[21] ^= 1;
  ERROR(decode_frame(p.bytes(),candidate), Error::Checksum);
  CHECK(valid_date({2000,2,29}));
  CHECK(!valid_date({2100,2,29}));
  CHECK(!valid_date({2026,4,31}));
  CHECK(!valid_date({1999,12,31}));
  CHECK(add_days({2024,2,28},2).value.month == 3);
  CHECK(add_days({2024,2,28},2).value.day == 1);
  CHECK(add_days({2026,12,31},1).value.year == 2027);
  CHECK(add_days({2099,12,31},60).value.month == 3);
  CHECK(add_days({2099,12,31},60).value.day == 1);
  ERROR(add_days({9999,12,31},1), Error::InvalidValue);
  const auto large = add_days({2000,2,28},65535);
  CHECK(large && large.value.year==2179 && large.value.month==8 && large.value.day==3);
  const auto leap = add_days({2024,2,29},366);
  CHECK(leap && leap.value.year==2025 && leap.value.month==3 && leap.value.day==1);
}

std::array<std::uint8_t,24> reminder_entries() {
  std::array<std::uint8_t,24> out{};
  for (std::size_t i=0; i<4; ++i) {
    out[i*6] = static_cast<std::uint8_t>(i+1);
    out[i*6+1]=26; out[i*6+2]=11; out[i*6+3]=8; out[i*6+4]=30;
  }
  return out;
}
void reminders_test() {
  auto entries = reminder_entries();
  auto result = decode_reminder_entries({entries.data(),entries.size()});
  CHECK(result && result.value[0].due.year == 2026);
  CHECK(result.value[0].due.month == 12 && result.value[0].due.day == 8);
  for (std::size_t size=0; size<24; ++size)
    CHECK(!decode_reminder_entries({entries.data(),size}));
  ERROR(decode_reminder_entries({nullptr,24}), Error::InvalidView);
  for (const auto& change : std::vector<std::pair<std::size_t,std::uint8_t>>{
      {0,0},{0,5},{6,1},{1,100},{2,0},{2,13},{3,31},{4,0}}) {
    auto bad=entries; bad[change.first]=change.second;
    CHECK(!decode_reminder_entries({bad.data(),bad.size()}));
  }
  entries[4]=0x34; entries[5]=0x12;
  CHECK(decode_reminder_entries({entries.data(),24}).value[0].interval_days == 0x1234);
  entries[4]=entries[5]=255;
  result=decode_reminder_entries({entries.data(),24});
  CHECK(result && valid_date(result.value[0].due) && result.value[0].due.year > 2099);
  CHECK(result.value[0].due.year==2206 && result.value[0].due.month==4 && result.value[0].due.day==14);
  entries=reminder_entries();
  for (std::size_t i=0;i<4;++i) std::swap(entries[i*6+1],entries[i*6+3]);
  CHECK(decode_reminder_entries({entries.data(),24},DateOrder::DayMonthYear).value[0].due.day == 8);
  CHECK(!decode_reminder_entries({entries.data(),24},static_cast<DateOrder>(99)));
  auto p=inbound(77,0x0b);
  entries=reminder_entries(); std::copy(entries.begin(),entries.end(),p.data.begin()+16);
  ERROR(decode_frame(p.bytes()),Error::UnverifiedProtocol);
  CHECK(decode_frame(p.bytes(),candidate).value.kind==FrameKind::Notification);
}

std::vector<std::uint8_t> status_payload() {
  std::vector<std::uint8_t> p(162,0);
  p[3]=3; p[5]=0xe4; p[6]=0x24; p[7]=0xe4;
  p[21]=2; p[22]=0x9a; p[23]=2; p[24]=0x91; p[53]=1;
  return p;
}
void status_test() {
  auto p=status_payload();
  ERROR(decode_status({p.data(),p.size()},CandidateProfile::InYtV51),Error::UnverifiedProtocol);
  ERROR(decode_status({p.data(),p.size()},CandidateProfile::Unknown,candidate),Error::UnsupportedProfile);
  ERROR(decode_status({p.data(),p.size()},CandidateProfile::InYtV50,candidate),Error::UnsupportedProfile);
  ERROR(decode_status({p.data(),p.size()},CandidateProfile::InYtV51,static_cast<DecodeMode>(99)),Error::InvalidValue);
  auto s=decode_status({p.data(),p.size()},CandidateProfile::InYtV51,candidate);
  CHECK(s && !s.value.hardware_verified);
  CHECK(s.value.target.raw==666 && s.value.target.celsius==37);
  CHECK(s.value.actual.celsius==36.5 && s.value.standby==true);
  CHECK(s.value.heater_on && s.value.circulation_on && s.value.light_demand_on);
  CHECK(s.value.user_demand==0xe4);
  CHECK((s.value.pumps==std::array<PumpState,4>{PumpState::Off,PumpState::High,PumpState::Low,PumpState::Unknown}));
  for (std::size_t size=0;size<162;++size)
    CHECK(!decode_status({p.data(),size},CandidateProfile::InYtV51,candidate));
  p[21]=p[22]=0;
  s=decode_status({p.data(),p.size()},CandidateProfile::InYtV51,candidate);
  CHECK(s && !s.value.target.celsius && s.value.actual.celsius==36.5);
  p[21]=p[22]=255; p[23]=p[24]=0; p[3]=255;
  s=decode_status({p.data(),p.size()},CandidateProfile::InYtV51,candidate);
  CHECK(s && !s.value.target.celsius && !s.value.actual.celsius && !s.value.standby);
  p[53]=2;
  CHECK(!decode_status({p.data(),p.size()},CandidateProfile::InYtV51,candidate));
  p=status_payload(); p[1]=1;
  CHECK(!decode_status({p.data(),p.size()},CandidateProfile::InYtV51,candidate));
}

void assembly_test() {
  const auto payload=status_payload();
  Assembler a;
  std::size_t offset=0;
  for (std::uint32_t i=0;i<3;++i) {
    const std::size_t n=i==2 ? 38 : 62;
    const auto f=fragment({payload.data()+offset,n},i!=2);
    const auto r=a.push(f.bytes(),context(i,i*1000),candidate);
    CHECK(r.error==Error::None);
    if (i!=2) CHECK(r.state==AssemblyState::Incomplete && r.message.size==0);
    else {
      CHECK(r.state==AssemblyState::Complete && r.message.size==162);
      CHECK(r.message.first_at_us==0 && r.message.complete_at_us==2000);
      CHECK(std::equal(payload.begin(),payload.end(),r.message.data.begin()));
      CHECK(!r.message.hardware_verified && a.buffered()==0);
      CHECK(decode_status(r.message.bytes(),CandidateProfile::InYtV51,candidate));
    }
    offset+=n;
  }
  const std::uint8_t zero=0;
  const auto first=fragment({&zero,1},true);
  const auto last=fragment({&zero,1},false);
  ERROR(a.push(first.bytes(),context(0)),Error::UnverifiedProtocol);
  ERROR(a.push(first.bytes(),{0,7,0,{}},candidate),Error::UnverifiedProtocol);
  for (auto bad_context : {context(0,1),context(2,1),context(1,250000),context(1,1,8),context(1,1,7,1)}) {
    CHECK(a.push(first.bytes(),context(0),candidate).state==AssemblyState::Incomplete);
    const auto r=a.push(last.bytes(),bad_context,candidate);
    CHECK(r.state==AssemblyState::Rejected && r.message.size==0 && a.buffered()==0);
  }
  CHECK(a.push(first.bytes(),context(0,100),candidate).state==AssemblyState::Incomplete);
  ERROR(a.push(last.bytes(),context(1,99),candidate),Error::ClockRegression);
  CHECK(a.buffered()==0);
  CHECK(a.push(first.bytes(),context(0),candidate).state==AssemblyState::Incomplete);
  auto corrupt=last; corrupt.data[15]^=1;
  CHECK(a.push(corrupt.bytes(),context(1,1),candidate).state==AssemblyState::Rejected);
  CHECK(a.buffered()==0);
  CHECK(a.push(first.bytes(),context(0),candidate).state==AssemblyState::Incomplete);
  CHECK(a.push(encode_go().value.bytes(),context(1,1),candidate).state==AssemblyState::Rejected);
  CHECK(a.buffered()==0);
  CHECK(a.push(first.bytes(),context(0),candidate).state==AssemblyState::Incomplete);
  CHECK(a.expire(249999)==Error::None);
  CHECK(a.expire(250000)==Error::Timeout && a.buffered()==0);
  ERROR(a.push(last.bytes(),context(1,250001),candidate),Error::Sequence);
  CHECK(a.push(last.bytes(),context(0,250001),candidate).state==AssemblyState::Complete);
  Assembler total({100,150});
  CHECK(total.push(first.bytes(),context(0,0),candidate).state==AssemblyState::Incomplete);
  CHECK(total.push(first.bytes(),context(1,90),candidate).state==AssemblyState::Incomplete);
  CHECK(total.expire(150)==Error::Timeout);
  Assembler invalid({0,1});
  ERROR(invalid.push(first.bytes(),context(0),candidate),Error::InvalidValue);
}

void boundaries_test() {
  for (const auto total : {120U,162U,170U,390U,405U,512U,513U}) {
    Assembler a;
    std::vector<std::uint8_t> bytes(total,0xa5);
    std::size_t offset=0;
    std::uint32_t index=0;
    while (offset<bytes.size()) {
      const auto n=std::min<std::size_t>(112,bytes.size()-offset);
      const bool more=offset+n<bytes.size();
      const auto p=fragment({bytes.data()+offset,n},more);
      const auto result=a.push(p.bytes(),context(index,index*100),candidate);
      if (total==513 && !more) {
        CHECK(result.error==Error::TooLong && a.buffered()==0 && result.message.size==0);
      } else if (!more) {
        CHECK(result.state==AssemblyState::Complete && result.message.size==total);
        CHECK(std::equal(bytes.begin(),bytes.end(),result.message.data.begin()));
      } else CHECK(result.state==AssemblyState::Incomplete);
      offset+=n; ++index;
    }
  }
  // Maximum fragment count; capacity and counters cannot wrap.
  Assembler many({10000,10000});
  const std::uint8_t zero=0;
  for (std::uint32_t i=0;i<512;++i) {
    const auto p=fragment({&zero,1},i!=511);
    const auto r=many.push(p.bytes(),context(i,i),candidate);
    CHECK(r.state==(i==511 ? AssemblyState::Complete : AssemblyState::Incomplete));
  }
  Assembler edge({100,100});
  const auto p=fragment({&zero,1},true);
  const auto max=std::numeric_limits<std::uint64_t>::max();
  CHECK(edge.push(p.bytes(),context(0,max-20),candidate).state==AssemblyState::Incomplete);
  CHECK(edge.expire(max)==Error::None);
  CHECK(edge.expire(0)==Error::ClockRegression);
}

void mutations_test() {
  // Deterministic adversarial inputs; NOT a proof of protocol coverage or hardware behavior.
  std::uint32_t random=0x4745434b;
  const auto next=[&random]() {
    random^=random<<13; random^=random>>17; random^=random<<5; return random;
  };
  Assembler a;
  for (std::uint32_t i=0;i<50000;++i) {
    const auto size=static_cast<std::size_t>(next()%130U);
    std::vector<std::uint8_t> bytes(size);
    for (auto& byte : bytes) byte=static_cast<std::uint8_t>(next());
    const Bytes view{bytes.data(),bytes.size()};
    const auto frame=decode_frame(view,candidate);
    if (frame) CHECK(frame.value.packet.size==size && size<=128);
    const auto r=a.push(view,context(i%5,i),candidate);
    CHECK(r.state==AssemblyState::Rejected && a.buffered()==0);
    const auto p=decode_status(view,CandidateProfile::InYtV51,candidate);
    CHECK(!p);  // Mutation lengths never reach the candidate's exact 162 bytes.
  }
  // Structured mutations reach meaningful status fields and fragment boundaries.
  auto status=status_payload();
  for (std::uint32_t i=0;i<10000;++i) {
    auto payload=status;
    payload[next()%162U]=static_cast<std::uint8_t>(next());
    const auto r=decode_status({payload.data(),payload.size()},CandidateProfile::InYtV51,candidate);
    if (r) {
      CHECK(!r.value.hardware_verified);
      if (r.value.target.celsius) CHECK(std::isfinite(*r.value.target.celsius));
      if (r.value.actual.celsius) CHECK(std::isfinite(*r.value.actual.celsius));
    }
    ERROR(decode_status({payload.data(),payload.size()},CandidateProfile::InYtV51),Error::UnverifiedProtocol);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc!=2 && argc!=4) return 2;
  const std::string name=argv[1];
  try {
    if(name=="fixture" && argc==4) {
      const auto p=hex(argv[2]);
      const auto decoded=decode_frame(p.bytes());
      CHECK(decoded && !decoded.value.synthetic_candidate);
      const std::string kind=argv[3];
      if(kind=="go") CHECK(decoded.value.kind==FrameKind::Go);
      else if(kind=="ack") CHECK(decoded.value.kind==FrameKind::Ack);
      else if(kind=="temperature") CHECK(decoded.value.kind==FrameKind::Temperature);
      else if(kind=="program") CHECK(decoded.value.kind==FrameKind::Program);
      else if(kind=="switch") CHECK(decoded.value.kind==FrameKind::Switch);
      else throw std::runtime_error("unknown fixture kind");
    }
    else if(argc!=2) return 2;
    else if(name=="bytes") bytes_test();
    else if(name=="hex") hex_test();
    else if(name=="proxy") proxy_test();
    else if(name=="temperature") temperature_test();
    else if(name=="switches") switches_test();
    else if(name=="program") program_test();
    else if(name=="fixed_frames") fixed_frames_test();
    else if(name=="frame_rejection") frame_rejection_test();
    else if(name=="filename") filename_test();
    else if(name=="clock") clock_test();
    else if(name=="reminders") reminders_test();
    else if(name=="status") status_test();
    else if(name=="assembly") assembly_test();
    else if(name=="boundaries") boundaries_test();
    else if(name=="mutations") mutations_test();
    else return 2;
    std::cout<<"PASS "<<name<<": "<<checks<<" checks\n";
  } catch(const std::exception& e) {
    std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; return 1;
  }
}
