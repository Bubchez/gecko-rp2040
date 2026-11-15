// SPDX-License-Identifier: MIT
#include "gecko/transport.hpp"

#include <algorithm>
#include <limits>

namespace gecko {
namespace {
bool packet_equal(const Packet& a, const Packet& b) noexcept {
  return a.bytes().valid() && b.bytes().valid() && a.size == b.size &&
         std::equal(a.data.begin(), a.data.begin() + static_cast<std::ptrdiff_t>(a.size), b.data.begin());
}
bool outcome_valid(Error e) noexcept {
  return e == Error::None || e == Error::AddressNack || e == Error::DataNack ||
    e == Error::ArbitrationLost || e == Error::Timeout || e == Error::ShortRead ||
    e == Error::RestoreFailed || e == Error::StuckSda || e == Error::StuckScl;
}
bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (b > UINT64_MAX - a) return false;
  out = a + b; return true;
}
}  // namespace

bool SimulatedTransport::read_only_packet(Bytes bytes) noexcept {
  const auto frame = decode_frame(bytes);
  return frame && (frame.value.kind == FrameKind::Go || frame.value.kind == FrameKind::Ack);
}

Error SimulatedTransport::add_tx(const TxPlan& plan) noexcept {
  if (tx_size_ == script_capacity) return Error::ScenarioCapacity;
  if (!read_only_packet(plan.expected.bytes()) || plan.earliest_us > plan.latest_us ||
      !outcome_valid(plan.outcome) || static_cast<unsigned>(plan.fault_phase) > 3 ||
      plan.partial_write > plan.expected.size || plan.read_count > 2)
    return Error::InvalidValue;
  if ((plan.outcome == Error::AddressNack && (plan.fault_phase != Phase::Write || plan.partial_write != 0)) ||
      (plan.outcome == Error::ShortRead && (plan.fault_phase != Phase::Read || plan.read_count >= 2)) ||
      (plan.outcome == Error::RestoreFailed && plan.fault_phase != Phase::Restore) ||
      (plan.outcome == Error::DataNack && plan.fault_phase != Phase::Write) ||
      (plan.outcome == Error::None && plan.read_count != 2))
    return Error::InvalidValue;
  std::uint64_t sum = 0;
  for (auto duration : plan.duration_us)
    if (!checked_add(sum, duration, sum)) return Error::ClockRange;
  tx_plans_[tx_size_++] = plan;
  return Error::None;
}

Error SimulatedTransport::add_rx(const RxPlan& plan) noexcept {
  if (rx_size_ == script_capacity) return Error::ScenarioCapacity;
  if (!plan.packet.bytes().valid() || plan.packet.size > max_frame_bytes ||
      (rx_size_ != 0 && plan.context.at_us < rx_plans_[rx_size_ - 1].context.at_us) ||
      (clock_set_ && plan.context.at_us < now_)) return Error::InvalidValue;
  rx_plans_[rx_size_++] = plan;
  return Error::None;
}

Error SimulatedTransport::activate(TransportMode mode) noexcept {
  if (mode != TransportMode::Inactive && mode != TransportMode::Passive && mode != TransportMode::SyntheticReadOnly)
    return Error::InvalidValue;
  pending_.reset(); events_.clear(); overflow_ = false;
  mode_ = mode;
  change_role(mode == TransportMode::Inactive ? Role::Inactive :
              mode == TransportMode::Passive ? Role::Observing : Role::Listen, now_);
  return Error::None;
}

void SimulatedTransport::change_role(Role role, std::uint64_t at) noexcept {
  role_ = role;
  if (trace_size_ == trace_capacity) { scenario_error_ = Error::ScenarioCapacity; return; }
  trace_[trace_size_++] = {at, pending_ ? pending_->id : 0, role};
}

void SimulatedTransport::emit(TransportEvent event) noexcept {
  event.loss_generation = loss_generation_;
  if (!events_.push(event)) {
    ++stats_.queue_overflows; ++loss_generation_;
    discard_overflowed_events();
    ++stats_.queue_dropped_events;
    if (event.kind == EventKind::Rx) ++stats_.dropped_rx;
    overflow_ = true;
  }
}

void SimulatedTransport::discard_overflowed_events() noexcept {
  while (const auto event = events_.pop()) {
    ++stats_.queue_dropped_events;
    if (event->kind == EventKind::Rx) ++stats_.dropped_rx;
  }
}

void SimulatedTransport::fault(Error error, std::uint64_t at) noexcept {
  TransportEvent event;
  event.kind = EventKind::Fault; event.at_us = at; event.error = error;
  emit(event);
}

Error SimulatedTransport::submit(const TxRequest& request, std::uint64_t now_us) noexcept {
  // Content-based enforcement: a misleading command label cannot bypass this.
  if (!read_only_packet(request.write.bytes())) { ++stats_.denied; return Error::ReadOnly; }
  const auto advanced = advance(now_us);
  if (advanced != Error::None) return advanced;
  if (mode_ != TransportMode::SyntheticReadOnly || role_ == Role::Released) return Error::Disabled;
  if (pending_) return Error::Busy;
  if (request.id == 0 || request.session == 0 || request.address != 0x17 || request.read_length != 2 ||
      !request.repeated_start || !request.final_stop) return Error::InvalidValue;
  if (request.deadline_us <= now_us) return Error::Timeout;
  if (tx_cursor_ == tx_size_ || !packet_equal(request.write, tx_plans_[tx_cursor_].expected) ||
      now_us < tx_plans_[tx_cursor_].earliest_us || now_us > tx_plans_[tx_cursor_].latest_us) {
    scenario_error_ = Error::UnexpectedTx;
    return Error::UnexpectedTx;
  }
  const auto& plan = tx_plans_[tx_cursor_];
  std::uint64_t completion = now_us;
  for (auto duration : plan.duration_us)
    if (!checked_add(completion, duration, completion)) return Error::ClockRange;
  active_ = plan; ++tx_cursor_; ++stats_.submitted;
  pending_ = request; written_ = 0; read_count_ = 0; written_known_ = false; phase_ = Phase::Write;
  next_phase_us_ = now_us + active_.duration_us[0];
  change_role(Role::MasterWrite, now_us);
  return Error::None;
}

void SimulatedTransport::finish(Error error, Phase phase, std::uint64_t at, bool restored) noexcept {
  TransportEvent event;
  event.kind = EventKind::Tx; event.at_us = at;
  event.tx = {pending_->id, pending_->session, error, phase, written_, active_.read,
              read_count_, restored, written_known_};
  change_role(restored ? Role::Listen : Role::Released, at);
  pending_.reset(); ++stats_.completed;
  emit(event);
}

void SimulatedTransport::step_tx(std::uint64_t at) noexcept {
  if (at >= pending_->deadline_us) {
    // Atomic synthetic abort/release. It does NOT establish a hardware recovery time.
    change_role(Role::Restore, at);
    finish(Error::Timeout, phase_, at, true); return;
  }
  const bool failed = active_.outcome != Error::None && phase_ == active_.fault_phase;
  if (phase_ == Phase::Write) {
    written_ = failed ? active_.partial_write : pending_->write.size;
    written_known_ = true;
  }
  if (phase_ == Phase::Read) read_count_ = failed ? active_.read_count : 2;
  if (failed) {
    change_role(Role::Restore, at);
    finish(active_.outcome, phase_, at, active_.outcome != Error::RestoreFailed); return;
  }
  if (phase_ == Phase::Restore) {
    finish(Error::None, phase_, at, true); return;
  }
  phase_ = static_cast<Phase>(static_cast<unsigned>(phase_) + 1U);
  const auto index = static_cast<std::size_t>(phase_);
  next_phase_us_ = at + active_.duration_us[index];
  change_role(phase_ == Phase::Read ? Role::MasterRead : phase_ == Phase::Stop ? Role::Stop : Role::Restore, at);
}

void SimulatedTransport::receive(const RxPlan& plan) noexcept {
  const auto at = plan.context.at_us;
  if (mode_ == TransportMode::Inactive) return;
  if (pending_ || role_ == Role::Released) {
    ++loss_generation_; ++stats_.dropped_rx;
    fault(Error::RxCollision, at); return;
  }
  if (mode_ == TransportMode::SyntheticReadOnly && plan.address == 0x17) {
    change_role(Role::Receive, at);
    if (plan.repeated_read) change_role(Role::Reply, at);
    change_role(Role::Listen, at);
  }
  TransportEvent event;
  event.kind = EventKind::Rx; event.at_us = at; event.rx = plan;
  emit(event);
}

Error SimulatedTransport::advance(std::uint64_t now_us) noexcept {
  if (clock_set_ && now_us < now_) {
    pending_.reset(); events_.clear(); ++loss_generation_;
    change_role(Role::Released, now_); fault(Error::ClockRegression, now_);
    return Error::ClockRegression;
  }
  clock_set_ = true;
  // At a tie, TX completion/deadline is processed before inbound arrival.
  for (;;) {
    const bool has_rx = rx_cursor_ < rx_size_ && rx_plans_[rx_cursor_].context.at_us <= now_us;
    const std::uint64_t tx_at = pending_ ? std::min(next_phase_us_, pending_->deadline_us) : UINT64_MAX;
    const bool has_tx = pending_ && tx_at <= now_us;
    if (!has_rx && !has_tx) break;
    if (has_tx && (!has_rx || tx_at <= rx_plans_[rx_cursor_].context.at_us)) step_tx(tx_at);
    else receive(rx_plans_[rx_cursor_++]);
  }
  now_ = now_us;
  return Error::None;
}

std::optional<TransportEvent> SimulatedTransport::poll() noexcept {
  if (overflow_) {
    overflow_ = false; discard_overflowed_events();
    TransportEvent event;
    event.kind = EventKind::Fault; event.at_us = now_; event.error = Error::TransportLoss;
    event.loss_generation = loss_generation_;
    return event;
  }
  return events_.pop();
}

void SimulatedTransport::cancel(std::uint64_t session) noexcept {
  if (pending_ && pending_->session == session) {
    pending_.reset();
    change_role(mode_ == TransportMode::SyntheticReadOnly ? Role::Listen :
                mode_ == TransportMode::Passive ? Role::Observing : Role::Inactive, now_);
  }
}

bool SimulatedTransport::scenario_complete() const noexcept {
  return scenario_error_ == Error::None && tx_cursor_ == tx_size_ && rx_cursor_ == rx_size_ &&
         !pending_ && events_.size() == 0 && !overflow_;
}

}  // namespace gecko
