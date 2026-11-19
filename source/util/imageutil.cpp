// SPDX-License-Identifier: MIT
#include "util/imageutil.h"

#include <cctype>

namespace imageutil {

std::string sniffExtension(std::string_view d)
{
    auto at = [&](size_t i) { return i < d.size() ? (unsigned char)d[i] : 0; };
    if (at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF)
        return "jpg";
    if (d.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8))
        return "png";
    if (d.substr(0, 6) == "GIF87a" || d.substr(0, 6) == "GIF89a")
        return "gif";
    if (d.size() >= 12 && d.substr(0, 4) == "RIFF" && d.substr(8, 4) == "WEBP")
        return "webp";
    return "";
}

std::string contentTypeForName(std::string_view name)
{
    size_t dot = name.rfind('.');
    if (dot == std::string_view::npos)
        return "";
    std::string ext;
    for (char c : name.substr(dot + 1))
        ext.push_back((char)tolower((unsigned char)c));
    if (ext == "jpg" || ext == "jpeg")
        return "image/jpeg";
    if (ext == "png")
        return "image/png";
    if (ext == "gif")
        return "image/gif";
    if (ext == "webp")
        return "image/webp";
    if (ext == "mp4" || ext == "m4v")
        return "video/mp4";
    if (ext == "mov")
        return "video/quicktime";
    return "";
}

std::string withExtension(std::string_view name, std::string_view ext)
{
    size_t dot = name.rfind('.');
    size_t slash = name.find_last_of('/');
    std::string base(dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash) && dot > 0
                         ? name.substr(0, dot)
                         : name);
    return base + "." + std::string(ext);
}

bool shrink(std::vector<uint8_t> &rgba, int &w, int &h, int max_side)
{
    int longest = w > h ? w : h;
    if (w <= 0 || h <= 0 || longest <= max_side || rgba.size() < (size_t)w * h * 4)
        return false;
    int k = (longest + max_side - 1) / max_side;
    int nw = w / k, nh = h / k;
    if (nw < 1)
        nw = 1;
    if (nh < 1)
        nh = 1;
    std::vector<uint8_t> out((size_t)nw * nh * 4);
    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            uint32_t sum[4] = {0, 0, 0, 0};
            int n = 0;
            for (int dy = 0; dy < k && y * k + dy < h; dy++) {
                const uint8_t *row = &rgba[((size_t)(y * k + dy) * w + (size_t)x * k) * 4];
                for (int dx = 0; dx < k && x * k + dx < w; dx++, n++)
                    for (int c = 0; c < 4; c++)
                        sum[c] += row[dx * 4 + c];
            }
            uint8_t *o = &out[((size_t)y * nw + x) * 4];
            for (int c = 0; c < 4; c++)
                o[c] = (uint8_t)(sum[c] / (n ? n : 1));
        }
    }
    rgba = std::move(out);
    w = nw;
    h = nh;
    return true;
}

}  // namespace imageutil
