// SPDX-License-Identifier: MIT
#include "media/mp4.h"

#include <algorithm>
#include <cstring>

namespace mp4 {

namespace {

constexpr uint64_t kMaxMoov = 64u << 20;
constexpr size_t kMaxSamples = 4u << 20;

uint32_t fourcc(const char *s) { return (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | (uint8_t)s[3]; }

std::string fourccStr(uint32_t t)
{
    std::string s(4, ' ');
    for (int i = 0; i < 4; i++) {
        char c = (char)(t >> (24 - 8 * i));
        s[i] = c >= 32 && c < 127 ? c : '?';
    }
    return s;
}

// A bounds-checked cursor over a byte range.
struct Cursor {
    const uint8_t *p;
    size_t n, i = 0;
    bool ok = true;
    Cursor(const uint8_t *data, size_t size) : p(data), n(size) {}
    bool has(size_t k) { if (!ok || n - i < k || i > n) ok = false; return ok; }
    uint8_t u8() { if (!has(1)) return 0; return p[i++]; }
    uint16_t u16() { if (!has(2)) return 0; uint16_t v = (uint16_t)(p[i] << 8 | p[i + 1]); i += 2; return v; }
    uint32_t u24() { if (!has(3)) return 0; uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2]; i += 3; return v; }
    uint32_t u32() { if (!has(4)) return 0; uint32_t v = (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3]; i += 4; return v; }
    uint64_t u64() { uint64_t hi = u32(); return hi << 32 | u32(); }
    void skip(size_t k) { if (has(k)) i += k; }
    size_t left() const { return ok && i <= n ? n - i : 0; }
};

struct Box {
    uint32_t type = 0;
    const uint8_t *data = nullptr;  // payload
    size_t size = 0;
};

// Splits a payload into child boxes.
std::vector<Box> children(const uint8_t *data, size_t size)
{
    std::vector<Box> out;
    size_t i = 0;
    while (size - i >= 8) {
        uint64_t len = (uint32_t)(data[i] << 24 | data[i + 1] << 16 | data[i + 2] << 8 | data[i + 3]);
        uint32_t type = (uint32_t)(data[i + 4] << 24 | data[i + 5] << 16 | data[i + 6] << 8 | data[i + 7]);
        size_t hdr = 8;
        if (len == 1) {
            if (size - i < 16)
                break;
            len = 0;
            for (int k = 0; k < 8; k++)
                len = len << 8 | data[i + 8 + k];
            hdr = 16;
        } else if (len == 0) {
            len = size - i;
        }
        if (len < hdr || len > size - i)
            break;
        out.push_back({type, data + i + hdr, (size_t)(len - hdr)});
        i += (size_t)len;
    }
    return out;
}

const Box *find(const std::vector<Box> &boxes, const char *type)
{
    uint32_t t = fourcc(type);
    for (const auto &b : boxes)
        if (b.type == t)
            return &b;
    return nullptr;
}

// MPEG-4 descriptor length: up to four bytes, 7 bits each.
size_t descLen(Cursor &c)
{
    size_t len = 0;
    for (int k = 0; k < 4; k++) {
        uint8_t b = c.u8();
        len = len << 7 | (b & 0x7f);
        if (!(b & 0x80))
            break;
    }
    return len;
}

void parseEsds(const Box &esds, Track &t)
{
    Cursor c(esds.data, esds.size);
    c.skip(4);  // version, flags
    if (c.u8() != 3)
        return;
    descLen(c);
    c.skip(2);  // ES_ID
    uint8_t flags = c.u8();
    if (flags & 0x80)
        c.skip(2);
    if (flags & 0x40)
        c.skip(c.u8());
    if (flags & 0x20)
        c.skip(2);
    if (c.u8() != 4)
        return;
    descLen(c);
    t.object_type = c.u8();
    c.skip(1 + 3 + 4 + 4);
    if (c.u8() != 5)
        return;
    size_t len = descLen(c);
    if (!c.has(len))
        return;
    t.asc.assign(c.p + c.i, c.p + c.i + len);
}

// Finds esds among an audio sample entry's children (QuickTime nests it in 'wave').
void findEsds(const std::vector<Box> &boxes, Track &t)
{
    for (const auto &b : boxes) {
        if (b.type == fourcc("esds"))
            parseEsds(b, t);
        else if (b.type == fourcc("wave"))
            findEsds(children(b.data, b.size), t);
    }
}

void parseStsd(const Box &stsd, Track &t)
{
    Cursor c(stsd.data, stsd.size);
    c.skip(4);
    if (c.u32() == 0)
        return;
    auto entries = children(c.p + c.i, c.left());
    if (entries.empty())
        return;
    const Box &e = entries[0];
    t.codec = fourccStr(e.type);
    Cursor ec(e.data, e.size);
    if (t.kind == Track::Kind::Video) {
        ec.skip(24);
        t.width = ec.u16();
        t.height = ec.u16();
        if (e.size > 78) {
            auto kids = children(e.data + 78, e.size - 78);
            if (const Box *avcc = find(kids, "avcC"))
                t.avcc.assign(avcc->data, avcc->data + avcc->size);
        }
    } else if (t.kind == Track::Kind::Audio) {
        ec.skip(8);
        uint16_t version = ec.u16();
        ec.skip(6);
        t.channels = ec.u16();
        ec.skip(6);
        t.sample_rate = (int)(ec.u32() >> 16);
        size_t base = 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);
        if (e.size > base)
            findEsds(children(e.data + base, e.size - base), t);
    }
}

bool parseTrak(const Box &trak, uint32_t movie_timescale, Track &t, std::string &error)
{
    auto tk = children(trak.data, trak.size);
    const Box *mdia = find(tk, "mdia");
    if (!mdia)
        return false;
    auto md = children(mdia->data, mdia->size);
    const Box *mdhd = find(md, "mdhd"), *hdlr = find(md, "hdlr"), *minf = find(md, "minf");
    if (!mdhd || !hdlr || !minf)
        return false;
    {
        Cursor c(mdhd->data, mdhd->size);
        uint8_t v = c.u8();
        c.skip(3);
        c.skip(v == 1 ? 16 : 8);
        t.timescale = c.u32();
        if (!c.ok || !t.timescale)
            return false;
    }
    {
        Cursor c(hdlr->data, hdlr->size);
        c.skip(8);
        uint32_t h = c.u32();
        t.kind = h == fourcc("vide") ? Track::Kind::Video : h == fourcc("soun") ? Track::Kind::Audio : Track::Kind::Other;
    }
    if (t.kind == Track::Kind::Other)
        return true;
    auto mi = children(minf->data, minf->size);
    const Box *stbl = find(mi, "stbl");
    if (!stbl)
        return false;
    auto st = children(stbl->data, stbl->size);
    const Box *stsd = find(st, "stsd"), *stts = find(st, "stts"), *stsz = find(st, "stsz"), *stsc = find(st, "stsc");
    const Box *stco = find(st, "stco"), *co64 = find(st, "co64"), *stss = find(st, "stss"), *ctts = find(st, "ctts");
    if (!stsd || !stts || !stsz || !stsc || (!stco && !co64))
        return false;
    parseStsd(*stsd, t);

    // Sizes.
    std::vector<uint32_t> sizes;
    {
        Cursor c(stsz->data, stsz->size);
        c.skip(4);
        uint32_t fixed = c.u32(), count = c.u32();
        if (count > kMaxSamples) {
            error = "too many samples";
            return false;
        }
        sizes.resize(count, fixed);
        if (!fixed)
            for (auto &s : sizes)
                s = c.u32();
        if (!c.ok)
            return false;
    }
    // Chunk offsets.
    std::vector<uint64_t> chunks;
    {
        Cursor c(co64 ? co64->data : stco->data, co64 ? co64->size : stco->size);
        c.skip(4);
        uint32_t count = c.u32();
        if (count > kMaxSamples || !c.has((size_t)count * (co64 ? 8 : 4)))
            return false;
        chunks.resize(count);
        for (auto &o : chunks)
            o = co64 ? c.u64() : c.u32();
    }
    // Samples per chunk.
    t.samples.resize(sizes.size());
    {
        Cursor c(stsc->data, stsc->size);
        c.skip(4);
        uint32_t count = c.u32();
        struct Run { uint32_t first, per; };
        std::vector<Run> runs;
        for (uint32_t k = 0; k < count && c.ok; k++) {
            uint32_t first = c.u32(), per = c.u32();
            c.skip(4);
            runs.push_back({first, per});
        }
        if (!c.ok || runs.empty())
            return false;
        size_t s = 0;
        for (size_t r = 0; r < runs.size() && s < sizes.size(); r++) {
            uint32_t end = r + 1 < runs.size() ? runs[r + 1].first : (uint32_t)chunks.size() + 1;
            for (uint32_t ch = runs[r].first; ch < end && s < sizes.size(); ch++) {
                if (ch == 0 || ch > chunks.size())
                    return false;
                uint64_t off = chunks[ch - 1];
                for (uint32_t k = 0; k < runs[r].per && s < sizes.size(); k++) {
                    t.samples[s].offset = off;
                    t.samples[s].size = sizes[s];
                    off += sizes[s];
                    s++;
                }
            }
        }
        if (s != sizes.size())
            return false;
    }
    // Decode times.
    std::vector<int64_t> dts(sizes.size()), cto(sizes.size(), 0);
    int64_t end_time = 0;
    {
        Cursor c(stts->data, stts->size);
        c.skip(4);
        uint32_t count = c.u32();
        size_t s = 0;
        int64_t time = 0;
        for (uint32_t k = 0; k < count && c.ok; k++) {
            uint32_t n = c.u32(), delta = c.u32();
            for (uint32_t j = 0; j < n && s < dts.size(); j++, s++) {
                dts[s] = time;
                time += delta;
            }
        }
        for (; s < dts.size(); s++)
            dts[s] = time;
        end_time = time;
    }
    if (ctts) {
        Cursor c(ctts->data, ctts->size);
        c.skip(4);
        uint32_t count = c.u32();
        size_t s = 0;
        for (uint32_t k = 0; k < count && c.ok; k++) {
            uint32_t n = c.u32();
            int32_t off = (int32_t)c.u32();  // signed in version 1; small enough either way
            for (uint32_t j = 0; j < n && s < cto.size(); j++, s++)
                cto[s] = off;
        }
    }
    // Edit list: skip the media time of the first real edit, delay by empty ones.
    int64_t shift = 0, delay_us = 0;
    if (const Box *edts = find(tk, "edts")) {
        auto ed = children(edts->data, edts->size);
        if (const Box *elst = find(ed, "elst")) {
            Cursor c(elst->data, elst->size);
            uint8_t v = c.u8();
            c.skip(3);
            uint32_t count = c.u32();
            for (uint32_t k = 0; k < count && c.ok; k++) {
                int64_t dur = v == 1 ? (int64_t)c.u64() : c.u32();
                int64_t mt = v == 1 ? (int64_t)c.u64() : (int32_t)c.u32();
                c.skip(4);
                if (mt == -1) {
                    if (movie_timescale)
                        delay_us += dur * 1000000 / movie_timescale;
                } else {
                    shift = mt;
                    break;
                }
            }
        }
    }
    auto us = [&](int64_t v) { return v * 1000000 / (int64_t)t.timescale; };
    for (size_t s = 0; s < t.samples.size(); s++) {
        t.samples[s].dts_us = us(dts[s] - shift) + delay_us;
        t.samples[s].pts_us = us(dts[s] + cto[s] - shift) + delay_us;
    }
    if (stss) {
        Cursor c(stss->data, stss->size);
        c.skip(4);
        uint32_t count = c.u32();
        for (uint32_t k = 0; k < count && c.ok; k++) {
            uint32_t n = c.u32();
            if (n >= 1 && n <= t.samples.size())
                t.samples[n - 1].key = true;
        }
    } else {
        for (auto &s : t.samples)
            s.key = true;
    }
    t.duration_us = us(end_time - shift) + delay_us;
    return true;
}

}  // namespace

bool parse(const Reader &read, uint64_t file_size, File &out, std::string &error)
{
    out = File{};
    // Walk the top level for moov (it may come after mdat).
    uint64_t pos = 0;
    std::vector<uint8_t> moov;
    bool fragmented = false;
    while (pos + 8 <= file_size) {
        uint8_t hdr[16];
        if (!read(pos, hdr, 8)) {
            error = "read failed";
            return false;
        }
        uint64_t len = (uint32_t)(hdr[0] << 24 | hdr[1] << 16 | hdr[2] << 8 | hdr[3]);
        uint32_t type = (uint32_t)(hdr[4] << 24 | hdr[5] << 16 | hdr[6] << 8 | hdr[7]);
        uint64_t head = 8;
        if (len == 1) {
            if (pos + 16 > file_size || !read(pos + 8, hdr + 8, 8)) {
                error = "read failed";
                return false;
            }
            len = 0;
            for (int k = 0; k < 8; k++)
                len = len << 8 | hdr[8 + k];
            head = 16;
        } else if (len == 0) {
            len = file_size - pos;
        }
        if (len < head || len > file_size - pos)
            break;
        if (type == fourcc("moov")) {
            if (len - head > kMaxMoov) {
                error = "header too large";
                return false;
            }
            moov.resize((size_t)(len - head));
            if (!read(pos + head, moov.data(), moov.size())) {
                error = "read failed";
                return false;
            }
        } else if (type == fourcc("moof")) {
            fragmented = true;
        }
        pos += len;
    }
    if (moov.empty()) {
        error = fragmented ? "fragmented MP4 isn't supported" : "not an MP4 video";
        return false;
    }
    auto boxes = children(moov.data(), moov.size());
    uint32_t movie_timescale = 0;
    if (const Box *mvhd = find(boxes, "mvhd")) {
        Cursor c(mvhd->data, mvhd->size);
        uint8_t v = c.u8();
        c.skip(3 + (v == 1 ? 16 : 8));
        movie_timescale = c.u32();
    }
    for (const auto &b : boxes) {
        if (b.type != fourcc("trak"))
            continue;
        Track t;
        if (!parseTrak(b, movie_timescale, t, error) || t.kind == Track::Kind::Other || t.samples.empty())
            continue;
        out.tracks.push_back(std::move(t));
        int idx = (int)out.tracks.size() - 1;
        Track &added = out.tracks.back();
        if (added.kind == Track::Kind::Video && out.video < 0)
            out.video = idx;
        if (added.kind == Track::Kind::Audio && out.audio < 0)
            out.audio = idx;
        out.duration_us = std::max(out.duration_us, added.duration_us);
    }
    if (out.video < 0 && out.audio < 0) {
        if (error.empty())
            error = "no playable tracks";
        return false;
    }
    error.clear();
    return true;
}

bool avccToAnnexB(const std::vector<uint8_t> &avcc, std::string &out, int &nal_length_size)
{
    out.clear();
    Cursor c(avcc.data(), avcc.size());
    c.skip(4);  // version, profile, compatibility, level
    nal_length_size = (c.u8() & 3) + 1;
    for (int set = 0; set < 2; set++) {
        int count = set == 0 ? (c.u8() & 0x1f) : c.u8();
        for (int k = 0; k < count; k++) {
            uint16_t len = c.u16();
            if (!c.has(len))
                return false;
            out.append("\0\0\0\1", 4);
            out.append((const char *)c.p + c.i, len);
            c.skip(len);
        }
    }
    return c.ok && !out.empty();
}

bool sampleToAnnexB(const uint8_t *data, size_t size, int nal_length_size, std::string &out)
{
    out.clear();
    size_t i = 0;
    while (i < size) {
        if (size - i < (size_t)nal_length_size)
            return false;
        size_t len = 0;
        for (int k = 0; k < nal_length_size; k++)
            len = len << 8 | data[i + k];
        i += nal_length_size;
        if (len > size - i)
            return false;
        out.append("\0\0\0\1", 4);
        out.append((const char *)data + i, len);
        i += len;
    }
    return true;
}

}  // namespace mp4
