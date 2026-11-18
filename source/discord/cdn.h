// SPDX-License-Identifier: MIT
//
// Discord CDN image paths (on cdn.discordapp.com). Images are requested as
// PNG at a power-of-two size; animated avatars come back as their first frame.
#pragma once

#include <string>

#include "discord/types.h"

namespace discord::cdn {

constexpr const char *kHost = "cdn.discordapp.com";

// The user's avatar, or the default avatar Discord assigns them.
std::string avatarPath(const User &u, int size = 64);
// Empty when the server has no icon.
std::string guildIconPath(const Guild &g, int size = 128);
// Group DM icon, or the other person's avatar for a 1:1 DM. Empty if none.
std::string dmIconPath(const Channel &c, int size = 64);

}  // namespace discord::cdn
