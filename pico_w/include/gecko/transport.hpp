// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/assembly.hpp"

namespace gecko {

template <typename T, std::size_t Capacity> class FixedQueue {
 public:
  bool push(const T& value) noexcept {
    if (size_ == Capacity) return false;
    data_[(head_ + size_) % Capacity] = value; ++size_; return true;
  }
  std::optional<T> pop() noexcept {
    if (size_ == 0) return {};
    T out = data_[head_]; head_ = (head_ + 1) % Capacity; --size_; return out;
  }
  const T* front() const noexcept { return size_ == 0 ? nullptr : &data_[head_]; }
  std::size_t size() const noexcept { return size_; }
  void clear() noexcept { head_ = size_ = 0; }
 private:
  static_assert(Capacity > 0, "queue capacity must be positive");
  std::array<T, Capacity> data_{};
  std::size_t head_{0}, size_{0};
};

enum class TransportMode { Inactive, Passive, SyntheticReadOnly };
enum class Role { Inactive, Observing, Listen, Receive, Reply, MasterWrite, MasterRead, Stop, Restore, Released };
enum class Phase { Write, Read, Stop, Restore };

struct TxRequest {
  Packet write{};
  std::uint64_t id{0}, session{0}, deadline_us{0};
  std::uint8_t address{0x17}, read_length{2};
  bool repeated_start{true}, final_stop{true};
};
struct TxResult {
  std::uint64_t id{0}, session{0};
  Error error{Error::None};
  Phase phase{Phase::Restore};
  std::size_t written{0};
  std::array<std::uint8_t, 2> read{};
  std::uint8_t read_count{0};
  bool restored{false};
  bool write_count_known{true};
  bool possibly_applied() const noexcept { return !write_count_known || written != 0; }
};

// Plan timing and faults are simulator choices, never measured bus behavior.
struct TxPlan {
  Packet expected{};
  std::uint64_t earliest_us{0}, latest_us{UINT64_MAX};
  std::array<std::uint64_t, 4> duration_us{10,10,10,10};
  Error outcome{Error::None};
  Phase fault_phase{Phase::Write};
  std::size_t partial_write{0};
  std::uint8_t read_count{2};
  std::array<std::uint8_t, 2> read{};
};
struct RxPlan {
  Packet packet{};
  FragmentContext context{};
  std::uint8_t address{0x17};
  bool repeated_read{true};
};

enum class EventKind { Rx, Tx, Fault };
struct TransportEvent {
  EventKind kind{EventKind::Fault};
  std::uint64_t at_us{0}, loss_generation{0};
  RxPlan rx{};
  TxResult tx{};
  Error error{Error::None};
};
struct RoleTrace { std::uint64_t at_us{0}, tx_id{0}; Role role{Role::Inactive}; };
struct TransportStats {
  std::uint64_t submitted{0}, completed{0}, denied{0}, dropped_rx{0}, queue_overflows{0};
  std::uint64_t queue_dropped_events{0};
  std::uint64_t checkpoints{0}, retired_trace_events{0};
};

// There is deliberately no physical adapter, SDK dependency or write-enable mode.
class SimulatedTransport {
 public:
  SimulatedTransport() = default;
  SimulatedTransport(const SimulatedTransport&) = delete;
  SimulatedTransport& operator=(const SimulatedTransport&) = delete;
  SimulatedTransport(SimulatedTransport&&) = delete;
  SimulatedTransport& operator=(SimulatedTransport&&) = delete;
  static constexpr bool physical_communication_enabled = false;
  static constexpr std::size_t script_capacity = 64, event_capacity = 32, trace_capacity = 512;
  Error add_tx(const TxPlan& plan) noexcept;
  Error add_rx(const RxPlan& plan) noexcept;
  Error activate(TransportMode mode) noexcept;
  Error submit(const TxRequest& request, std::uint64_t now_us) noexcept;
  Error advance(std::uint64_t now_us) noexcept;
  std::optional<TransportEvent> poll() noexcept;
  void cancel(std::uint64_t session) noexcept;
  bool scenario_complete() const noexcept;
  // Explicit host-driver acknowledgement of a completed finite script/trace.
  // Preserves clock, role, loss generation, counters and session identity.
  Error retire_completed_script() noexcept;
  Error scenario_error() const noexcept { return scenario_error_; }
  Role role() const noexcept { return role_; }
  std::uint64_t loss_generation() const noexcept { return loss_generation_; }
  const TransportStats& stats() const noexcept { return stats_; }
  std::size_t trace_size() const noexcept { return trace_size_; }
  const std::array<RoleTrace, trace_capacity>& trace() const noexcept { return trace_; }
  static bool read_only_packet(Bytes packet) noexcept;

 private:
  void change_role(Role role, std::uint64_t at) noexcept;
  void emit(TransportEvent event) noexcept;
  void discard_overflowed_events() noexcept;
  void fault(Error error, std::uint64_t at) noexcept;
  void step_tx(std::uint64_t at) noexcept;
  void finish(Error error, Phase phase, std::uint64_t at, bool restored) noexcept;
  void receive(const RxPlan& plan) noexcept;
  std::array<TxPlan, script_capacity> tx_plans_{};
  std::array<RxPlan, script_capacity> rx_plans_{};
  std::size_t tx_size_{0}, tx_cursor_{0}, rx_size_{0}, rx_cursor_{0};
  FixedQueue<TransportEvent, event_capacity> events_{};
  std::array<RoleTrace, trace_capacity> trace_{};
  std::size_t trace_size_{0};
  TransportMode mode_{TransportMode::Inactive};
  Role role_{Role::Inactive};
  Error scenario_error_{Error::None};
  std::optional<TxRequest> pending_{};
  TxPlan active_{};
  Phase phase_{Phase::Write};
  std::uint64_t now_{0}, next_phase_us_{0}, loss_generation_{0};
  bool clock_set_{false}, overflow_{false};
  std::size_t written_{0};
  std::uint8_t read_count_{0};
  bool written_known_{true};
  TransportStats stats_{};
};

}  // namespace gecko
