// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/json.h"

using Snowflake = uint64_t;

namespace perm {
constexpr uint64_t Administrator = 1ULL << 3;
constexpr uint64_t ManageChannels = 1ULL << 4;
constexpr uint64_t ManageMessages = 1ULL << 13;
constexpr uint64_t ViewChannel = 1ULL << 10;
constexpr uint64_t SendMessages = 1ULL << 11;
constexpr uint64_t ReadMessageHistory = 1ULL << 16;
constexpr uint64_t All = ~0ULL;
}  // namespace perm

enum ChannelType : int {
    ChannelType_GuildText = 0,
    ChannelType_DM = 1,
    ChannelType_GuildVoice = 2,
    ChannelType_GroupDM = 3,
    ChannelType_GuildCategory = 4,
    ChannelType_GuildAnnouncement = 5,
    ChannelType_AnnouncementThread = 10,
    ChannelType_PublicThread = 11,
    ChannelType_PrivateThread = 12,
    ChannelType_GuildStageVoice = 13,
    ChannelType_GuildForum = 15,
    ChannelType_GuildMedia = 16,
};

struct User {
    Snowflake id = 0;
    std::string username;
    std::string global_name;
    std::string avatar;         // CDN hash, empty for the default avatar
    std::string discriminator;  // "0" for users on the new username system
    bool bot = false;

    const std::string &displayName() const { return global_name.empty() ? username : global_name; }
    static User fromJson(const json::Value &v);
};

struct Role {
    Snowflake id = 0;
    std::string name;
    uint64_t permissions = 0;
    int position = 0;
    uint32_t color = 0;
};

struct Overwrite {
    Snowflake id = 0;
    int type = 0;  // 0 role, 1 member
    uint64_t allow = 0;
    uint64_t deny = 0;
};

struct Channel {
    Snowflake id = 0;
    Snowflake guild_id = 0;
    Snowflake parent_id = 0;
    Snowflake last_message_id = 0;
    int type = 0;
    int position = 0;
    int rate_limit_per_user = 0;  // slowmode, seconds
    std::string name;
    std::string topic;
    std::string icon;  // group DM icon hash
    std::vector<Overwrite> overwrites;
    std::vector<User> recipients;  // DMs

    bool isText() const
    {
        return type == ChannelType_GuildText || type == ChannelType_GuildAnnouncement ||
               type == ChannelType_DM || type == ChannelType_GroupDM ||
               type == ChannelType_PublicThread || type == ChannelType_PrivateThread ||
               type == ChannelType_AnnouncementThread;
    }
    bool isDm() const { return type == ChannelType_DM || type == ChannelType_GroupDM; }
    // Name to show in lists ("#general", or the DM recipients).
    std::string label() const;

    // Updates the fields present in v. users resolves recipient_ids.
    void applyJson(const json::Value &v, const std::vector<User> *users = nullptr);
};

struct Guild {
    Snowflake id = 0;
    Snowflake owner_id = 0;
    std::string name;
    std::string icon;  // CDN hash
    std::vector<Role> roles;
    std::vector<Channel> channels;  // sorted for display by sortChannels
    std::vector<Snowflake> my_roles;
    bool unavailable = false;

    void applyJson(const json::Value &v);
    Channel *findChannel(Snowflake id);
    // Permissions of the current user in a channel of this guild.
    uint64_t permissionsIn(const Channel &c, Snowflake me) const;
    // Sorts channels as the official client does: categories by position,
    // with their children beneath them, uncategorised channels first.
    void sortChannels();
};

struct Attachment {
    std::string filename;
    std::string url;
    uint64_t size = 0;
};

struct Message {
    Snowflake id = 0;
    Snowflake channel_id = 0;
    Snowflake guild_id = 0;
    User author;
    std::string content;
    int type = 0;
    bool edited = false;
    std::vector<Attachment> attachments;
    int embed_count = 0;
    std::string embed_summary;  // title/description of the first embed
    // Replied-to message, if any.
    bool has_reply = false;
    std::string reply_author;
    std::string reply_content;
    // Local bookkeeping for messages we sent and are waiting on.
    std::string nonce;
    bool pending = false;
    bool failed = false;
    std::string error;  // why sending failed

    static Message fromJson(const json::Value &v);
    void applyUpdate(const json::Value &v);
};
