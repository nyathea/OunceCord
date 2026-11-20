// SPDX-License-Identifier: MIT
#include "discord/mentions.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace discord {
namespace {

void appendUtf8(std::string &out, uint32_t cp)
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

// Reads up to 20 digits at s[i]; 0 when there are none.
uint64_t readId(std::string_view s, size_t &i)
{
    size_t start = i;
    uint64_t v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - start < 20)
        v = v * 10 + (uint64_t)(s[i++] - '0');
    return i == start ? 0 : v;
}

// Parses a token starting with '<' at s[0]. Returns its length (0 if none)
// and fills span and the text to show.
size_t parseToken(std::string_view s, const MentionLookup &lk, MentionSpan &span, std::string &shown)
{
    if (s.size() < 3 || s[0] != '<')
        return 0;
    size_t i = 1;
    if (s[i] == '@') {
        i++;
        bool role = false;
        if (i < s.size() && s[i] == '!')
            i++;
        else if (i < s.size() && s[i] == '&') {
            role = true;
            i++;
        }
        uint64_t id = readId(s, i);
        if (!id || i >= s.size() || s[i] != '>')
            return 0;
        span.id = id;
        std::string name;
        if (role) {
            span.kind = MentionKind::Role;
            uint32_t color = 0;
            if (lk.role && lk.role(id, name, color))
                span.color = color;
            else
                name = "deleted-role";
        } else {
            span.kind = MentionKind::User;
            if (!lk.user || !lk.user(id, name) || name.empty())
                name = "unknown-user";
        }
        shown = "@" + name;
        return i + 1;
    }
    if (s[i] == '#') {
        i++;
        uint64_t id = readId(s, i);
        if (!id || i >= s.size() || s[i] != '>')
            return 0;
        span.kind = MentionKind::Channel;
        span.id = id;
        std::string name;
        if (!lk.channel || !lk.channel(id, name) || name.empty())
            name = "unknown";
        shown = "#" + name;
        return i + 1;
    }
    if (s[i] == 't' && i + 1 < s.size() && s[i + 1] == ':') {
        i += 2;
        bool neg = i < s.size() && s[i] == '-';
        if (neg)
            i++;
        size_t start = i;
        int64_t v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - start < 15)
            v = v * 10 + (s[i++] - '0');
        if (i == start)
            return 0;
        char style = 'f';
        if (i < s.size() && s[i] == ':') {
            if (i + 2 >= s.size())
                return 0;
            style = s[i + 1];
            if (std::string_view("tTdDfFR").find(style) == std::string_view::npos)
                return 0;
            i += 2;
        }
        if (i >= s.size() || s[i] != '>')
            return 0;
        span.kind = MentionKind::Time;
        shown = lk.time ? lk.time(neg ? -v : v, style) : std::to_string(neg ? -v : v);
        return i + 1;
    }
    if (s[i] == '/') {
        // </name:id>, where name may have spaces (subcommands).
        i++;
        size_t start = i;
        while (i < s.size() && i - start <= 100 && s[i] != ':' && s[i] != '>' && s[i] != '<' && s[i] != '\n')
            i++;
        if (i == start || i >= s.size() || s[i] != ':')
            return 0;
        std::string_view name = s.substr(start, i - start);
        i++;
        uint64_t id = readId(s, i);
        if (!id || i >= s.size() || s[i] != '>')
            return 0;
        span.kind = MentionKind::Command;
        span.id = id;
        shown = "/" + std::string(name);
        return i + 1;
    }
    return 0;
}

bool wordBoundary(std::string_view s, size_t at)
{
    if (at >= s.size())
        return true;
    char c = s[at];
    return !(isalnum((unsigned char)c) || c == '_');
}

}  // namespace

MentionText resolveMentions(std::string_view content, const MentionLookup &lookup, bool marks)
{
    MentionText out;
    out.text.reserve(content.size() + 16);
    auto emit = [&](const MentionSpan &span, const std::string &shown) {
        if (!marks || out.spans.size() >= kMaxMentionSpans) {
            out.text += shown;
            return;
        }
        appendUtf8(out.text, kMentionMarkFirst + (uint32_t)out.spans.size());
        // Names are user-chosen: they mustn't contain markers either.
        for (size_t k = 0; k < shown.size();) {
            unsigned char c = (unsigned char)shown[k];
            if (c == 0xF4 && k + 3 < shown.size()) {
                out.text += "\xEF\xBF\xBD";
                k += 4;
            } else {
                out.text.push_back((char)c);
                k++;
            }
        }
        appendUtf8(out.text, kMentionMarkEnd);
        out.spans.push_back(span);
    };
    size_t i = 0;
    while (i < content.size()) {
        char c = content[i];
        if (c == '<') {
            MentionSpan span{};
            std::string shown;
            if (size_t n = parseToken(content.substr(i), lookup, span, shown)) {
                emit(span, shown);
                i += n;
                continue;
            }
        } else if (c == '@' && (i == 0 || wordBoundary(content, i - 1))) {
            for (std::string_view word : {std::string_view("everyone"), std::string_view("here")}) {
                if (content.substr(i + 1, word.size()) == word && wordBoundary(content, i + 1 + word.size())) {
                    MentionSpan span{};
                    span.kind = MentionKind::Everyone;
                    emit(span, "@" + std::string(word));
                    i += 1 + word.size();
                    goto next;
                }
            }
        } else if (marks && (unsigned char)c == 0xF4 && i + 3 < content.size()) {
            // U+100000..U+10FFFF all start with F4: neutralise typed ones.
            out.text += "\xEF\xBF\xBD";
            i += 4;
            continue;
        }
        out.text.push_back(c);
        i++;
    next:;
    }
    return out;
}

namespace {
std::string clock(int hour, int minute, int second, bool clock24)
{
    char buf[24];
    int h = clock24 ? hour : (hour % 12 == 0 ? 12 : hour % 12);
    if (second >= 0)
        snprintf(buf, sizeof(buf), clock24 ? "%02d:%02d:%02d" : "%d:%02d:%02d", h, minute, second);
    else
        snprintf(buf, sizeof(buf), clock24 ? "%02d:%02d" : "%d:%02d", h, minute);
    std::string s = buf;
    if (!clock24)
        s += hour < 12 ? " AM" : " PM";
    return s;
}
}  // namespace

std::string formatTimestamp(int64_t when, int64_t now, char style, const CalendarTime &ct, bool clock24,
                            DateFormat date)
{
    static const char *kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    char buf[96];
    switch (style) {
    case 't': return clock(ct.hour, ct.minute, -1, clock24);
    case 'T': return clock(ct.hour, ct.minute, ct.second, clock24);
    case 'd': return numericDate(ct.year, ct.month, ct.day, date);
    case 'D': return longDate(ct.year, ct.month, ct.day, date);
    case 'F':
        return std::string(kDays[(ct.weekday % 7 + 7) % 7]) + ", " + longDate(ct.year, ct.month, ct.day, date) + " " +
               clock(ct.hour, ct.minute, -1, clock24);
    case 'R': {
        int64_t d = when - now;
        bool future = d > 0;
        uint64_t a = (uint64_t)(future ? d : -d);
        struct {
            uint64_t secs;
            const char *unit;
        } units[] = {{31536000, "year"}, {2592000, "month"}, {86400, "day"}, {3600, "hour"}, {60, "minute"}, {1, "second"}};
        if (a < 1)
            return "now";
        for (const auto &u : units) {
            if (a >= u.secs) {
                uint64_t n = a / u.secs;
                snprintf(buf, sizeof(buf), future ? "in %llu %s%s" : "%llu %s%s ago", (unsigned long long)n, u.unit,
                         n == 1 ? "" : "s");
                break;
            }
        }
        break;
    }
    default:  // 'f'
        return longDate(ct.year, ct.month, ct.day, date) + " " + clock(ct.hour, ct.minute, -1, clock24);
    }
    return buf;
}

}  // namespace discord
