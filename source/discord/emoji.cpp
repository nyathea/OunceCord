// SPDX-License-Identifier: MIT
#include "discord/emoji.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include "util/util.h"

namespace discord::emoji {

namespace {

std::vector<Entry> g_entries;
std::vector<std::string> g_categories;
std::unordered_map<std::string, size_t> g_byName;
std::unordered_map<std::string, size_t> g_bySeq;  // sequence without FE0F and skin tones
size_t g_maxSeqCps = 1;

bool isSkinTone(uint32_t cp) { return cp >= 0x1F3FB && cp <= 0x1F3FF; }

void appendUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// The matching key: no variation selectors or skin tones.
std::string key(std::string_view s, size_t *cps = nullptr)
{
    std::string out;
    size_t i = 0, n = 0;
    while (i < s.size()) {
        uint32_t cp = utf8Next(s, i);
        if (cp == 0xFE0F || cp == 0xFE0E || isSkinTone(cp))
            continue;
        appendUtf8(out, cp);
        n++;
    }
    if (cps)
        *cps = n;
    return out;
}

std::vector<std::string_view> split(std::string_view s, char sep)
{
    std::vector<std::string_view> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t e = s.find(sep, start);
        if (e == std::string_view::npos)
            e = s.size();
        out.push_back(s.substr(start, e - start));
        start = e + 1;
    }
    return out;
}

}  // namespace

void load(std::string_view table)
{
    g_entries.clear();
    g_categories.clear();
    g_byName.clear();
    g_bySeq.clear();
    g_maxSeqCps = 1;
    for (std::string_view line : split(table, '\n')) {
        if (line.empty())
            continue;
        auto f = split(line, '\t');
        if (f[0] == "#category") {  // "#category TAB name", in order
            if (f.size() > 1)
                g_categories.emplace_back(f[1]);
            continue;
        }
        if (f.size() < 3)
            continue;
        Entry e;
        e.chars = std::string(f[0]);
        e.category = (uint8_t)std::atoi(std::string(f[1]).c_str());
        for (auto n : split(f[2], ','))
            if (!n.empty())
                e.names.emplace_back(n);
        if (f.size() > 3)
            e.tags = std::string(f[3]);
        size_t index = g_entries.size();
        for (const auto &n : e.names)
            g_byName.emplace(n, index);
        size_t cps = 0;
        std::string k = key(e.chars, &cps);
        if (!k.empty()) {
            g_bySeq.emplace(k, index);
            g_maxSeqCps = std::max(g_maxSeqCps, cps);
        }
        g_entries.push_back(std::move(e));
    }
}

bool loaded() { return !g_entries.empty(); }
const std::vector<Entry> &all() { return g_entries; }
const std::vector<std::string> &categoryNames() { return g_categories; }

const Entry *byName(std::string_view name)
{
    auto it = g_byName.find(std::string(name));
    return it == g_byName.end() ? nullptr : &g_entries[it->second];
}

size_t match(std::string_view s, size_t i, const Entry **entry)
{
    if (i >= s.size() || g_bySeq.empty())
        return 0;
    unsigned char c0 = (unsigned char)s[i];
    if (c0 < 0x80) {
        // Only keycaps (#, *, 0-9 followed by FE0F/20E3) start with ASCII.
        bool keycap = (c0 == '#' || c0 == '*' || (c0 >= '0' && c0 <= '9')) && i + 1 < s.size() &&
                      ((unsigned char)s[i + 1] == 0xEF || (unsigned char)s[i + 1] == 0xE2);
        if (!keycap)
            return 0;
    }
    // Walk up to the longest sequence, remembering where each key length
    // ends, then take the longest one in the table.
    std::string k;
    size_t at = i, cps = 0, best_len = 0;
    const Entry *best = nullptr;
    while (at < s.size() && cps < g_maxSeqCps + 1) {
        size_t start = at;
        uint32_t cp = utf8Next(s, at);
        if (cp == 0xFE0F || cp == 0xFE0E || isSkinTone(cp)) {
            if (cps == 0)
                return 0;
            if (best && best_len == start - i)
                best_len = at - i;  // belongs to the match so far
            continue;
        }
        appendUtf8(k, cp);
        cps++;
        auto it = g_bySeq.find(k);
        if (it != g_bySeq.end()) {
            best = &g_entries[it->second];
            best_len = at - i;
        }
        // Sequences only continue through a ZWJ, keycap, regional
        // indicator (flags) or tag; stop early otherwise.
        if (cps >= 1 && at < s.size()) {
            size_t peek = at;
            uint32_t next = utf8Next(s, peek);
            bool joins = cp == 0x200D || next == 0x200D || next == 0x20E3 || next == 0xFE0F || next == 0xFE0E ||
                         isSkinTone(next) || (next >= 0xE0020 && next <= 0xE007F) ||
                         (cp >= 0x1F1E6 && cp <= 0x1F1FF && next >= 0x1F1E6 && next <= 0x1F1FF && cps == 1) ||
                         (cp >= 0xE0020 && cp <= 0xE007F);
            if (!joins)
                break;
        }
    }
    if (best && entry)
        *entry = best;
    return best ? best_len : 0;
}

std::string shortcode(std::string_view chars)
{
    const Entry *e = nullptr;
    if (match(chars, 0, &e) && e && !e->names.empty())
        return ":" + e->names[0] + ":";
    return {};
}

static bool nameChar(char c)
{
    return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '+';
}

std::string replaceShortcodes(std::string_view text, const std::function<std::string(std::string_view)> &custom)
{
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        // Code is left as typed.
        if (text[i] == '`') {
            size_t n = 1;
            while (i + n < text.size() && text[i + n] == '`')
                n++;
            size_t end = text.find(std::string(n, '`'), i + n);
            end = end == std::string_view::npos ? text.size() : end + n;
            out.append(text.substr(i, end - i));
            i = end;
            continue;
        }
        if (text[i] == ':') {
            size_t j = i + 1;
            while (j < text.size() && j - i <= 40 && nameChar(text[j]))
                j++;
            if (j < text.size() && text[j] == ':' && j > i + 1) {
                std::string_view name = text.substr(i + 1, j - i - 1);
                std::string rep = custom ? custom(name) : std::string();
                if (rep.empty())
                    if (const Entry *e = byName(name))
                        rep = e->chars;
                if (!rep.empty()) {
                    size_t next = j + 1;
                    // :skin-tone-N: right after a standard emoji.
                    if (rep[0] != '<' && text.compare(next, 11, ":skin-tone-") == 0 && next + 13 <= text.size() &&
                        text[next + 11] >= '1' && text[next + 11] <= '5' && text[next + 12] == ':') {
                        std::string base;
                        size_t k = 0;
                        while (k < rep.size()) {  // the tone goes after the first emoji codepoint
                            uint32_t cp = utf8Next(rep, k);
                            if (cp == 0xFE0F)
                                continue;
                            appendUtf8(base, cp);
                            appendUtf8(base, 0x1F3FB + (text[next + 11] - '1'));
                            base.append(rep.substr(k));
                            break;
                        }
                        rep = base;
                        next += 13;
                    }
                    out += rep;
                    i = next;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

}  // namespace discord::emoji
