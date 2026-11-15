// SPDX-License-Identifier: MIT
#include "gecko/runtime.hpp"
#include <algorithm>
namespace gecko::runtime {
bool Maintenance::tick(std::uint64_t wall) noexcept {
  if(wall<last_wall_ || wall<wall_base_ || wall-wall_base_>UINT64_MAX-model_base_) {
    api_.unavailable();return false;
  }
  last_wall_=wall;const auto desired=model_base_+(wall-wall_base_);
  if(desired<model_.now_ms()) {api_.unavailable();return false;}
  const auto delta=std::min(desired-model_.now_ms(),static_cast<std::uint64_t>(100));
  if(!model_.advance(model_.now_ms()+delta) || model_.now_ms()!=desired) {api_.unavailable();return false;}
  api_.publish(model_,wall);return true;
}
} // namespace gecko::runtime
