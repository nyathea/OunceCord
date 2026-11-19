// SPDX-License-Identifier: MIT
#include "discord/emotes.h"

#include <cctype>

#include "discord/emoji.h"
#include "discord/reactions.h"

namespace discord {

static void appendUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// Parses an emote token at the start of s. Returns its length, or 0.
static size_t parseEmote(std::string_view s, Emote &e)
{
    size_t i = 0;
    if (s.size() < 6 || s[i++] != '<')
        return 0;
    e.animated = false;
    if (s[i] == 'a') {
        e.animated = true;
        i++;
    }
    if (i >= s.size() || s[i++] != ':')
        return 0;
    size_t name_start = i;
    while (i < s.size() && i - name_start <= 32 &&
           (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-' || s[i] == '~'))
        i++;
    size_t name_len = i - name_start;
    if (name_len < 1 || name_len > 32 || i >= s.size() || s[i++] != ':')
        return 0;
    size_t id_start = i;
    uint64_t id = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - id_start < 20)
        id = id * 10 + (uint64_t)(s[i++] - '0');
    if (i == id_start || i >= s.size() || s[i] != '>' || id == 0)
        return 0;
    e.name = std::string(s.substr(name_start, name_len));
    e.id = id;
    return i + 1;
}

// Decodes one UTF-8 sequence at s[i] (lenient), advancing i.
static uint32_t nextCp(std::string_view s, size_t &i)
{
    unsigned char c = (unsigned char)s[i++];
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = extra == 3 ? c & 0x07 : extra == 2 ? c & 0x0F : extra == 1 ? c & 0x1F : c;
    for (int k = 0; k < extra && i < s.size(); k++)
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

EmoteText extractEmotes(std::string_view content, uint32_t first_cp, uint32_t last_cp)
{
    EmoteText out;
    out.text.reserve(content.size());
    bool only_emotes = true;
    size_t i = 0;
    while (i < content.size()) {
        if (content[i] == '<' && out.emotes.size() < kMaxEmotes) {
            Emote e;
            if (size_t n = parseEmote(content.substr(i), e)) {
                appendUtf8(out.text, first_cp + (uint32_t)out.emotes.size());
                out.emotes.push_back(std::move(e));
                i += n;
                continue;
            }
        }
        // Standard emoji (only non-ASCII bytes and keycaps can start one).
        unsigned char c = (unsigned char)content[i];
        if ((c >= 0x80 || c == '#' || c == '*' || (c >= '0' && c <= '9')) && out.emotes.size() < kMaxEmotes) {
            if (size_t n = emoji::match(content, i)) {
                Emote e;
                e.unicode = std::string(content.substr(i, n));
                appendUtf8(out.text, first_cp + (uint32_t)out.emotes.size());
                out.emotes.push_back(std::move(e));
                i += n;
                continue;
            }
        }
        size_t start = i;
        uint32_t cp = nextCp(content, i);
        if (cp >= first_cp && cp <= last_cp) {
            appendUtf8(out.text, 0xFFFD);  // someone typed our marker
            only_emotes = false;
            continue;
        }
        if (cp != ' ' && cp != '\n' && cp != '\t' && cp != '\r')
            only_emotes = false;
        out.text.append(content.substr(start, i - start));
    }
    out.jumbo = only_emotes && !out.emotes.empty() && out.emotes.size() <= kMaxJumbo;
    return out;
}

std::string emotesAsText(std::string_view content)
{
    std::string out;
    size_t i = 0;
    while (i < content.size()) {
        Emote e;
        if (content[i] == '<') {
            if (size_t n = parseEmote(content.substr(i), e)) {
                out += ":" + e.name + ":";
                i += n;
                continue;
            }
        }
        const emoji::Entry *entry = nullptr;
        if ((unsigned char)content[i] >= 0x80) {
            if (size_t n = emoji::match(content, i, &entry); n && entry && !entry->names.empty()) {
                out += ":" + entry->names[0] + ":";
                i += n;
                continue;
            }
        }
        out.push_back(content[i++]);
    }
    return out;
}

std::string emotePath(const Emote &e, int size)
{
    if (!e.unicode.empty()) {
        std::string name = twemojiName(e.unicode);
        return name.empty() ? std::string() : "https://cdn.jsdelivr.net/gh/jdecked/twemoji@15.1.0/assets/72x72/" + name + ".png";
    }
    return "/emojis/" + std::to_string(e.id) + (e.animated ? ".gif" : ".png") + "?size=" + std::to_string(size) +
           "&quality=lossless";
}

}  // namespace discord
