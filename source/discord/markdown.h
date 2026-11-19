// SPDX-License-Identifier: MIT
//
// Discord's flavour of markdown: **bold**, *italic* / _italic_, __underline__,
// ~~strikethrough~~, ||spoilers||, `inline code`, ```code blocks```,
// [masked](https://links) and bare links, > quotes and >>> quotes,
// # / ## / ### headings, -# subtext, and - / * / 1. lists. Backslash escapes
// the next symbol. Platform-free.
//
// The text of non-code spans still holds Discord's <@id>, <:emote:id> etc.
// (left whole, never split by markdown) for the caller to resolve; code spans
// and code blocks are literal.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace discord::md {

enum Style : uint16_t {
    Bold = 1,
    Italic = 2,
    Underline = 4,
    Strike = 8,
    Spoiler = 16,
    Code = 32,  // inline code
    Link = 64,
};

struct Span {
    std::string text;
    uint16_t style = 0;
    int spoiler = -1;  // which spoiler (in message order) when Spoiler is set
    std::string url;   // for Link
};

enum class LineKind { Text, Heading1, Heading2, Heading3, Subtext, ListItem, CodeBlock };

struct Line {
    LineKind kind = LineKind::Text;
    bool quote = false;
    int indent = 0;       // list nesting, 0-2
    std::string bullet;   // ListItem: "•" or "3."
    std::vector<Span> spans;  // CodeBlock: one span with the code (newlines kept)
    std::string lang;     // CodeBlock: the language after the opening fence, if any
};

struct Document {
    std::vector<Line> lines;
    int spoilers = 0;  // how many spoilers there are
};

Document parse(std::string_view content);

// Parses inline markup only (no lines or blocks), appending to out.
// spoilers counts spoilers across calls.
void parseInline(std::string_view text, std::vector<Span> &out, int &spoilers);

// The text without markup, on one line (for previews: replies, topics).
// Spoilers become a row of blocks so they stay hidden.
std::string plainText(std::string_view content);

}  // namespace discord::md
