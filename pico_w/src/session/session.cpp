// SPDX-License-Identifier: MIT
#include "gecko/session.hpp"

#include <algorithm>

namespace gecko {
namespace {
bool same_packet(const Packet& a, const Packet& b) noexcept {
  return a.size == b.size && a.bytes().valid() && b.bytes().valid() &&
    std::equal(a.data.begin(), a.data.begin() + static_cast<std::ptrdiff_t>(a.size), b.data.begin());
}
bool due(std::uint64_t now, std::uint64_t since, std::uint64_t interval) noexcept {
  return now >= since && now - since >= interval;
}
bool fatal_error(Error e) noexcept {
  return e == Error::RestoreFailed || e == Error::ClockRegression || e == Error::ClockRange ||
    e == Error::UnexpectedTx || e == Error::ScenarioCapacity || e == Error::ReadOnly;
}
}  // namespace

Error ReadOnlySession::validate() const noexcept {
  if (config_.mode != TransportMode::Inactive && config_.mode != TransportMode::Passive &&
      config_.mode != TransportMode::SyntheticReadOnly) return Error::InvalidValue;
  if (config_.file_order != FileOrder::ConfigThenStatus && config_.file_order != FileOrder::EitherSynthetic)
    return Error::InvalidValue;
  if (config_.max_failed_attempts == 0 || config_.max_failed_attempts > 16 || config_.duplicate_limit > 8)
    return Error::InvalidValue;
  for (auto interval : {config_.tx_timeout_us, config_.step_timeout_us, config_.status_timeout_us,
      config_.stale_after_us, config_.go_interval_us, config_.backoff_initial_us, config_.backoff_max_us})
    if (interval == 0 || interval > UINT64_MAX / 16) return Error::InvalidValue;
  if (config_.backoff_initial_us > config_.backoff_max_us) return Error::InvalidValue;
  if (config_.mode == TransportMode::SyntheticReadOnly) {
    if (config_.decode_mode != DecodeMode::SyntheticCandidate) return Error::UnverifiedProtocol;
    // Explicitly selected synthetic C82/S81 model. No broad v51+ whitelist.
    if (config_.profile != CandidateProfile::InYtV51 ||
        config_.expected_versions.config != 82 || config_.expected_versions.status != 81)
      return Error::UnsupportedProfile;
  }
  return Error::None;
}

Error ReadOnlySession::start(std::uint64_t now_us) noexcept {
  const auto validation = validate();
  if (validation != Error::None) {
    stop(); state_ = SessionState::Fault; last_error_ = validation; return validation;
  }
  if (clock_set_ && now_us < now_) { fail(Error::ClockRegression, now_, true); return Error::ClockRegression; }
  clock_set_ = true; now_ = now_us;
  // Reach the activation instant before changing participation mode. Otherwise a
  // late start would retroactively listen/reply during the inactive interval.
  const auto advanced = transport_.advance(now_);
  if (advanced != Error::None || transport_.scenario_error() != Error::None) {
    const auto error = advanced != Error::None ? advanced : transport_.scenario_error();
    fail(error, now_, true); return error;
  }
  failed_attempts_ = 0; last_error_ = Error::None;
  assembly_.reset(); pending_.reset(); binding_ready_ = false;
  const auto activation = transport_.activate(config_.mode);
  if (activation != Error::None) { fail(activation, now_, true); return activation; }
  if (config_.mode == TransportMode::Inactive) { state_ = SessionState::Disabled; return Error::None; }
  if (config_.mode == TransportMode::Passive) { state_ = SessionState::Disconnected; return Error::None; }
  return begin(now_);
}

void ReadOnlySession::stop() noexcept {
  transport_.cancel(session_);
  const auto ignored = transport_.activate(TransportMode::Inactive);
  (void) ignored;
  pending_.reset(); assembly_.reset(); binding_ready_ = false;
  config_seen_ = status_seen_ = false;
  state_ = SessionState::Disabled;
}

Error ReadOnlySession::begin(std::uint64_t now) noexcept {
  if (session_ == UINT64_MAX) { fail(Error::ClockRange, now, true); return Error::ClockRange; }
  ++session_; ++stats_.attempts;
  config_seen_ = status_seen_ = binding_ready_ = false;
  duplicates_ = 0; clock_packet_ = {}; assembly_.reset(); frame_at_.reset();
  loss_generation_ = transport_.loss_generation();
  go_at_ = progress_at_ = now; state_ = SessionState::GoPending;
  return send(Purpose::Go, now);
}

Error ReadOnlySession::send(Purpose purpose, std::uint64_t now) noexcept {
  if (pending_) { fail(Error::Busy, now); return Error::Busy; }
  if (next_tx_ == UINT64_MAX || config_.tx_timeout_us > UINT64_MAX - now) {
    fail(Error::ClockRange, now, true); return Error::ClockRange;
  }
  const auto encoded = purpose == Purpose::Go ? encode_go() : encode_ack();
  if (!encoded) { fail(encoded.error, now, true); return encoded.error; }
  TxRequest request;
  request.write = encoded.value; request.id = ++next_tx_; request.session = session_;
  request.deadline_us = now + config_.tx_timeout_us;
  const auto error = transport_.submit(request, now);
  if (error != Error::None) { fail(error, now, fatal_error(error)); return error; }
  pending_ = Pending{request.id, request.deadline_us, purpose};
  return Error::None;
}

std::uint32_t ReadOnlySession::random_next() noexcept {
  random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5; return random_;
}

void ReadOnlySession::fail(Error error, std::uint64_t now, bool fatal) noexcept {
  transport_.cancel(session_); pending_.reset(); assembly_.reset(); binding_ready_ = false;
  config_seen_ = status_seen_ = false;
  last_error_ = error; ++stats_.failures; ++failed_attempts_;
  if (fatal || fatal_error(error) || failed_attempts_ >= config_.max_failed_attempts) {
    state_ = SessionState::Fault;
    const auto deactivated = transport_.activate(TransportMode::Inactive);
    (void) deactivated;
    return;
  }
  std::uint64_t interval = config_.backoff_initial_us;
  for (unsigned i = 1; i < failed_attempts_ && interval < config_.backoff_max_us; ++i)
    interval = interval > config_.backoff_max_us / 2 ? config_.backoff_max_us : interval * 2;
  const auto jitter = static_cast<std::uint64_t>(random_next()) % (interval / 4 + 1);
  backoff_us_ = std::min(config_.backoff_max_us, interval + jitter);
  backoff_at_ = now; state_ = SessionState::Backoff;
}

bool ReadOnlySession::active_handshake() const noexcept {
  return state_ == SessionState::GoPending || state_ == SessionState::AwaitingFiles ||
    state_ == SessionState::AwaitingClock || state_ == SessionState::AwaitingLo;
}

void ReadOnlySession::on_tx(const TransportEvent& event, std::uint64_t now) noexcept {
  const auto& tx = event.tx;
  if (!pending_ || tx.session != session_ || tx.id != pending_->id) { ++stats_.stale_events; return; }
  const auto purpose = pending_->purpose;
  last_tx_ = tx;
  if (tx.error != Error::None || !tx.restored || !tx.write_count_known || tx.written != 15 || tx.read_count != 2 ||
      event.at_us >= pending_->deadline) {
    const auto error = tx.error != Error::None ? tx.error : !tx.restored ? Error::RestoreFailed :
      event.at_us >= pending_->deadline ? Error::Timeout : Error::ShortRead;
    fail(error, now); return;
  }
  pending_.reset();
  if (purpose == Purpose::Go) { state_ = SessionState::AwaitingFiles; progress_at_ = event.at_us; }
  else if (purpose == Purpose::FileAck) {
    if (config_seen_ && status_seen_) { binding_ready_ = true; state_ = SessionState::AwaitingClock; }
    progress_at_ = event.at_us;
  } else if (purpose == Purpose::ClockAck) { state_ = SessionState::AwaitingLo; progress_at_ = event.at_us; }
  // Duplicate ACKs deliberately do not extend the handshake progress deadline.
}

void ReadOnlySession::on_rx(const TransportEvent& event, std::uint64_t now) noexcept {
  const auto& rx = event.rx;
  if (rx.context.session != session_) { ++stats_.stale_events; return; }
  if (rx.address != 0x17 || rx.context.at_us != event.at_us || event.at_us > now) { ++stats_.invalid_rx; return; }
  if (event.loss_generation != loss_generation_ || rx.context.loss_generation != loss_generation_) {
    fail(Error::TransportLoss, now); return;
  }
  const auto frame = decode_frame(rx.packet.bytes(), config_.decode_mode);
  if (!frame) { ++stats_.invalid_rx; if (assembly_.buffered() != 0) ++stats_.assembly_aborts; assembly_.reset(); return; }
  const auto kind = frame.value.kind;
  if (kind == FrameKind::Go || kind == FrameKind::Ack || kind == FrameKind::Program ||
      kind == FrameKind::Temperature || kind == FrameKind::Switch || kind == FrameKind::Notification) {
    if (assembly_.buffered() != 0) { ++stats_.assembly_aborts; assembly_.reset(); }
    ++stats_.ignored_rx; return;
  }
  if (pending_) { fail(Error::HandshakeOrder, now); return; }
  if (kind == FrameKind::VersionFile) {
    const auto& file = frame.value.file;
    if (file.version != (file.config ? config_.expected_versions.config : config_.expected_versions.status)) {
      fail(Error::UnknownVersion, now); return;
    }
    const bool already = file.config ? config_seen_ : status_seen_;
    if (state_ != SessionState::AwaitingFiles && !(state_ == SessionState::AwaitingClock && already)) {
      fail(Error::HandshakeOrder, now); return;
    }
    if (!file.config && !config_seen_ && config_.file_order != FileOrder::EitherSynthetic) {
      fail(Error::HandshakeOrder, now); return;
    }
    if (already) {
      if (++duplicates_ > config_.duplicate_limit) { fail(Error::HandshakeOrder, now); return; }
      const auto sent = send(Purpose::DuplicateAck, now); if (sent != Error::None) return;
    } else {
      if (file.config) config_seen_ = true; else status_seen_ = true;
      progress_at_ = now;
      const auto sent = send(Purpose::FileAck, now); if (sent != Error::None) return;
    }
  } else if (kind == FrameKind::Clock) {
    if (state_ == SessionState::AwaitingLo && same_packet(clock_packet_, rx.packet)) {
      if (++duplicates_ > config_.duplicate_limit) { fail(Error::HandshakeOrder, now); return; }
      const auto sent = send(Purpose::DuplicateAck, now); if (sent != Error::None) return;
    } else if (state_ == SessionState::AwaitingClock && binding_ready_) {
      clock_packet_ = rx.packet; progress_at_ = now;
      const auto sent = send(Purpose::ClockAck, now); if (sent != Error::None) return;
    } else { fail(Error::HandshakeOrder, now); return; }
  } else if (kind == FrameKind::Lo) {
    if (state_ != SessionState::AwaitingLo || !binding_ready_) { fail(Error::HandshakeOrder, now); return; }
    state_ = SessionState::Ready; progress_at_ = now;
  } else if (kind == FrameKind::Fragment) {
    if (state_ != SessionState::Ready || !binding_ready_) { ++stats_.ignored_rx; return; }
    if (now - event.at_us >= config_.stale_after_us) { ++stats_.stale_events; assembly_.reset(); return; }
    const auto message = assembly_.push(rx.packet.bytes(), rx.context, config_.decode_mode);
    if (message.state == AssemblyState::Rejected) { ++stats_.assembly_aborts; ++stats_.invalid_rx; return; }
    if (message.state == AssemblyState::Complete) {
      const auto status = decode_status(message.message.bytes(), config_.profile, config_.decode_mode);
      if (!status) { ++stats_.invalid_rx; return; }
      const auto first = message.message.first_at_us;
      const auto complete = message.message.complete_at_us;
      if (first > complete || complete > now || now - first >= config_.stale_after_us) {
        ++stats_.stale_events; return;
      }
      if (revision_ == UINT64_MAX) { fail(Error::ClockRange, now, true); return; }
      observed_ = status.value; status_at_ = first; completed_at_ = complete; data_session_ = session_; ++revision_;
      ++stats_.statuses; failed_attempts_ = 0;
      last_error_ = Error::None;
    }
  } else { ++stats_.ignored_rx; return; }
  frame_at_ = event.at_us;
}

void ReadOnlySession::timers(std::uint64_t now) noexcept {
  if (pending_ && now >= pending_->deadline) { fail(Error::Timeout, now); return; }
  if (active_handshake() && due(now, progress_at_, config_.step_timeout_us)) { fail(Error::Timeout, now); return; }
  if (state_ == SessionState::Ready) {
    if (data_session_ == session_ && status_at_) {
      if (due(now, *status_at_, config_.stale_after_us)) { fail(Error::StaleState, now); return; }
    } else if (due(now, progress_at_, config_.status_timeout_us)) { fail(Error::Timeout, now); return; }
  }
}

Error ReadOnlySession::tick(std::uint64_t now_us) noexcept {
  if (clock_set_ && now_us < now_) { fail(Error::ClockRegression, now_, true); return Error::ClockRegression; }
  clock_set_ = true; now_ = now_us;
  const auto advanced = transport_.advance(now_);
  if (advanced != Error::None) { fail(advanced, now_, true); return advanced; }
  if (transport_.scenario_error() != Error::None && state_ != SessionState::Fault) {
    fail(transport_.scenario_error(), now_, true); return last_error_;
  }
  for (std::size_t count = 0; count <= SimulatedTransport::event_capacity; ++count) {
    const auto event = transport_.poll();
    if (!event) break;
    if (state_ == SessionState::Disabled || config_.mode == TransportMode::Passive) { ++stats_.ignored_rx; continue; }
    if (event->kind == EventKind::Tx) on_tx(*event, now_);
    else if (state_ == SessionState::Fault || state_ == SessionState::Backoff) { ++stats_.stale_events; }
    else if (event->kind == EventKind::Fault) fail(event->error, now_);
    else { timers(now_); if (state_ != SessionState::Backoff && state_ != SessionState::Fault) on_rx(*event, now_); }
  }
  if (state_ == SessionState::Fault || state_ == SessionState::Disabled || config_.mode == TransportMode::Passive)
    return last_error_;
  if (assembly_.expire(now_) != Error::None) ++stats_.assembly_aborts;
  timers(now_);
  if (state_ == SessionState::Backoff && due(now_, backoff_at_, backoff_us_)) return begin(now_);
  if (state_ == SessionState::Ready && !pending_ && due(now_, go_at_, config_.go_interval_us)) return begin(now_);
  return last_error_;
}

Snapshot ReadOnlySession::snapshot(std::uint64_t now_us) const noexcept {
  Snapshot out;
  out.observed = observed_; out.revision = revision_; out.data_session = data_session_;
  if (!clock_set_ || now_us < now_) return out;
  if (status_at_) out.status_age_us = now_us - *status_at_;
  if (completed_at_) out.completed_age_us = now_us - *completed_at_;
  if (frame_at_) out.candidate_frame_age_us = now_us - *frame_at_;
  out.stale = !observed_ || !status_at_ || data_session_ != session_ || !binding_ready_ ||
    state_ != SessionState::Ready || *out.status_age_us >= config_.stale_after_us;
  out.ready = !out.stale;
  return out;
}

}  // namespace gecko
