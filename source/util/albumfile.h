// SPDX-License-Identifier: MIT
//
// Helpers for the console's screenshot album: file names and the small
// preview JPEG that screenshots carry in their EXIF data. Platform-free.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace album {

struct Date {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, id = 0;
    // Sortable: newer is larger.
    uint64_t sortKey() const;
    std::string label() const;     // "2026-11-18 14:03"
    std::string fileName() const;  // "screenshot-2026-11-18-140322.jpg"
};

// "2026111814032200-0123456789ABCDEF0123456789ABCDEF.jpg" (the date part
// is YYYYMMDDHHMMSS plus a two-digit id). False for other names.
bool parseFileName(std::string_view name, Date &out);

// The EXIF thumbnail embedded in a JPEG, as a view into it. False when the
// file has none.
bool exifThumbnail(std::string_view jpeg, std::string_view &thumb);

}  // namespace album
