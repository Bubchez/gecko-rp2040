// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/model.hpp"
#include <string_view>

namespace gecko::runtime {
constexpr std::size_t max_headers=2048, max_body=512, max_response=8192;
constexpr std::size_t max_connections=2;
bool identifier(std::string_view value, std::size_t min=1, std::size_t max=64) noexcept;
struct Credentials {
  std::array<char,33> ssid{};
  std::array<char,64> password{};
  std::array<char,129> token{};
  std::array<char,65> device{};
  bool valid() const noexcept;
};
// Fixed, explicitly encoded records; no compiler-layout dependence or secrets in UF2.
constexpr std::size_t record_size=512;
using ConfigRecord=std::array<std::uint8_t,record_size>;
bool encode_record(const Credentials&,std::uint32_t sequence,ConfigRecord&) noexcept;
bool decode_record(const ConfigRecord&,Credentials&,std::uint32_t& sequence) noexcept;
int newest_record(const ConfigRecord&,const ConfigRecord&) noexcept;
// PROVISION <ssid-hex> <password-hex> <token> <device-id>, no echo/logging.
bool parse_provision(std::string_view line,Credentials&) noexcept;

enum class WifiState { Unprovisioned, Connecting, Online, Backoff };
enum class WifiAction { None, Connect, Disconnect };
class WifiPolicy {
 public:
  void configure(bool valid,std::uint64_t now_ms) noexcept;
  WifiAction tick(std::uint64_t now_ms,bool link_up,bool failed) noexcept;
  bool reconnect(std::uint64_t now_ms) noexcept;
  WifiState state() const noexcept { return state_; }
  std::uint32_t attempts() const noexcept { return attempts_; }
  std::uint32_t reconnects() const noexcept { return reconnects_; }
 private:
  WifiState state_{WifiState::Unprovisioned};
  std::uint64_t deadline_{0};
  std::uint32_t attempts_{0},reconnects_{0},backoff_ms_{1000};
};
const char* wifi_name(WifiState) noexcept;

struct Request {
  std::array<char,max_headers+max_body+1> data{};
  std::size_t size{0};
  std::uint64_t started_ms{0};
  void reset(std::uint64_t now_ms) noexcept { size=0; started_ms=now_ms; }
  bool append(const void*,std::size_t) noexcept;
};
struct Response {
  std::array<char,max_response+1> body{};
  std::size_t size{0};
  unsigned status{0}; // zero means more request bytes required
  const char* allow{nullptr};
  bool authenticate{false},retry{false},head{false};
};
struct Diagnostics {
  WifiState wifi{WifiState::Unprovisioned};
  std::uint32_t wifi_attempts{0},wifi_reconnects{0};
};
// Single-owner immutable cache. HTTP has no reference to a mutable model/transport.
class Api {
 public:
  bool configure(const Credentials&,std::string_view boot_id) noexcept;
  void publish(const simulation::Model&,std::uint64_t wall_ms) noexcept;
  void unavailable() noexcept { cached_=false; }
  void diagnostics(Diagnostics d) noexcept { diagnostics_=d; }
  void respond(const Request&,Response&,std::uint64_t now_ms) noexcept;
 private:
  void document(std::string_view path,Response&,std::uint64_t now_ms) noexcept;
  Credentials credentials_{};
  std::array<char,33> boot_{};
  Snapshot snapshot_{};
  SessionStats session_stats_{};
  TransportStats transport_stats_{};
  Versions versions_{};
  SessionState state_{SessionState::Disabled};
  Error error_{Error::None};
  simulation::Scenario scenario_{simulation::Scenario::Unavailable};
  Diagnostics diagnostics_{};
  std::uint64_t published_ms_{0},model_ms_{0},refill_ms_{0};
  std::uint64_t stale_after_ms_{2000};
  std::uint32_t allowance_milli_{10000},unauthorized_{0},denied_{0},limited_{0};
  bool configured_{false},cached_{false};
};
// Exposed for malformed-input tests; object-only intent is denied by Api.
struct JsonCheck { bool valid{false},object{false}; std::array<char,65> request_id{}; };
JsonCheck check_json(std::string_view) noexcept;
// Separate maintenance owner: at most 100 model milliseconds per invocation.
// Large wall-time gaps keep the cache unavailable throughout bounded catch-up.
class Maintenance {
 public:
  Maintenance(simulation::Model& model,Api& api,std::uint64_t wall_ms) noexcept
    :model_(model),api_(api),wall_base_(wall_ms),model_base_(model.now_ms()),last_wall_(wall_ms) {}
  bool tick(std::uint64_t wall_ms) noexcept;
 private:
  simulation::Model& model_;Api& api_;
  std::uint64_t wall_base_,model_base_,last_wall_;
};
} // namespace gecko::runtime
