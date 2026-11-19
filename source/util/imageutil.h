// SPDX-License-Identifier: MIT
//
// Small image helpers: telling formats apart by their bytes, and shrinking
// RGBA pixels for thumbnails. Platform-free.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace imageutil {

// "jpg", "png", "gif", "webp", or "" when unknown.
std::string sniffExtension(std::string_view data);
// For a file name's extension (any case): "image/jpeg", "video/mp4" etc., "" when not a file we send.
std::string contentTypeForName(std::string_view name);
// Replaces (or adds) the extension.
std::string withExtension(std::string_view name, std::string_view ext);

// Averages k x k blocks so the longer side is at most max_side. Returns
// false (and leaves rgba alone) when the image is already small enough.
bool shrink(std::vector<uint8_t> &rgba, int &w, int &h, int max_side);

}  // namespace imageutil
