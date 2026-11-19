// SPDX-License-Identifier: MIT
//
// Standard (Unicode) emoji: their :shortcodes:, picker categories, and
// finding them in text so they can be drawn with Twemoji pictures. The table
// comes from GitHub's gemoji (tools/pack_assets.py), plus a few names
// Discord uses differently. Platform-free.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace discord::emoji {

struct Entry {
    std::string chars;               // the emoji itself (fully qualified)
    std::vector<std::string> names;  // shortcodes without colons, Discord's first
    std::string tags;                // extra search words, space separated
    uint8_t category = 0;            // index into categoryNames()
};

// Loads the table: one emoji per line, "chars TAB category TAB names(,) TAB tags".
void load(std::string_view table);
bool loaded();
const std::vector<Entry> &all();
const std::vector<std::string> &categoryNames();

// By shortcode ("joy", "+1"), without colons. Null if unknown.
const Entry *byName(std::string_view name);
// The emoji starting at s[i]: its length in bytes (with any skin tone
// modifiers and variation selectors), or 0. entry is set when non-null.
size_t match(std::string_view s, size_t i, const Entry **entry = nullptr);
// ":joy:" for an emoji's text ("" when unknown).
std::string shortcode(std::string_view chars);

// Replaces :shortcodes: in a message being sent: custom(name) is asked
// first (it returns "<:name:id>", or "" when there's no usable emote by that
// name), then standard emoji. A following :skin-tone-1: to :skin-tone-5:
// applies to the emoji before it. Code (`...`, ```...```) and unknown names
// are left alone.
std::string replaceShortcodes(std::string_view text, const std::function<std::string(std::string_view)> &custom);

}  // namespace discord::emoji
