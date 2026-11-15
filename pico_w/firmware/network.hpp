// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/runtime.hpp"
namespace firmware {
bool server_start(gecko::runtime::Api&) noexcept;
void server_stop() noexcept;
void server_poll() noexcept;
}
