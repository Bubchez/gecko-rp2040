// SPDX-License-Identifier: MIT
#pragma once
#include "gecko/runtime.hpp"
namespace gecko::runtime {
struct UsbInfo {
  std::string_view boot_id{},device_id{},ip{};
  bool provisioned{false};
  WifiState wifi{WifiState::Unprovisioned};
  std::uint32_t attempts{0},reconnects{0};
};
// One bounded metadata line; no API can supply passwords/tokens/SSID/control flags.
std::size_t format_usb_info(const UsbInfo&,std::array<char,513>&) noexcept;
}
