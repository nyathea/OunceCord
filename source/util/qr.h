// SPDX-License-Identifier: MIT
//
// A small QR code encoder (ISO/IEC 18004): byte mode, error correction
// level M, versions 1 to 10 (up to 213 bytes), automatic mask choice.
// Enough for login URLs.
#pragma once

#include <string_view>
#include <vector>

namespace qr {

struct Code {
    int size = 0;                  // modules per side; 0 when encoding failed
    std::vector<bool> modules;     // row-major, true = dark

    bool at(int x, int y) const { return modules[(size_t)y * size + x]; }
};

Code encode(std::string_view data);

}  // namespace qr
