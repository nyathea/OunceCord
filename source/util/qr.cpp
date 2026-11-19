// SPDX-License-Identifier: MIT
//
// Follows the structure of Project Nayuki's reference QR generator: build
// the data codewords, add Reed-Solomon error correction per block,
// interleave, place them around the function patterns, then try all eight
// masks and keep the one with the lowest penalty.
#include "util/qr.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace qr {
namespace {

constexpr int kMaxVersion = 10;
// Level M, index = version.
constexpr int kEccPerBlock[kMaxVersion + 1] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
constexpr int kNumBlocks[kMaxVersion + 1] = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
constexpr int kEclFormatBits = 0;  // M

int rawDataModules(int ver)
{
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int num_align = ver / 7 + 2;
        result -= (25 * num_align - 10) * num_align - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

int dataCodewords(int ver)
{
    return rawDataModules(ver) / 8 - kEccPerBlock[ver] * kNumBlocks[ver];
}

uint8_t gfMul(uint8_t x, uint8_t y)
{
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

std::vector<uint8_t> rsDivisor(int degree)
{
    std::vector<uint8_t> result(degree, 0);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            result[j] = gfMul(result[j], root);
            if (j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = gfMul(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> rsRemainder(const std::vector<uint8_t> &data, const std::vector<uint8_t> &divisor)
{
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); i++)
            result[i] ^= gfMul(divisor[i], factor);
    }
    return result;
}

struct Builder {
    int ver, size;
    std::vector<bool> dark, func;

    explicit Builder(int v) : ver(v), size(v * 4 + 17), dark(size * size, false), func(size * size, false) {}

    void set(int x, int y, bool d)
    {
        dark[(size_t)y * size + x] = d;
        func[(size_t)y * size + x] = true;
    }

    std::vector<int> alignmentPositions() const
    {
        if (ver == 1)
            return {};
        int num = ver / 7 + 2;
        int step = (ver * 8 + num * 3 + 5) / (num * 4 - 4) * 2;
        std::vector<int> result;
        for (int i = 0, pos = size - 7; i < num - 1; i++, pos -= step)
            result.insert(result.begin(), pos);
        result.insert(result.begin(), 6);
        return result;
    }

    void drawFinder(int cx, int cy)
    {
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || x >= size || y < 0 || y >= size)
                    continue;
                int dist = std::max(std::abs(dx), std::abs(dy));
                set(x, y, dist != 2 && dist != 4);
            }
    }

    void drawAlignment(int cx, int cy)
    {
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                set(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }

    void drawFormat(int mask)
    {
        int data = kEclFormatBits << 3 | mask;
        int rem = data;
        for (int i = 0; i < 10; i++)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412;
        auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
        for (int i = 0; i <= 5; i++)
            set(8, i, bit(i));
        set(8, 7, bit(6));
        set(8, 8, bit(7));
        set(7, 8, bit(8));
        for (int i = 9; i < 15; i++)
            set(14 - i, 8, bit(i));
        for (int i = 0; i < 8; i++)
            set(size - 1 - i, 8, bit(i));
        for (int i = 8; i < 15; i++)
            set(8, size - 15 + i, bit(i));
        set(8, size - 8, true);  // the dark module
    }

    void drawVersion()
    {
        if (ver < 7)
            return;
        int rem = ver;
        for (int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = (long)ver << 12 | rem;
        for (int i = 0; i < 18; i++) {
            bool b = ((bits >> i) & 1) != 0;
            int a = size - 11 + i % 3, c = i / 3;
            set(a, c, b);
            set(c, a, b);
        }
    }

    void drawFunctionPatterns()
    {
        for (int i = 0; i < size; i++) {
            set(6, i, i % 2 == 0);
            set(i, 6, i % 2 == 0);
        }
        drawFinder(3, 3);
        drawFinder(size - 4, 3);
        drawFinder(3, size - 4);
        std::vector<int> pos = alignmentPositions();
        int n = (int)pos.size();
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
                    continue;
                drawAlignment(pos[i], pos[j]);
            }
        drawFormat(0);  // reserved; redrawn with the real mask
        drawVersion();
    }

    void drawCodewords(const std::vector<uint8_t> &data)
    {
        size_t i = 0;
        for (int right = size - 1; right >= 1; right -= 2) {
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < size; vert++) {
                for (int j = 0; j < 2; j++) {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? size - 1 - vert : vert;
                    size_t idx = (size_t)y * size + x;
                    if (!func[idx] && i < data.size() * 8) {
                        dark[idx] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                        i++;
                    }
                }
            }
        }
    }

    void applyMask(int mask)
    {
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++) {
                bool invert;
                switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                default: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                }
                size_t idx = (size_t)y * size + x;
                if (!func[idx] && invert)
                    dark[idx] = !dark[idx];
            }
    }

    bool at(int x, int y) const { return dark[(size_t)y * size + x]; }

    // The standard's four penalty rules.
    long penalty() const
    {
        long total = 0;
        for (int pass = 0; pass < 2; pass++) {
            for (int a = 0; a < size; a++) {
                int run = 0;
                bool color = false;
                for (int b = 0; b < size; b++) {
                    bool c = pass == 0 ? at(b, a) : at(a, b);
                    if (b == 0 || c != color) {
                        if (run >= 5)
                            total += 3 + (run - 5);
                        color = c;
                        run = 1;
                    } else {
                        run++;
                    }
                }
                if (run >= 5)
                    total += 3 + (run - 5);
                // Finder-like 1:1:3:1:1 with four light modules on a side.
                for (int b = 0; b + 11 <= size; b++) {
                    auto m = [&](int k) { return pass == 0 ? at(b + k, a) : at(a, b + k); };
                    static const bool p1[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
                    static const bool p2[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
                    bool m1 = true, m2 = true;
                    for (int k = 0; k < 11; k++) {
                        bool v = m(k);
                        m1 = m1 && v == p1[k];
                        m2 = m2 && v == p2[k];
                    }
                    total += (m1 ? 40 : 0) + (m2 ? 40 : 0);
                }
            }
        }
        for (int y = 0; y + 1 < size; y++)
            for (int x = 0; x + 1 < size; x++) {
                bool c = at(x, y);
                if (c == at(x + 1, y) && c == at(x, y + 1) && c == at(x + 1, y + 1))
                    total += 3;
            }
        long dark_count = std::count(dark.begin(), dark.end(), true);
        long all = (long)size * size;
        long k = (std::abs(dark_count * 20 - all * 10) + all - 1) / all - 1;
        total += std::max(0L, k) * 10;
        return total;
    }
};

}  // namespace

Code encode(std::string_view text)
{
    int ver = 1;
    for (; ver <= kMaxVersion; ver++) {
        int count_bits = ver < 10 ? 8 : 16;
        if (4 + count_bits + (int)text.size() * 8 <= dataCodewords(ver) * 8)
            break;
    }
    if (ver > kMaxVersion)
        return {};

    // Data bits: byte mode, length, bytes, terminator, padding.
    std::vector<bool> bits;
    auto append = [&bits](uint32_t val, int len) {
        for (int i = len - 1; i >= 0; i--)
            bits.push_back(((val >> i) & 1) != 0);
    };
    append(0x4, 4);
    append((uint32_t)text.size(), ver < 10 ? 8 : 16);
    for (unsigned char c : text)
        append(c, 8);
    size_t capacity = (size_t)dataCodewords(ver) * 8;
    append(0, (int)std::min<size_t>(4, capacity - bits.size()));
    append(0, (int)((8 - bits.size() % 8) % 8));
    for (uint8_t pad = 0xEC; bits.size() < capacity; pad ^= 0xEC ^ 0x11)
        append(pad, 8);
    std::vector<uint8_t> data(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); i++)
        data[i >> 3] |= (uint8_t)(bits[i] << (7 - (i & 7)));

    // Error correction and interleaving.
    int num_blocks = kNumBlocks[ver], ecc_len = kEccPerBlock[ver];
    int raw_codewords = rawDataModules(ver) / 8;
    int num_short = num_blocks - raw_codewords % num_blocks;
    int short_len = raw_codewords / num_blocks;
    std::vector<uint8_t> divisor = rsDivisor(ecc_len);
    std::vector<std::vector<uint8_t>> blocks;
    for (int i = 0, k = 0; i < num_blocks; i++) {
        int len = short_len - ecc_len + (i < num_short ? 0 : 1);
        std::vector<uint8_t> dat(data.begin() + k, data.begin() + k + len);
        k += len;
        std::vector<uint8_t> ecc = rsRemainder(dat, divisor);
        if (i < num_short)
            dat.push_back(0);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(dat));
    }
    std::vector<uint8_t> codewords;
    for (size_t i = 0; i < blocks[0].size(); i++)
        for (int j = 0; j < num_blocks; j++)
            if (i != (size_t)(short_len - ecc_len) || j >= num_short)
                codewords.push_back(blocks[j][i]);

    Builder base(ver);
    base.drawFunctionPatterns();
    base.drawCodewords(codewords);

    long best_penalty = -1;
    Builder best = base;
    for (int mask = 0; mask < 8; mask++) {
        Builder b = base;
        b.applyMask(mask);
        b.drawFormat(mask);
        long p = b.penalty();
        if (best_penalty < 0 || p < best_penalty) {
            best_penalty = p;
            best = std::move(b);
        }
    }
    Code out;
    out.size = best.size;
    out.modules = std::move(best.dark);
    return out;
}

}  // namespace qr
