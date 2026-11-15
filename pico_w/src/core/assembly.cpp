// SPDX-License-Identifier: MIT
#include "gecko/assembly.hpp"

#include <algorithm>

namespace gecko {

void Assembler::reset() noexcept {
  buffer_ = {};
  active_ = false;
  first_us_ = last_us_ = session_ = loss_generation_ = 0;
  next_index_ = 0;
  header_ = {};
}

AssemblyResult Assembler::reject(Error error) noexcept {
  reset();
  return {AssemblyState::Rejected, error, {}};
}

Error Assembler::expire(std::uint64_t now_us) noexcept {
  if (!active_) return Error::None;
  Error error = Error::None;
  if (now_us < last_us_) error = Error::ClockRegression;
  else if (now_us - last_us_ >= policy_.gap_us || now_us - first_us_ >= policy_.total_us)
    error = Error::Timeout;
  if (error != Error::None) reset();
  return error;
}

AssemblyResult Assembler::push(Bytes raw, FragmentContext context, DecodeMode mode) noexcept {
  if (policy_.gap_us == 0 || policy_.total_us == 0) return reject(Error::InvalidValue);
  const auto parsed = decode_frame(raw, mode);
  if (!parsed) return reject(parsed.error);
  if (parsed.value.kind != FrameKind::Fragment) return reject(Error::UnknownType);
  if (!context.synthetic_index) return reject(Error::UnverifiedProtocol);
  if (active_) {
    if (context.session != session_) return reject(Error::SessionChanged);
    if (context.loss_generation != loss_generation_) return reject(Error::TransportLoss);
    const auto timeout = expire(context.at_us);
    if (timeout != Error::None) return reject(timeout);
  }
  if (*context.synthetic_index != next_index_) return reject(Error::Sequence);
  if (!active_) {
    active_ = true;
    first_us_ = context.at_us;
    session_ = context.session;
    loss_generation_ = context.loss_generation;
    std::copy(raw.data, raw.data + header_.size(), header_.begin());
  } else {
    for (std::size_t i = 0; i < header_.size(); ++i) {
      // Length/type bytes are not interpreted as counters; continuation varies.
      if (i != 9 && i != 12 && i != 13 && raw.data[i] != header_[i])
        return reject(Error::InvalidHeader);
    }
  }
  const auto payload = raw.slice(16, raw.size - 16).value;
  if (payload.size > buffer_.data.size() - buffer_.size) return reject(Error::TooLong);
  std::copy(payload.data, payload.data + payload.size, buffer_.data.data() + buffer_.size);
  buffer_.size += payload.size;
  last_us_ = context.at_us;
  ++next_index_;  // At most 512 one-byte fragments can fit; cannot overflow.
  if (parsed.value.more) return {AssemblyState::Incomplete, Error::None, {}};
  AssemblyResult out{AssemblyState::Complete, Error::None, buffer_};
  reset();
  return out;
}

}  // namespace gecko
