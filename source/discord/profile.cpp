// SPDX-License-Identifier: MIT
#include "discord/profile.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace discord {

std::string profilePath(Snowflake user_id, Snowflake guild_id)
{
    std::string p = "/users/" + std::to_string(user_id) +
                    "/profile?type=popout&with_mutual_guilds=true&with_mutual_friends=false&with_mutual_friends_count=false";
    if (guild_id)
        p += "&guild_id=" + std::to_string(guild_id);
    return p;
}

static bool hexHash(const std::string &h)
{
    if (h.empty() || h.size() > 64)
        return false;
    for (char c : h)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '_'))
            return false;
    return true;
}

bool parseProfile(const json::Value &v, Snowflake user_id, Snowflake guild_id, UserProfile &out)
{
    if (!v.isObject() || !v["user"].isObject())
        return false;
    out = {};
    const json::Value &user = v["user"];
    const json::Value &up = v["user_profile"];
    const json::Value &gp = v["guild_member_profile"];

    out.bio = up["bio"].str(user["bio"].str());
    out.pronouns = up["pronouns"].str();
    std::string banner = up["banner"].str(user["banner"].str());
    if (hexHash(banner))
        out.banner = "/banners/" + std::to_string(user_id) + "/" + banner + ".png?size=600";
    const json::Value &accent = up["accent_color"].isNumber() ? up["accent_color"] : user["accent_color"];
    if (accent.isNumber()) {
        out.banner_color = (uint32_t)accent.asInt();
        out.has_banner_color = true;
    }
    for (const auto &c : up["theme_colors"].items())
        if (c.isNumber())
            out.theme_colors.push_back((uint32_t)c.asInt());

    // The server profile overrides what it sets.
    if (guild_id && gp.isObject()) {
        if (!gp["bio"].str().empty())
            out.bio = gp["bio"].str();
        if (!gp["pronouns"].str().empty())
            out.pronouns = gp["pronouns"].str();
        std::string gb = gp["banner"].str();
        if (hexHash(gb))
            out.banner = "/guilds/" + std::to_string(guild_id) + "/users/" + std::to_string(user_id) +
                         "/banners/" + gb + ".png?size=600";
        if (gp["accent_color"].isNumber()) {
            out.banner_color = (uint32_t)gp["accent_color"].asInt();
            out.has_banner_color = true;
        }
        if (gp["theme_colors"].isArray() && gp["theme_colors"].size()) {
            out.theme_colors.clear();
            for (const auto &c : gp["theme_colors"].items())
                if (c.isNumber())
                    out.theme_colors.push_back((uint32_t)c.asInt());
        }
    }

    for (const auto &c : v["connected_accounts"].items()) {
        UserProfile::Connection conn;
        conn.type = c["type"].str();
        conn.name = c["name"].str();
        conn.verified = c["verified"].asBool();
        if (!conn.type.empty() && !conn.name.empty())
            out.connections.push_back(conn);
    }
    for (const auto &b : v["badges"].items()) {
        UserProfile::Badge badge;
        badge.id = b["id"].str();
        badge.description = b["description"].str();
        std::string icon = b["icon"].str();
        if (hexHash(icon))
            badge.icon = "/badge-icons/" + icon + ".png";
        out.badges.push_back(badge);
    }
    out.joined_server = v["guild_member"]["joined_at"].str();
    if (v["mutual_guilds"].isArray()) {
        out.mutual_servers = (int)v["mutual_guilds"].size();
        for (const auto &g : v["mutual_guilds"].items())
            if (Snowflake id = g["id"].asId())
                out.mutual_guild_ids.push_back(id);
    }
    return true;
}

std::string connectionName(const std::string &type)
{
    static const struct { const char *type, *name; } names[] = {
        {"battlenet", "Battle.net"}, {"bluesky", "Bluesky"},   {"bungie", "Bungie.net"},
        {"crunchyroll", "Crunchyroll"}, {"domain", "Domain"}, {"ebay", "eBay"},
        {"epicgames", "Epic Games"}, {"facebook", "Facebook"}, {"github", "GitHub"},
        {"instagram", "Instagram"}, {"leagueoflegends", "League of Legends"},
        {"mastodon", "Mastodon"}, {"paypal", "PayPal"}, {"playstation", "PlayStation"},
        {"reddit", "Reddit"}, {"riotgames", "Riot Games"}, {"roblox", "Roblox"},
        {"skype", "Skype"}, {"spotify", "Spotify"}, {"steam", "Steam"}, {"tiktok", "TikTok"},
        {"twitch", "Twitch"}, {"twitter", "X"}, {"xbox", "Xbox"}, {"youtube", "YouTube"},
    };
    for (const auto &n : names)
        if (type == n.type)
            return n.name;
    std::string s = type;
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
        s[0] = (char)(s[0] - 'a' + 'A');
    return s;
}

static std::string dateText(int y, int m, int d, DateFormat f)
{
    if (m < 1 || m > 12 || d < 1 || d > 31 || y < 2000)
        return {};
    return longDate(y, m, d, f, true);
}

std::string formatIsoDate(const std::string &iso, DateFormat f)
{
    int y = 0, m = 0, d = 0;
    if (iso.size() < 10 || sscanf(iso.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3)
        return {};
    return dateText(y, m, d, f);
}

std::string snowflakeDate(Snowflake id, DateFormat f)
{
    if (!id)
        return {};
    time_t t = (time_t)(((id >> 22) + 1420070400000ULL) / 1000);
    struct tm tm{};
    gmtime_r(&t, &tm);
    return dateText(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, f);
}

}  // namespace discord
