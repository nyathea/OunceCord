// SPDX-License-Identifier: MIT
//
// Reactions under messages: parsing, the gateway's add/remove events, the
// emoji's form in REST routes, and where to get its picture. Platform-free.
#pragma once

#include <string>

#include "discord/types.h"

namespace discord {

// The emoji as it goes in /reactions/{emoji}: "name:id" for custom emotes,
// the emoji itself for standard ones, percent-encoded.
std::string reactionRoute(const Reaction &r);
// The picture for a reaction: a CDN path for custom emotes, or a Twemoji
// URL for standard emoji.
std::string reactionImage(const Reaction &r, int size);
// Twemoji's file name for an emoji ("1f44d", "1f468-200d-1f469-200d-1f467").
std::string twemojiName(const std::string &emoji);
// The picture for a Twemoji file name: the copy built into the app
// ("local:asset/twemoji/1f44d"), or jsDelivr's when the app lacks it.
std::string twemojiImage(const std::string &name);

// Applies MESSAGE_REACTION_ADD / _REMOVE (emoji is the event's emoji
// object). by_me: the current user reacted; their own events that only
// repeat what we already show (we update right away) change nothing.
void reactionAdded(Message &m, const json::Value &emoji, bool by_me);
void reactionRemoved(Message &m, const json::Value &emoji, bool by_me);
// MESSAGE_REACTION_REMOVE_EMOJI: drops that emoji altogether.
void reactionCleared(Message &m, const json::Value &emoji);

}  // namespace discord
