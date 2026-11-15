// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/transport.hpp"

namespace gecko {

enum class SessionState { Disabled, Disconnected, GoPending, AwaitingFiles, AwaitingClock, AwaitingLo, Ready, Backoff, Fault };
enum class FileOrder { ConfigThenStatus, EitherSynthetic };
struct SessionConfig {
  TransportMode mode{TransportMode::Inactive};
  DecodeMode decode_mode{DecodeMode::DocumentedOnly};
  CandidateProfile profile{CandidateProfile::Unknown};
  Versions expected_versions{};
  FileOrder file_order{FileOrder::ConfigThenStatus};
  std::uint64_t tx_timeout_us{25000}, step_timeout_us{1000000}, status_timeout_us{1000000};
  std::uint64_t stale_after_us{60000000}, go_interval_us{23000000};
  std::uint64_t backoff_initial_us{100000}, backoff_max_us{1000000};
  std::uint32_t seed{1};
  unsigned max_failed_attempts{3}, duplicate_limit{2};
};
struct SessionStats {
  std::uint64_t attempts{0}, invalid_rx{0}, ignored_rx{0}, stale_events{0}, statuses{0};
  std::uint64_t failures{0}, assembly_aborts{0}, control_denied{0};
};
struct Snapshot {
  std::optional<Status> observed{};
  std::optional<std::uint64_t> status_age_us{}, candidate_frame_age_us{};
  std::optional<std::uint64_t> completed_age_us{};
  std::uint64_t revision{0}, data_session{0};
  bool stale{true}, ready{false}, synthetic{true}, hardware_verified{false};
};

// Portable synthetic session only; no transport substitution or write-enable switch.
class ReadOnlySession {
 public:
  explicit ReadOnlySession(SimulatedTransport& transport, SessionConfig config = {}) noexcept
      : transport_(transport), config_(config), random_(config.seed == 0 ? 1 : config.seed) {}
  ReadOnlySession(const ReadOnlySession&) = delete;
  ReadOnlySession& operator=(const ReadOnlySession&) = delete;
  ReadOnlySession(ReadOnlySession&&) = delete;
  ReadOnlySession& operator=(ReadOnlySession&&) = delete;
  Error start(std::uint64_t now_us) noexcept;
  void stop() noexcept;
  Error tick(std::uint64_t now_us) noexcept;
  Error request_control(Bytes) noexcept { ++stats_.control_denied; return Error::ReadOnly; }
  Snapshot snapshot(std::uint64_t now_us) const noexcept;
  SessionState state() const noexcept { return state_; }
  std::uint64_t session_id() const noexcept { return session_; }
  Versions versions() const noexcept { return binding_ready_ ? config_.expected_versions : Versions{}; }
  const SessionStats& stats() const noexcept { return stats_; }
  Error last_error() const noexcept { return last_error_; }
  const std::optional<TxResult>& last_tx_result() const noexcept { return last_tx_; }
  std::uint64_t backoff_duration_us() const noexcept { return backoff_us_; }

 private:
  enum class Purpose { Go, FileAck, ClockAck, DuplicateAck };
  struct Pending { std::uint64_t id{0}, deadline{0}; Purpose purpose{Purpose::Go}; };
  Error validate() const noexcept;
  Error begin(std::uint64_t now) noexcept;
  Error send(Purpose purpose, std::uint64_t now) noexcept;
  void fail(Error error, std::uint64_t now, bool fatal = false) noexcept;
  void on_tx(const TransportEvent& event, std::uint64_t now) noexcept;
  void on_rx(const TransportEvent& event, std::uint64_t now) noexcept;
  void timers(std::uint64_t now) noexcept;
  bool active_handshake() const noexcept;
  std::uint32_t random_next() noexcept;
  SimulatedTransport& transport_;
  SessionConfig config_{};
  SessionState state_{SessionState::Disabled};
  Assembler assembly_{};
  std::optional<Pending> pending_{};
  std::optional<TxResult> last_tx_{};
  std::optional<Status> observed_{};
  std::optional<std::uint64_t> status_at_{}, frame_at_{};
  std::optional<std::uint64_t> completed_at_{};
  Packet clock_packet_{};
  std::uint64_t session_{0}, next_tx_{0}, now_{0}, progress_at_{0}, go_at_{0};
  std::uint64_t backoff_at_{0}, backoff_us_{0}, revision_{0}, data_session_{0}, loss_generation_{0};
  std::uint32_t random_{1};
  unsigned failed_attempts_{0}, duplicates_{0};
  bool clock_set_{false}, config_seen_{false}, status_seen_{false}, binding_ready_{false};
  Error last_error_{Error::None};
  SessionStats stats_{};
};

}  // namespace gecko
