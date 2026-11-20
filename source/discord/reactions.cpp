// SPDX-License-Identifier: MIT
#include "discord/reactions.h"

#include <algorithm>
#include <cstdio>

#include "discord/emotes.h"
#include "util/assets.h"

namespace discord {

namespace {

std::string percentEncode(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

Reaction fromEmoji(const json::Value &emoji)
{
    Reaction r;
    r.emoji_id = emoji["id"].asId();
    r.emoji_name = emoji["name"].str();
    r.animated = emoji["animated"].asBool();
    return r;
}

Reaction *find(Message &m, const Reaction &key)
{
    for (auto &r : m.reactions)
        if (key.emoji_id ? r.emoji_id == key.emoji_id : (!r.emoji_id && r.emoji_name == key.emoji_name))
            return &r;
    return nullptr;
}

}  // namespace

std::string reactionRoute(const Reaction &r)
{
    if (r.emoji_id)
        return percentEncode(r.emoji_name + ":" + std::to_string(r.emoji_id));
    return percentEncode(r.emoji_name);
}

std::string twemojiName(const std::string &emoji)
{
    std::vector<uint32_t> cps;
    for (size_t i = 0; i < emoji.size();) {
        unsigned char c = emoji[i];
        uint32_t cp;
        int n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c >> 5) == 6) cp = c & 0x1f, n = 2;
        else if ((c >> 4) == 14) cp = c & 0x0f, n = 3;
        else if ((c >> 3) == 30) cp = c & 0x07, n = 4;
        else return {};
        if (i + n > emoji.size())
            return {};
        for (int k = 1; k < n; k++)
            cp = (cp << 6) | (emoji[i + k] & 0x3f);
        cps.push_back(cp);
        i += n;
    }
    // Twemoji drops the variation selector unless the emoji is a ZWJ sequence.
    bool zwj = std::find(cps.begin(), cps.end(), 0x200d) != cps.end();
    std::string out;
    for (uint32_t cp : cps) {
        if (cp == 0xfe0f && !zwj)
            continue;
        char buf[12];
        snprintf(buf, sizeof(buf), "%s%x", out.empty() ? "" : "-", (unsigned)cp);
        out += buf;
    }
    return out;
}

std::string reactionImage(const Reaction &r, int size)
{
    if (r.emoji_id) {
        Emote e;
        e.id = r.emoji_id;
        e.name = r.emoji_name;
        e.animated = r.animated;
        return emotePath(e, size);
    }
    std::string name = twemojiName(r.emoji_name);
    return name.empty() ? std::string() : twemojiImage(name);
}

std::string twemojiImage(const std::string &name)
{
    if (!assets::get("twemoji/" + name).empty())
        return "local:asset/twemoji/" + name;
    // A few ZWJ sequences are named without their variation selectors
    // ("1f441-200d-1f5e8" for the eye in a speech bubble).
    std::string bare;
    for (size_t i = 0; i < name.size();) {
        size_t dash = name.find('-', i);
        std::string part = name.substr(i, dash == std::string::npos ? std::string::npos : dash - i);
        if (part != "fe0f")
            bare += (bare.empty() ? "" : "-") + part;
        i = dash == std::string::npos ? name.size() : dash + 1;
    }
    if (bare != name && !assets::get("twemoji/" + bare).empty())
        return "local:asset/twemoji/" + bare;
    return "https://cdn.jsdelivr.net/gh/jdecked/twemoji@15.1.0/assets/72x72/" + name + ".png";
}

void reactionAdded(Message &m, const json::Value &emoji, bool by_me)
{
    Reaction key = fromEmoji(emoji);
    Reaction *r = find(m, key);
    if (r && by_me && r->me)
        return;  // already shown
    if (!r) {
        m.reactions.push_back(key);
        r = &m.reactions.back();
    }
    r->count++;
    if (by_me)
        r->me = true;
}

void reactionRemoved(Message &m, const json::Value &emoji, bool by_me)
{
    Reaction *r = find(m, fromEmoji(emoji));
    if (!r || (by_me && !r->me))
        return;
    r->count--;
    if (by_me)
        r->me = false;
    if (r->count <= 0)
        m.reactions.erase(m.reactions.begin() + (r - m.reactions.data()));
}

void reactionCleared(Message &m, const json::Value &emoji)
{
    if (Reaction *r = find(m, fromEmoji(emoji)))
        m.reactions.erase(m.reactions.begin() + (r - m.reactions.data()));
}

}  // namespace discord

Reaction Reaction::fromJson(const json::Value &v)
{
    Reaction r = discord::fromEmoji(v["emoji"]);
    r.count = (int)v["count"].asInt();
    r.me = v["me"].asBool() || v["me_burst"].asBool();
    return r;
}
