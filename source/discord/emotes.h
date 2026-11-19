// SPDX-License-Identifier: MIT
//
// Custom emotes in message text: <:name:id> and <a:name:id> (animated).
// For layout, each emote is replaced by a private-use codepoint that the
// renderer treats as an emote-sized blank (see Renderer::kInlineFirst), so
// text wrapping works unchanged and the images are drawn into the gaps.
// Platform-free.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace discord {

struct Emote {
    uint64_t id = 0;
    std::string name;
    bool animated = false;
};

struct EmoteText {
    std::string text;            // emotes replaced by first_cp + index
    std::vector<Emote> emotes;
    // Only emotes and whitespace, at most kMaxJumbo of them: Discord shows
    // these large ("jumbo").
    bool jumbo = false;
};

constexpr size_t kMaxJumbo = 30;
constexpr size_t kMaxEmotes = 200;  // per message; any beyond stay as text

// first_cp..first_cp+kMaxEmotes must be a private-use range. Codepoints in
// that range already present in the text are replaced with U+FFFD.
EmoteText extractEmotes(std::string_view content, uint32_t first_cp, uint32_t last_cp);
// "<:wave:123>" becomes ":wave:", for places that can't draw emotes.
std::string emotesAsText(std::string_view content);
// CDN path for an emote: PNG, or GIF for animated ones. size is a power of two.
std::string emotePath(const Emote &e, int size);

}  // namespace discord
