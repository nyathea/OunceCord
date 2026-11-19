// SPDX-License-Identifier: MIT
//
// Fonts and icons built into the app: data/assets.bin, made by
// tools/pack_assets.py and linked in by the build.
#pragma once

#include <string_view>

namespace assets {

// The named entry ("font/normal", "icon/reply"...), or empty if there is none.
std::string_view get(std::string_view name);
// The same lookup in any blob in that format (for tests).
std::string_view find(std::string_view blob, std::string_view name);

}  // namespace assets
