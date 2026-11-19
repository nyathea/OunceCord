// SPDX-License-Identifier: MIT
#include "util/assets.h"

#include <cstdint>
#include <cstring>

#if defined(__SWITCH__)
#include "assets_bin.h"
#endif

namespace assets {

static uint32_t le32(const char *p)
{
    const unsigned char *u = (const unsigned char *)p;
    return u[0] | (u[1] << 8) | (u[2] << 16) | ((uint32_t)u[3] << 24);
}

std::string_view find(std::string_view blob, std::string_view name)
{
    // "OCA1", u32 count, then per entry: u16 name length, name, u32 size, data.
    if (blob.size() < 8 || blob.substr(0, 4) != "OCA1")
        return {};
    uint32_t count = le32(blob.data() + 4);
    size_t at = 8;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 2 > blob.size())
            return {};
        size_t n = (unsigned char)blob[at] | ((unsigned char)blob[at + 1] << 8);
        at += 2;
        if (at + n + 4 > blob.size())
            return {};
        std::string_view entry = blob.substr(at, n);
        at += n;
        size_t size = le32(blob.data() + at);
        at += 4;
        if (size > blob.size() - at)
            return {};
        if (entry == name)
            return blob.substr(at, size);
        at += size;
    }
    return {};
}

std::string_view get(std::string_view name)
{
#if defined(__SWITCH__)
    return find(std::string_view((const char *)assets_bin, assets_bin_size), name);
#else
    (void)name;
    return {};
#endif
}

}  // namespace assets
