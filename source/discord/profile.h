// SPDX-License-Identifier: MIT
//
// User profiles from GET /users/{id}/profile: bio, pronouns, banner,
// badges, connected accounts, and the server-specific profile. Platform-free.
#pragma once

#include "util/datefmt.h"

#include <cstdint>
#include <string>
#include <vector>

#include "discord/types.h"

namespace discord {

struct UserProfile {
    struct Connection {
        std::string type;  // "github", "steam"...
        std::string name;
        bool verified = false;
    };
    struct Badge {
        std::string id, description;
        std::string icon;  // CDN path
    };
    std::string bio;       // the server profile's, if it has one
    std::string pronouns;
    std::string banner;    // CDN path, empty if none
    uint32_t banner_color = 0;  // accent colour, used when there's no banner
    bool has_banner_color = false;
    std::vector<uint32_t> theme_colors;  // Nitro profile colours (primary, accent)
    std::vector<Connection> connections;
    std::vector<Badge> badges;
    std::string joined_server;  // ISO 8601, empty when unknown
    int mutual_servers = -1;    // -1: unknown
};

// The request path for a user's profile, with their server profile when
// guild_id isn't 0.
std::string profilePath(Snowflake user_id, Snowflake guild_id);
// Parses the response. False if it isn't a profile.
bool parseProfile(const json::Value &v, Snowflake user_id, Snowflake guild_id, UserProfile &out);
// A connection type's display name ("GitHub", "Steam"...).
std::string connectionName(const std::string &type);
// "2021-03-04T05:06:07.000000+00:00" -> "Mar 4, 2021"; empty if unparseable.
std::string formatIsoDate(const std::string &iso, DateFormat f = DateFormat::MDY);
// The account's creation date from its ID, as "Mar 4, 2021".
std::string snowflakeDate(Snowflake id, DateFormat f = DateFormat::MDY);

}  // namespace discord
