// SPDX-License-Identifier: MIT
//
// A GIF decoder (GIF87a/89a): LZW, local and global palettes, interlacing,
// transparency, and frame disposal. Every frame comes out as a full RGBA
// canvas, ready to upload as a texture. Platform-free.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gif {

struct Frame {
    std::vector<uint8_t> rgba;  // width * height * 4
    int delay_ms = 100;
};

struct Image {
    int width = 0, height = 0;
    std::vector<Frame> frames;
    bool truncated = false;  // stopped early at max_bytes
};

// Decodes up to max_bytes of frame data (frames past that are dropped, so a
// long GIF still plays its start). False when the data isn't a usable GIF.
bool decode(std::string_view data, Image &out, size_t max_bytes, int max_side = 2048);

}  // namespace gif
