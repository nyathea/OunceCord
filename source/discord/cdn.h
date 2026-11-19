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

// --- Images in messages ---
//
// Attachments and embed images come as absolute URLs on Discord's media
// proxies (media.discordapp.net, images-ext-N.discordapp.net), which resize
// on request. Only Discord's own image hosts are fetched, plus Tenor's and
// Giphy's media hosts for GIFs (Discord doesn't proxy those as GIFs).

// Splits "https://host/path?query" into host and "/path?query". False when
// it isn't https or the host isn't one of Discord's image hosts.
bool splitImageUrl(const std::string &url, std::string &host, std::string &path);
// The URL to fetch an image at (at most) width x height: sets the proxy's
// width/height parameters, and asks for PNG unless the original is PNG or
// JPEG (GIFs are kept animated; WebP becomes a still PNG). URLs on hosts
// that can't resize (Tenor, Giphy) come back unchanged.
std::string sizedImageUrl(const std::string &url, int width, int height);
// The URL for an attachment's picture: its proxy URL, or the media proxy
// version of its CDN URL.
std::string attachmentImageUrl(const Attachment &a);
// A stable cache key: the URL without the signature parameters (ex, is,
// hm) that Discord rotates on attachment links.
std::string imageCacheKey(const std::string &url);

}  // namespace discord::cdn
