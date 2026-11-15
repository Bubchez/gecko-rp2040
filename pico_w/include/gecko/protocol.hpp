// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gecko {

inline constexpr std::size_t max_frame_bytes = 128;
inline constexpr std::size_t max_assembly_bytes = 512;

enum class Error {
  None, InvalidView, Bounds, Empty, TooLong, InvalidHex, InvalidLength,
  InvalidHeader, UnknownType, Checksum, InvalidValue, UnknownVersion,
  UnverifiedProtocol, UnsupportedProfile, Timeout, Sequence, SessionChanged,
  TransportLoss, ClockRegression, ReadOnly, Disabled, Busy, UnexpectedTx,
  ScenarioCapacity, AddressNack, DataNack, ArbitrationLost, ShortRead,
  RestoreFailed, StuckSda, StuckScl, RxCollision, HandshakeOrder,
  StaleState, RetryExhausted, ClockRange
};

template <typename T> struct [[nodiscard]] Result {
  T value{};
  Error error{Error::None};
  explicit operator bool() const noexcept { return error == Error::None; }
  static Result failure(Error why) noexcept { return {T{}, why}; }
};

struct Bytes {
  const std::uint8_t* data{nullptr};
  std::size_t size{0};
  bool valid() const noexcept { return data != nullptr || size == 0; }
  Result<Bytes> slice(std::size_t offset, std::size_t count) const noexcept;
};

struct MutableBytes {
  std::uint8_t* data{nullptr};
  std::size_t size{0};
  bool valid() const noexcept { return data != nullptr || size == 0; }
};

struct Packet {
  std::array<std::uint8_t, max_frame_bytes> data{};
  std::size_t size{0};
  Bytes bytes() const noexcept {
    return {size <= data.size() ? data.data() : nullptr, size};
  }
};

Result<std::uint16_t> read_be16(Bytes input, std::size_t offset) noexcept;
Result<std::size_t> copy_bytes(Bytes input, MutableBytes output) noexcept;
Result<std::uint8_t> xor_bytes(Bytes input) noexcept;
Result<Packet> decode_hex(std::string_view compact_hex) noexcept;
// Recording importer only: exact RX:<decimal length>:<compact hex>, no newline.
Result<Packet> decode_proxy_rx(std::string_view line) noexcept;

struct Versions { std::uint8_t config{0}; std::uint8_t status{0}; };
enum class SwitchTarget { Light, Circulation };
enum class Program : std::uint8_t { Away, Standard, Energy, SuperEnergy, Weekend };
enum class PumpDemand : std::uint8_t { Off, Low, High };

Result<Packet> encode_go() noexcept;
Result<Packet> encode_ack() noexcept;
Result<Packet> encode_temperature(double celsius, Versions versions) noexcept;
Result<Packet> encode_switch(SwitchTarget target, bool on, Versions versions) noexcept;
Result<Packet> encode_program(Program program) noexcept;
// Pure two-bit packing exercise. No claim that every pump/speed is supported.
Result<std::uint8_t> replace_pump_demand(std::uint8_t previous, unsigned pump_index,
                                         PumpDemand demand) noexcept;

struct Date { std::uint16_t year{0}; std::uint8_t month{0}; std::uint8_t day{0}; };
bool valid_date(Date date) noexcept;
Result<Date> add_days(Date date, std::uint16_t days) noexcept;
struct Clock {
  std::optional<Date> date{};
  std::optional<std::uint8_t> weekday{};
  std::uint8_t hour{0}, minute{0}, second{0};
};
enum class DateOrder { YearMonthDay, DayMonthYear };
struct Reminder { std::uint8_t id{0}; Date reset{}; std::uint16_t interval_days{0}; Date due{}; };
Result<std::array<Reminder, 4>> decode_reminder_entries(
    Bytes entries, DateOrder order = DateOrder::YearMonthDay) noexcept;

enum class DecodeMode { DocumentedOnly, SyntheticCandidate };
enum class Integrity { FixedLiteral, XorChecked, Unverified };
enum class FrameKind { Go, Ack, Lo, VersionFile, Clock, Program, Temperature, Switch, Fragment, Notification };
struct VersionFile {
  std::array<char, 14> name{};
  bool config{false};
  std::uint8_t version{0};
};
struct Frame {
  Packet packet{};
  FrameKind kind{FrameKind::Go};
  Integrity integrity{Integrity::Unverified};
  bool synthetic_candidate{false};
  bool more{false};
  VersionFile file{};
  Clock clock{};
};
// No decoded frame authorizes I/O. Candidate-only families reject by default.
Result<Frame> decode_frame(Bytes input, DecodeMode mode = DecodeMode::DocumentedOnly) noexcept;

enum class CandidateProfile { Unknown, InYtV50, InYtV51 };
enum class PumpState { Off, High, Low, Unknown };
struct Temperature {
  std::uint16_t raw{0};
  std::optional<double> celsius{};
};
struct Status {
  // Always false in this implementation: there are no hardware captures.
  bool hardware_verified{false};
  Temperature target{}, actual{};
  bool heater_on{false}, circulation_on{false}, light_demand_on{false};
  std::uint8_t user_demand{0};
  std::optional<bool> standby{};
  std::array<PumpState, 4> pumps{};
};
Result<Status> decode_status(Bytes payload, CandidateProfile profile,
                            DecodeMode mode = DecodeMode::DocumentedOnly) noexcept;

}  // namespace gecko
