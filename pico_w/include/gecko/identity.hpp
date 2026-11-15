// SPDX-License-Identifier: MIT
#pragma once
// Native host tests intentionally have no installed-device identity.
#ifndef GECKO_FIRMWARE_BUILD_ID
#define GECKO_FIRMWARE_BUILD_ID "0000000000000000000000000000000000000000000000000000000000000000"
#endif
namespace gecko::runtime {
inline constexpr const char* firmware_version="0.2.1-pico-sim";
inline constexpr const char* firmware_build_id=GECKO_FIRMWARE_BUILD_ID;
static_assert(sizeof(GECKO_FIRMWARE_BUILD_ID)==65);
}
