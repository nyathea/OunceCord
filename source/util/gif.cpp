// SPDX-License-Identifier: MIT
#include "util/gif.h"

#include <algorithm>
#include <cstring>

namespace gif {
namespace {

struct Reader {
    const uint8_t *p, *end;

    bool has(size_t n) const { return (size_t)(end - p) >= n; }
    uint8_t u8() { return has(1) ? *p++ : 0; }
    uint16_t u16()
    {
        uint16_t lo = u8();
        return (uint16_t)(lo | (u8() << 8));
    }
    // Skips a chain of data sub-blocks.
    void skipBlocks()
    {
        while (has(1)) {
            uint8_t n = u8();
            if (n == 0 || !has(n)) {
                if (n)
                    p = end;
                return;
            }
            p += n;
        }
    }
};

// Decodes LZW image data (concatenated sub-blocks) into palette indices.
// Missing data leaves the rest of the frame as index 0... except we track
// how many pixels were produced, and the caller treats the rest as untouched.
size_t lzwDecode(Reader &r, int min_code_size, uint8_t *out, size_t out_len)
{
    if (min_code_size < 2 || min_code_size > 8)
        return 0;
    // Gather the sub-blocks into one buffer.
    std::vector<uint8_t> data;
    while (r.has(1)) {
        uint8_t n = r.u8();
        if (n == 0)
            break;
        if (!r.has(n)) {
            data.insert(data.end(), r.p, r.end);
            r.p = r.end;
            break;
        }
        data.insert(data.end(), r.p, r.p + n);
        r.p += n;
    }

    const int clear = 1 << min_code_size, eoi = clear + 1;
    static thread_local uint16_t prefix[4096];
    static thread_local uint8_t suffix[4096];
    static thread_local uint8_t first[4096];  // first byte of each code's string
    static thread_local uint8_t stack[4097];
    for (int i = 0; i < clear; i++) {
        prefix[i] = 0xFFFF;
        suffix[i] = (uint8_t)i;
        first[i] = (uint8_t)i;
    }

    int code_size = min_code_size + 1, next = eoi + 1, prev = -1;
    uint32_t bits = 0;
    int nbits = 0;
    size_t pos = 0, written = 0;
    while (written < out_len) {
        while (nbits < code_size && pos < data.size()) {
            bits |= (uint32_t)data[pos++] << nbits;
            nbits += 8;
        }
        if (nbits < code_size)
            break;
        int code = (int)(bits & ((1u << code_size) - 1));
        bits >>= code_size;
        nbits -= code_size;

        if (code == clear) {
            code_size = min_code_size + 1;
            next = eoi + 1;
            prev = -1;
            continue;
        }
        if (code == eoi)
            break;

        int sp = 0, cur;
        if (prev < 0) {
            if (code >= clear)
                break;  // corrupt: first code after clear must be a literal
            out[written++] = (uint8_t)code;
            prev = code;
            continue;
        }
        if (code < next) {
            cur = code;
        } else if (code == next) {
            // KwKwK case: the string is prev's string plus its first byte.
            stack[sp++] = first[prev];
            cur = prev;
        } else {
            break;  // corrupt
        }
        while (cur != 0xFFFF && cur >= 0 && sp < 4096) {
            stack[sp++] = suffix[cur];
            cur = prefix[cur];
        }
        uint8_t first_byte = stack[sp - 1];
        while (sp > 0 && written < out_len)
            out[written++] = stack[--sp];

        if (next < 4096) {
            prefix[next] = (uint16_t)prev;
            suffix[next] = first_byte;
            first[next] = first[prev];
            next++;
            if (next == (1 << code_size) && code_size < 12)
                code_size++;
        }
        prev = code;
    }
    return written;
}

}  // namespace

bool decode(std::string_view data, Image &out, size_t max_bytes, int max_side)
{
    out = Image();
    Reader r{(const uint8_t *)data.data(), (const uint8_t *)data.data() + data.size()};
    if (!r.has(13) || (memcmp(r.p, "GIF87a", 6) != 0 && memcmp(r.p, "GIF89a", 6) != 0))
        return false;
    r.p += 6;
    int w = r.u16(), h = r.u16();
    uint8_t packed = r.u8();
    r.u8();  // background colour index: browsers use transparent, and so do we
    r.u8();  // aspect ratio
    if (w <= 0 || h <= 0 || w > max_side || h > max_side)
        return false;
    const size_t frame_bytes = (size_t)w * h * 4;

    uint8_t global[256 * 3] = {};
    int global_size = 0;
    if (packed & 0x80) {
        global_size = 2 << (packed & 7);
        if (!r.has((size_t)global_size * 3))
            return false;
        memcpy(global, r.p, (size_t)global_size * 3);
        r.p += global_size * 3;
    }

    std::vector<uint8_t> canvas(frame_bytes, 0), saved;
    std::vector<uint8_t> indices;
    int delay = 0, transparent = -1, disposal = 0;
    // The previous frame's disposal, applied before drawing the next one.
    int prev_disposal = 0, px = 0, py = 0, pw = 0, ph = 0;

    out.width = w;
    out.height = h;
    while (r.has(1)) {
        uint8_t block = r.u8();
        if (block == 0x3B)  // trailer
            break;
        if (block == 0x21) {
            uint8_t label = r.u8();
            if (label == 0xF9 && r.has(6) && r.p[0] == 4) {
                r.u8();
                uint8_t gp = r.u8();
                delay = r.u16();
                int ti = r.u8();
                disposal = (gp >> 2) & 7;
                transparent = (gp & 1) ? ti : -1;
            }
            r.skipBlocks();
            continue;
        }
        if (block != 0x2C)
            break;  // unknown block: stop with what we have

        int fx = r.u16(), fy = r.u16(), fw = r.u16(), fh = r.u16();
        uint8_t fp = r.u8();
        uint8_t local[256 * 3];
        const uint8_t *palette = global;
        int palette_size = global_size;
        if (fp & 0x80) {
            palette_size = 2 << (fp & 7);
            if (!r.has((size_t)palette_size * 3))
                break;
            memcpy(local, r.p, (size_t)palette_size * 3);
            r.p += palette_size * 3;
            palette = local;
        }
        bool interlaced = (fp & 0x40) != 0;
        int min_code = r.u8();

        // Pixel indices for the frame rectangle.
        indices.assign((size_t)fw * fh, 0);
        size_t got = fw && fh ? lzwDecode(r, min_code, indices.data(), indices.size()) : (r.skipBlocks(), 0);

        if (out.frames.size() * frame_bytes + frame_bytes > max_bytes) {
            out.truncated = true;
            break;
        }

        // Dispose of the previous frame.
        if (prev_disposal == 2) {
            for (int y = py; y < py + ph && y < h; y++)
                for (int x = px; x < px + pw && x < w; x++)
                    memset(&canvas[((size_t)y * w + x) * 4], 0, 4);
        } else if (prev_disposal == 3 && !saved.empty()) {
            canvas = saved;
        }
        if (disposal == 3)
            saved = canvas;

        // Draw, in interlaced row order if needed.
        static const int kStart[4] = {0, 4, 2, 1}, kStep[4] = {8, 8, 4, 2};
        size_t i = 0;
        for (int pass = 0, row_index = 0; pass < (interlaced ? 4 : 1); pass++) {
            int start = interlaced ? kStart[pass] : 0, step = interlaced ? kStep[pass] : 1;
            for (int row = start; row < fh; row += step, row_index++) {
                int y = fy + row;
                for (int col = 0; col < fw; col++, i++) {
                    if (i >= got)
                        continue;  // truncated data: leave the canvas as it was
                    int x = fx + col;
                    int idx = indices[i];
                    if (idx == transparent || x >= w || y >= h || idx >= palette_size)
                        continue;
                    uint8_t *px_out = &canvas[((size_t)y * w + x) * 4];
                    px_out[0] = palette[idx * 3];
                    px_out[1] = palette[idx * 3 + 1];
                    px_out[2] = palette[idx * 3 + 2];
                    px_out[3] = 255;
                }
            }
            (void)row_index;
        }

        Frame f;
        f.rgba = canvas;
        // Like browsers: very short delays play at 10 fps.
        f.delay_ms = delay <= 1 ? 100 : delay * 10;
        out.frames.push_back(std::move(f));

        prev_disposal = disposal;
        px = fx;
        py = fy;
        pw = fw;
        ph = fh;
        delay = 0;
        transparent = -1;
        disposal = 0;
    }
    return !out.frames.empty();
}

}  // namespace gif
