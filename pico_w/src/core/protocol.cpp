// SPDX-License-Identifier: MIT
#include "gecko/protocol.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

namespace gecko {
namespace {

int hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

Packet packet(std::initializer_list<std::uint8_t> bytes) noexcept {
  Packet out;
  if (bytes.size() <= out.data.size()) {
    std::copy(bytes.begin(), bytes.end(), out.data.begin());
    out.size = bytes.size();
  }
  return out;
}

void finish_xor(Packet& out) noexcept {
  const auto prefix = out.bytes().slice(0, out.size - 1);
  out.data[out.size - 1] = xor_bytes(prefix.value).value;
}

bool equal(Bytes a, Bytes b) noexcept {
  return a.size == b.size && std::equal(a.data, a.data + a.size, b.data);
}

bool common_header(Bytes b) noexcept {
  return b.data[0] == 0x17 && b.data[5] == 0x17 &&
         b.data[2] == 0 && b.data[3] == 0 && b.data[4] == 0 &&
         b.data[8] == 0 && b.data[10] == 0 && b.data[11] == 0;
}

bool route(Bytes b, std::uint8_t destination, std::uint8_t source,
           std::uint8_t flag = 0) noexcept {
  return b.data[1] == destination && b.data[6] == source && b.data[7] == flag;
}

bool command_header(Bytes b, std::uint8_t length) noexcept {
  return route(b, 0x0a, 0x09) && b.data[9] == 0 &&
         b.data[12] == length && b.data[13] == 0x46;
}

unsigned days_in_month(unsigned year, unsigned month) noexcept {
  constexpr std::array<unsigned, 12> lengths{31,28,31,30,31,30,31,31,30,31,30,31};
  if (month == 0 || month > lengths.size()) return 0;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return lengths[month - 1] + (month == 2 && leap ? 1U : 0U);
}

Result<VersionFile> version_file(Bytes b) noexcept {
  std::size_t n = 0;
  // 13 ASCII filename bytes plus a required terminator, all within the frame.
  while (n <= 13 && b.data[16 + n] != 0) ++n;
  if (n < 11 || n > 13) return Result<VersionFile>::failure(Error::InvalidValue);
  const std::string_view name(reinterpret_cast<const char*>(b.data + 16), n);
  if (name.substr(0, 5) != "inYT_" || (name[5] != 'C' && name[5] != 'S') ||
      name.substr(n - 4) != ".xml")
    return Result<VersionFile>::failure(Error::InvalidValue);
  unsigned version = 0;
  for (std::size_t i = 6; i < n - 4; ++i) {
    if (name[i] < '0' || name[i] > '9')
      return Result<VersionFile>::failure(Error::InvalidValue);
    version = version * 10U + static_cast<unsigned>(name[i] - '0');
  }
  if (version == 0 || version > 255 || name[6] == '0')
    return Result<VersionFile>::failure(Error::UnknownVersion);
  VersionFile out;
  std::copy(name.begin(), name.end(), out.name.begin());
  out.config = name[5] == 'C';
  out.version = static_cast<std::uint8_t>(version);
  return {out};
}

Result<Clock> clock_fields(Bytes b) noexcept {
  Clock out;
  out.hour = b.data[18]; out.minute = b.data[19]; out.second = b.data[20];
  if (out.hour > 23 || out.minute > 59 || out.second > 59)
    return Result<Clock>::failure(Error::InvalidValue);
  const bool no_date = b.data[14] == 255 && b.data[15] == 255 && b.data[16] == 255;
  if (!no_date) {
    Date date{static_cast<std::uint16_t>(2000U + b.data[14]), b.data[15], b.data[16]};
    if (b.data[14] > 99 || !valid_date(date)) return Result<Clock>::failure(Error::InvalidValue);
    out.date = date;
  }
  if (b.data[17] <= 6) out.weekday = b.data[17];
  else if (!no_date || b.data[17] != 255) return Result<Clock>::failure(Error::InvalidValue);
  return {out};
}

}  // namespace

Result<Bytes> Bytes::slice(std::size_t offset, std::size_t count) const noexcept {
  if (!valid()) return Result<Bytes>::failure(Error::InvalidView);
  if (offset > size || count > size - offset) return Result<Bytes>::failure(Error::Bounds);
  return {Bytes{data == nullptr ? nullptr : data + offset, count}};
}

Result<std::uint16_t> read_be16(Bytes input, std::size_t offset) noexcept {
  const auto field = input.slice(offset, 2);
  if (!field) return Result<std::uint16_t>::failure(field.error);
  return {static_cast<std::uint16_t>(static_cast<unsigned>(field.value.data[0]) * 256U +
                                     field.value.data[1])};
}

Result<std::size_t> copy_bytes(Bytes input, MutableBytes output) noexcept {
  if (!input.valid() || !output.valid()) return Result<std::size_t>::failure(Error::InvalidView);
  if (input.size > output.size) return Result<std::size_t>::failure(Error::Bounds);
  if (input.size != 0) std::memmove(output.data, input.data, input.size);
  return {input.size};
}

Result<std::uint8_t> xor_bytes(Bytes input) noexcept {
  if (!input.valid()) return Result<std::uint8_t>::failure(Error::InvalidView);
  unsigned accumulator = 0;
  for (std::size_t position = 0; position != input.size; ++position)
    accumulator ^= input.data[position];
  return {static_cast<std::uint8_t>(accumulator)};
}

Result<Packet> decode_hex(std::string_view text) noexcept {
  if (text.empty()) return Result<Packet>::failure(Error::Empty);
  if (text.size() > max_frame_bytes * 2) return Result<Packet>::failure(Error::TooLong);
  if (text.size() % 2 != 0) return Result<Packet>::failure(Error::InvalidLength);
  Packet out;
  out.size = text.size() / 2;
  for (std::size_t i = 0; i < out.size; ++i) {
    const int high = hex_digit(text[i * 2]);
    const int low = hex_digit(text[i * 2 + 1]);
    if (high < 0 || low < 0) return Result<Packet>::failure(Error::InvalidHex);
    out.data[i] = static_cast<std::uint8_t>(high * 16 + low);
  }
  return {out};
}

Result<Packet> decode_proxy_rx(std::string_view line) noexcept {
  if (line.size() > 3 + 3 + 1 + max_frame_bytes * 2)
    return Result<Packet>::failure(Error::TooLong);
  if (line.substr(0, 3) != "RX:") return Result<Packet>::failure(Error::InvalidHeader);
  const auto end = line.find(':', 3);
  if (end == std::string_view::npos || end == 3 || end > 6)
    return Result<Packet>::failure(Error::InvalidLength);
  std::size_t length = 0;
  for (std::size_t i = 3; i < end; ++i) {
    if (line[i] < '0' || line[i] > '9') return Result<Packet>::failure(Error::InvalidLength);
    const auto digit = static_cast<std::size_t>(line[i] - '0');
    if (length > (max_frame_bytes - digit) / 10)
      return Result<Packet>::failure(Error::TooLong);
    length = length * 10 + digit;
  }
  if (length == 0) return Result<Packet>::failure(Error::Empty);
  const auto hex = line.substr(end + 1);
  if (hex.size() != length * 2) return Result<Packet>::failure(Error::InvalidLength);
  return decode_hex(hex);
}

Result<Packet> encode_go() noexcept {
  return {packet({0x17,0,0,0,0,0x17,9,0,0,0,0,0,1,0x47,0x4f})};
}
Result<Packet> encode_ack() noexcept {
  return {packet({0x17,0x0a,0,0,0,0x17,9,0,0,0,0,0,1,0,2})};
}

Result<Packet> encode_temperature(double celsius, Versions versions) noexcept {
  if (!std::isfinite(celsius) || celsius < 26 || celsius > 40 ||
      std::trunc(celsius * 2) != celsius * 2)
    return Result<Packet>::failure(Error::InvalidValue);
  if (versions.config == 0 || versions.status == 0)
    return Result<Packet>::failure(Error::UnknownVersion);
  const auto raw = static_cast<std::uint16_t>(celsius * 18);
  Packet out = packet({0x17,0x0a,0,0,0,0x17,9,0,0,0,0,0,7,0x46,
    versions.config,versions.status,0,1,static_cast<std::uint8_t>(raw / 256),
    static_cast<std::uint8_t>(raw % 256),0});
  finish_xor(out);
  return {out};
}

Result<Packet> encode_switch(SwitchTarget target, bool on, Versions versions) noexcept {
  if (target != SwitchTarget::Light && target != SwitchTarget::Circulation)
    return Result<Packet>::failure(Error::InvalidValue);
  if (versions.config == 0 || versions.status == 0)
    return Result<Packet>::failure(Error::UnknownVersion);
  const std::uint8_t function = target == SwitchTarget::Light ? 0x33 : 0x6b;
  Packet out = packet({0x17,0x0a,0,0,0,0x17,9,0,0,0,0,0,6,0x46,
    versions.config,versions.status,1,function,static_cast<std::uint8_t>(on ? 1 : 0),0});
  finish_xor(out);
  return {out};
}

Result<Packet> encode_program(Program program) noexcept {
  const auto value = static_cast<std::uint8_t>(program);
  if (value > 4) return Result<Packet>::failure(Error::InvalidValue);
  Packet out = packet({0x17,0x0b,0,0,0,0x17,9,0,0,0,0,0,4,0x4e,3,0xd0,value,0});
  finish_xor(out);
  return {out};
}

Result<std::uint8_t> replace_pump_demand(std::uint8_t previous, unsigned pump_index,
                                         PumpDemand demand) noexcept {
  if (pump_index >= 4 || static_cast<unsigned>(demand) > 2)
    return Result<std::uint8_t>::failure(Error::InvalidValue);
  const unsigned shift = pump_index * 2;
  const unsigned mask = 3U << shift;
  return {static_cast<std::uint8_t>((static_cast<unsigned>(previous) & ~mask) |
                                   (static_cast<unsigned>(demand) << shift))};
}

bool valid_date(Date date) noexcept {
  return date.year >= 2000 && date.year <= 9999 && date.day >= 1 &&
         date.day <= days_in_month(date.year, date.month);
}

Result<Date> add_days(Date date, std::uint16_t days) noexcept {
  if (!valid_date(date)) return Result<Date>::failure(Error::InvalidValue);
  unsigned remaining = days;
  while (remaining != 0) {
    const unsigned available = days_in_month(date.year, date.month) - date.day;
    if (remaining <= available) {
      date.day = static_cast<std::uint8_t>(date.day + remaining);
      break;
    }
    remaining -= available + 1;
    date.day = 1;
    if (date.month == 12) {
      if (date.year == 9999) return Result<Date>::failure(Error::InvalidValue);
      ++date.year; date.month = 1;
    } else ++date.month;
  }
  return {date};
}

Result<std::array<Reminder, 4>> decode_reminder_entries(Bytes entries, DateOrder order) noexcept {
  using Output = Result<std::array<Reminder, 4>>;
  if (!entries.valid()) return Output::failure(Error::InvalidView);
  if (entries.size != 24) return Output::failure(Error::InvalidLength);
  if (order != DateOrder::YearMonthDay && order != DateOrder::DayMonthYear)
    return Output::failure(Error::InvalidValue);
  std::array<Reminder, 4> out{};
  std::array<bool, 4> seen{};
  for (std::size_t base = 0; base < entries.size; base += 6) {
    const unsigned id = entries.data[base];
    if (id == 0 || id > 4 || seen[id - 1]) return Output::failure(Error::InvalidValue);
    const bool ymd = order == DateOrder::YearMonthDay;
    const auto year = entries.data[base + (ymd ? 1U : 3U)];
    Date reset{static_cast<std::uint16_t>(2000U + year), entries.data[base + 2],
               entries.data[base + (ymd ? 3U : 1U)]};
    const unsigned interval = entries.data[base + 4] + 256U * entries.data[base + 5];
    if (year > 99 || interval == 0) return Output::failure(Error::InvalidValue);
    const auto due = add_days(reset, static_cast<std::uint16_t>(interval));
    if (!due) return Output::failure(due.error);
    out[id - 1] = {static_cast<std::uint8_t>(id), reset, static_cast<std::uint16_t>(interval), due.value};
    seen[id - 1] = true;
  }
  return {out};
}

Result<Frame> decode_frame(Bytes input, DecodeMode mode) noexcept {
  using Output = Result<Frame>;
  if (!input.valid()) return Output::failure(Error::InvalidView);
  if (input.size == 0) return Output::failure(Error::Empty);
  if (input.size > max_frame_bytes) return Output::failure(Error::TooLong);
  if (input.size < 15) return Output::failure(Error::InvalidLength);
  if (mode != DecodeMode::DocumentedOnly && mode != DecodeMode::SyntheticCandidate)
    return Output::failure(Error::InvalidValue);
  if (!common_header(input)) return Output::failure(Error::InvalidHeader);
  Frame out;
  out.packet.size = input.size;
  std::copy(input.data, input.data + input.size, out.packet.data.begin());
  if (equal(input, encode_go().value.bytes())) {
    out.kind = FrameKind::Go; out.integrity = Integrity::FixedLiteral; return {out};
  }
  if (equal(input, encode_ack().value.bytes())) {
    out.kind = FrameKind::Ack; out.integrity = Integrity::FixedLiteral; return {out};
  }

  if (input.size == 18 && route(input, 0x0b, 0x09) && input.data[7] == 0 &&
      input.data[9] == 0 && input.data[12] == 4 && input.data[13] == 0x4e &&
      input.data[14] == 3 && input.data[15] == 0xd0) {
    if (xor_bytes(input).value != 0) return Output::failure(Error::Checksum);
    if (input.data[16] > 4) return Output::failure(Error::InvalidValue);
    out.kind = FrameKind::Program; out.integrity = Integrity::XorChecked; return {out};
  }
  if ((input.size == 20 || input.size == 21) &&
      command_header(input, input.size == 20 ? 6 : 7)) {
    if (xor_bytes(input).value != 0) return Output::failure(Error::Checksum);
    if (input.data[14] == 0 || input.data[15] == 0) return Output::failure(Error::UnknownVersion);
    if (input.size == 21) {
      const auto raw = read_be16(input, 18).value;
      if (input.data[16] != 0 || input.data[17] != 1 || raw < 468 || raw > 720 || raw % 9 != 0)
        return Output::failure(Error::InvalidValue);
      out.kind = FrameKind::Temperature;
    } else {
      if (input.data[16] != 1 || (input.data[17] != 0x33 && input.data[17] != 0x6b) || input.data[18] > 1)
        return Output::failure(Error::InvalidValue);
      out.kind = FrameKind::Switch;
    }
    out.integrity = Integrity::XorChecked;
    return {out};
  }

  // Inbound routing below is a deliberately explicit SYNTHETIC envelope model.
  // No full hardware trace establishes these headers or multipart integrity.
  const bool inbound = route(input, 0x09, 0x0a) && input.data[9] == 0;
  const bool lo = input.size == 15 && inbound && input.data[12] == 1 &&
                  input.data[13] == 0x4c && input.data[14] == 0x4f;
  const bool xml = input.size == 33 && inbound;
  const bool clock = input.size == 22 && inbound && input.data[12] == 8 && input.data[13] == 0x4b;
  const bool fragment = input.size > 16 && route(input, 0x09, 0x0a, 1) &&
                        input.data[9] <= 1 && input.data[14] == 0x52 && input.data[15] == 0x51;
  const bool notification = input.size == 77 && route(input, 0x09, 0x0b) && input.data[9] == 0;
  if (!lo && !xml && !clock && !fragment && !notification) return Output::failure(Error::UnknownType);
  if (mode != DecodeMode::SyntheticCandidate) return Output::failure(Error::UnverifiedProtocol);
  out.synthetic_candidate = true;
  if (lo) out.kind = FrameKind::Lo;
  else if (xml) {
    const auto parsed = version_file(input);
    if (!parsed) return Output::failure(parsed.error);
    out.kind = FrameKind::VersionFile; out.file = parsed.value;
  } else if (clock) {
    if (xor_bytes(input).value != 0) return Output::failure(Error::Checksum);
    const auto parsed = clock_fields(input);
    if (!parsed) return Output::failure(parsed.error);
    out.kind = FrameKind::Clock; out.clock = parsed.value; out.integrity = Integrity::XorChecked;
  } else if (fragment) {
    out.kind = FrameKind::Fragment; out.more = input.data[9] == 1;
  } else {
    const auto entries = input.slice(16, 24);
    const auto reminders = decode_reminder_entries(entries.value);
    if (!reminders) return Output::failure(reminders.error);
    out.kind = FrameKind::Notification;
  }
  return {out};
}

Result<Status> decode_status(Bytes payload, CandidateProfile profile, DecodeMode mode) noexcept {
  using Output = Result<Status>;
  if (!payload.valid()) return Output::failure(Error::InvalidView);
  if (mode != DecodeMode::DocumentedOnly && mode != DecodeMode::SyntheticCandidate)
    return Output::failure(Error::InvalidValue);
  if (profile != CandidateProfile::InYtV51) return Output::failure(Error::UnsupportedProfile);
  if (mode != DecodeMode::SyntheticCandidate) return Output::failure(Error::UnverifiedProtocol);
  // Explicit candidate layout, never auto-detected from message size.
  if (payload.size != 162) return Output::failure(Error::InvalidLength);
  if (payload.data[1] != 0) return Output::failure(Error::InvalidHeader);
  if (payload.data[53] > 1) return Output::failure(Error::InvalidValue);
  Status out;
  out.target.raw = read_be16(payload, 21).value;
  out.actual.raw = read_be16(payload, 23).value;
  if (out.target.raw >= 8 * 18 && out.target.raw <= 42 * 18)
    out.target.celsius = static_cast<double>(out.target.raw) / 18;
  // Synthetic plausibility limit only; hardware sensor limits are unknown.
  if (out.actual.raw > 0 && out.actual.raw <= 100 * 18)
    out.actual.celsius = static_cast<double>(out.actual.raw) / 18;
  out.user_demand = payload.data[5];
  out.heater_on = (payload.data[6] & 0x20U) != 0;
  out.circulation_on = (payload.data[6] & 0x04U) != 0;
  out.light_demand_on = payload.data[53] == 1;
  if (payload.data[3] <= 3) out.standby = payload.data[3] == 3;
  for (unsigned i = 0; i < 4; ++i)
    out.pumps[i] = static_cast<PumpState>((payload.data[7] >> (i * 2)) & 3U);
  return {out};
}

}  // namespace gecko
