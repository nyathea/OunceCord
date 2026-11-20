// SPDX-License-Identifier: MIT
//
// Discord's inline markup: user, role and channel mentions (<@id>, <@!id>,
// <@&id>, <#id>), @everyone/@here, timestamps (<t:unix:style>) and slash
// command mentions (</name:id>). resolveMentions turns them into readable
// text, and can wrap each one in zero-width marker codepoints so the UI can
// find them after wrapping and draw them highlighted. Platform-free.
#pragma once

#include "util/datefmt.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace discord {

enum class MentionKind { User, Role, Channel, Everyone, Time, Command };

struct MentionSpan {
    MentionKind kind;
    uint64_t id = 0;     // user/role/channel id
    uint32_t color = 0;  // role colour, 0 = default
};

struct MentionLookup {
    // Each returns false when unknown.
    std::function<bool(uint64_t, std::string &)> user;
    std::function<bool(uint64_t, std::string &, uint32_t &)> role;
    std::function<bool(uint64_t, std::string &)> channel;
    // Formats a timestamp in one of Discord's styles (t T d D f F R).
    std::function<std::string(int64_t, char)> time;
};

struct MentionText {
    std::string text;
    std::vector<MentionSpan> spans;
};

constexpr uint32_t kMentionMarkFirst = 0x100000;  // + span index: span begins
constexpr uint32_t kMentionMarkEnd = 0x10FFFD;    // span ends
constexpr size_t kMaxMentionSpans = 0xFFF0;

// With marks, each mention is wrapped as begin-marker, text, end-marker, and
// marker codepoints already in the content are replaced with U+FFFD. Without,
// the result is plain text (for previews).
MentionText resolveMentions(std::string_view content, const MentionLookup &lookup, bool marks);

// Formats a timestamp style given broken-down local time. now/when are unix
// seconds; for 'R' (relative) only those are used.
struct CalendarTime {
    int year, month, day, hour, minute, second, weekday;  // month 1-12, weekday 0 = Sunday
};
// clock24 false: 12-hour times with AM/PM.
std::string formatTimestamp(int64_t when, int64_t now, char style, const CalendarTime &ct, bool clock24 = true,
                            DateFormat date = DateFormat::YMD);

}  // namespace discord
