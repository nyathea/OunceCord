// SPDX-License-Identifier: MIT
#include "util/albumfile.h"

#include <cstdio>

namespace album {

uint64_t Date::sortKey() const
{
    return ((((((uint64_t)year * 13 + month) * 32 + day) * 24 + hour) * 60 + minute) * 60 + second) * 100 + id;
}

std::string Date::label() const
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return buf;
}

std::string Date::fileName() const
{
    char buf[48];
    snprintf(buf, sizeof(buf), "screenshot-%04d-%02d-%02d-%02d%02d%02d.jpg", year, month, day, hour, minute,
             second);
    return buf;
}

std::string Date::videoFileName() const
{
    char buf[48];
    snprintf(buf, sizeof(buf), "clip-%04d-%02d-%02d-%02d%02d%02d.mp4", year, month, day, hour, minute, second);
    return buf;
}

bool parseFileName(std::string_view name, Date &out, bool *video)
{
    if (name.size() < 20)
        return false;
    for (int i = 0; i < 16; i++)
        if (name[i] < '0' || name[i] > '9')
            return false;
    if (name[16] != '-')
        return false;
    std::string_view ext = name.substr(name.size() - 4);
    bool is_video = ext == ".mp4" || ext == ".MP4";
    if (ext != ".jpg" && ext != ".JPG" && !is_video)
        return false;
    if (video)
        *video = is_video;
    auto num = [&](int at, int len) {
        int v = 0;
        for (int i = 0; i < len; i++)
            v = v * 10 + (name[at + i] - '0');
        return v;
    };
    Date d;
    d.year = num(0, 4);
    d.month = num(4, 2);
    d.day = num(6, 2);
    d.hour = num(8, 2);
    d.minute = num(10, 2);
    d.second = num(12, 2);
    d.id = num(14, 2);
    if (d.month < 1 || d.month > 12 || d.day < 1 || d.day > 31 || d.hour > 23 || d.minute > 59 || d.second > 60)
        return false;
    out = d;
    return true;
}

namespace {

uint32_t rd(const uint8_t *p, bool le, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint32_t)p[le ? i : n - 1 - i] << (8 * i);
    return v;
}

}  // namespace

bool exifThumbnail(std::string_view jpeg, std::string_view &thumb)
{
    const uint8_t *d = (const uint8_t *)jpeg.data();
    size_t n = jpeg.size();
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8)
        return false;
    size_t pos = 2;
    // Walk the segments before the image data, looking for APP1 "Exif".
    while (pos + 4 <= n && d[pos] == 0xFF) {
        uint8_t marker = d[pos + 1];
        if (marker == 0xDA || marker == 0xD9)
            break;
        size_t len = ((size_t)d[pos + 2] << 8) | d[pos + 3];
        if (len < 2 || pos + 2 + len > n)
            return false;
        const uint8_t *seg = d + pos + 4;
        size_t seg_len = len - 2;
        if (marker == 0xE1 && seg_len > 14 && seg[0] == 'E' && seg[1] == 'x' && seg[2] == 'i' && seg[3] == 'f' &&
            seg[4] == 0 && seg[5] == 0) {
            const uint8_t *t = seg + 6;
            size_t tl = seg_len - 6;
            bool le;
            if (t[0] == 'I' && t[1] == 'I')
                le = true;
            else if (t[0] == 'M' && t[1] == 'M')
                le = false;
            else
                return false;
            uint32_t ifd0 = rd(t + 4, le, 4);
            if (ifd0 + 2 > tl)
                return false;
            uint32_t count0 = rd(t + ifd0, le, 2);
            size_t next_at = ifd0 + 2 + (size_t)count0 * 12;
            if (next_at + 4 > tl)
                return false;
            uint32_t ifd1 = rd(t + next_at, le, 4);
            if (ifd1 == 0 || ifd1 + 2 > tl)
                return false;
            uint32_t count1 = rd(t + ifd1, le, 2);
            uint32_t off = 0, size = 0;
            for (uint32_t i = 0; i < count1; i++) {
                size_t e = ifd1 + 2 + (size_t)i * 12;
                if (e + 12 > tl)
                    return false;
                uint32_t tag = rd(t + e, le, 2);
                if (tag == 0x0201)
                    off = rd(t + e + 8, le, 4);
                else if (tag == 0x0202)
                    size = rd(t + e + 8, le, 4);
            }
            if (size < 2 || (size_t)off + size > tl || t[off] != 0xFF || t[off + 1] != 0xD8)
                return false;
            thumb = std::string_view((const char *)t + off, size);
            return true;
        }
        pos += 2 + len;
    }
    return false;
}

}  // namespace album
