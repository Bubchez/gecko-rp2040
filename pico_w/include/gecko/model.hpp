// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/session.hpp"
namespace gecko::simulation {
enum class Scenario { Healthy, Unavailable, Stale, Recovery, InvalidTemperature };
const char* scenario_name(Scenario value) noexcept;
const char* state_name(SessionState value) noexcept;
const char* error_name(Error value) noexcept;
class Model {
 public:
  explicit Model(Scenario scenario=Scenario::Healthy) noexcept;
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;
  bool advance(std::uint64_t target_ms) noexcept;
  std::uint64_t now_ms() const noexcept { return now_ms_; }
  Error error() const noexcept { return error_; }
  Scenario scenario() const noexcept { return scenario_; }
  Snapshot snapshot() const noexcept { return session_.snapshot(now_ms_*1000); }
  const ReadOnlySession& session() const noexcept { return session_; }
  const SimulatedTransport& transport() const noexcept { return transport_; }
  const SessionConfig& config() const noexcept { return config_; }
 private:
  void require(Error e) noexcept { if(error_==Error::None) error_=e; }
  void write(Packet p,std::uint64_t at,Error outcome=Error::None) noexcept;
  void read(Packet p,std::uint64_t at,std::uint64_t epoch,std::optional<std::uint32_t> index={}) noexcept;
  void status(std::uint64_t at,std::uint64_t epoch) noexcept;
  void handshake(std::uint64_t base,std::uint64_t epoch) noexcept;
  void failed_go(std::uint64_t at) noexcept;
  Scenario scenario_; SessionConfig config_; SimulatedTransport transport_;
  ReadOnlySession session_;
  std::uint64_t now_ms_{0},next_go_{0}; bool planned_{false},injected_{false};
  Error error_{Error::None};
};
} // namespace gecko::simulation
