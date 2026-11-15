// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/protocol.hpp"

namespace gecko {

struct FragmentContext {
  std::uint64_t at_us{0};
  std::uint64_t session{0};
  std::uint64_t loss_generation{0};
  // Out-of-band ordering supplied by a synthetic scenario, NOT a wire counter.
  std::optional<std::uint32_t> synthetic_index{};
};
struct AssemblyPolicy {
  std::uint64_t gap_us{250000};
  std::uint64_t total_us{1000000};
};
struct Assembly {
  std::array<std::uint8_t, max_assembly_bytes> data{};
  std::size_t size{0};
  bool hardware_verified{false};
  // Acquisition bounds from synthetic transport metadata, not pack timestamps.
  std::uint64_t first_at_us{0}, complete_at_us{0};
  Bytes bytes() const noexcept { return {size <= data.size() ? data.data() : nullptr, size}; }
};
enum class AssemblyState { Incomplete, Complete, Rejected };
struct AssemblyResult {
  AssemblyState state{AssemblyState::Rejected};
  Error error{Error::None};
  Assembly message{};
};

// Synthetic reassembly only until fragment integrity/order is evidenced on wire.
class Assembler {
 public:
  explicit Assembler(AssemblyPolicy policy = {}) noexcept : policy_(policy) {}
  AssemblyResult push(Bytes raw_frame, FragmentContext context,
                      DecodeMode mode = DecodeMode::DocumentedOnly) noexcept;
  Error expire(std::uint64_t now_us) noexcept;
  void reset() noexcept;
  std::size_t buffered() const noexcept { return buffer_.size; }

 private:
  AssemblyResult reject(Error error) noexcept;
  AssemblyPolicy policy_{};
  Assembly buffer_{};
  bool active_{false};
  std::uint64_t first_us_{0}, last_us_{0}, session_{0}, loss_generation_{0};
  std::uint32_t next_index_{0};
  std::array<std::uint8_t, 16> header_{};
};

}  // namespace gecko
