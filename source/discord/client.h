// SPDX-License-Identifier: MIT
//
// The Discord client: owns the cached state (guilds, DMs, messages), runs
// the gateway and REST workers, and applies their results on the UI thread
// in update().
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "discord/gateway.h"
#include "discord/profile.h"
#include "discord/rest.h"
#include "discord/types.h"
#include "discord/upload.h"

namespace discord {

struct ChannelMessages {
    std::vector<Message> messages;  // oldest first
    bool loading = false;
    bool loaded = false;
    bool reached_start = false;  // no older messages left
    uint64_t older_retry_at = 0;  // backoff after a failed page load
    uint64_t reload_at = 0;       // earliest manual reload
    std::string error;
};

// A forum or media channel's posts, as fetched (active and archived).
struct ForumPosts {
    std::map<Snowflake, Message> first;  // each post's opening message, by thread id
    bool loading = false;
    bool loaded = false;
    bool has_more = false;
    int offset = 0;  // posts fetched so far
    std::string error;
};

struct Typing {
    std::string name;
    uint64_t until_ms;
};

class Client : public GatewayListener {
public:
    Client();
    ~Client() override;

    void start(const std::string &token);
    void stop();
    // Runs queued results on the calling (UI) thread. Call once per frame.
    void update();

    GatewayStatus status() const { return m_status; }
    const std::string &statusDetail() const { return m_statusDetail; }
    bool ready() const { return m_ready; }
    const User &me() const { return m_me; }

    std::vector<Guild> &guilds() { return m_guilds; }
    std::vector<Channel> &dms() { return m_dms; }
    Guild *findGuild(Snowflake id);
    Channel *findChannel(Snowflake id);
    bool canView(const Channel &c);
    bool canSend(const Channel &c);

    // Forum/media channels: the posts are threads in its guild's channel
    // list (parent_id = the forum); this holds their first messages and
    // the paging state. loadForum fetches the first page once.
    ForumPosts &forum(Snowflake forum_id) { return m_forums[forum_id]; }
    void loadForum(Snowflake forum_id);
    void loadMoreForum(Snowflake forum_id);
    // Makes sure a thread we only know by id (from a message) is in its
    // guild's channel list, and fetches its details.
    void ensureThread(Snowflake thread_id, Snowflake parent_id, Snowflake guild_id, const std::string &name);

    ChannelMessages &messages(Snowflake channel_id) { return m_messages[channel_id]; }
    void loadMessages(Snowflake channel_id);  // latest page, if not loaded
    void loadOlder(Snowflake channel_id);
    // Refetches the latest page; ignored while on cooldown.
    void reloadMessages(Snowflake channel_id);
    // Milliseconds until slowmode lets us send in this channel (0 = now).
    uint64_t sendCooldownMs(const Channel &c);
    // Largest file we may upload in this channel (Nitro and server boosts).
    uint64_t uploadLimit(const Channel &c);
    // Sends a message, with a file when upload is given (content may then be empty).
    // reply_to: the message this replies to (0 for none).
    void sendMessage(Snowflake channel_id, const std::string &content, const Upload *upload = nullptr,
                     Snowflake reply_to = 0);
    // Adds our reaction with this emoji, or takes it away if we already reacted.
    void toggleReaction(Snowflake channel_id, Snowflake message_id, const Reaction &reaction);
    Message *findMessage(Snowflake channel_id, Snowflake message_id);
    // A user's profile (bio, banner, connections...), with their server
    // profile for guild_id. Fetched on first use, once per session: null
    // while loading; failed is set when it couldn't be loaded.
    const UserProfile *profile(Snowflake user_id, Snowflake guild_id, bool *failed = nullptr);
    // Whether we may send this server's emote in channel ch (null: anywhere):
    // without Nitro only the channel's own server's non-animated emotes;
    // role-locked and unavailable ones never.
    bool canUseEmoji(const Guild &source, const GuildEmoji &e, const Channel *ch);
    bool hasNitro() const { return m_me.premium_type != 0; }
    // Marks a channel as being viewed: subscribes to its guild's events.
    void focusChannel(Snowflake channel_id);
    std::string typingText(Snowflake channel_id);

    // Bumped whenever guilds/channels change, so the UI can rebuild lists.
    uint32_t structureVersion() const { return m_structureVersion; }
    // Bumped when a channel's messages change.
    uint32_t messagesVersion() const { return m_messagesVersion; }

    // Called when Discord hands out a refreshed token (AUTH_TOKEN_REFRESH).
    std::function<void(const std::string &)> onTokenRefresh;
    // Called with a short message when an action (like reacting) fails.
    std::function<void(const std::string &)> onError;

    // GatewayListener
    void onDispatch(const std::string &type, const json::Value &d) override;
    void onStatus(GatewayStatus status, const std::string &detail) override;

private:
    std::mutex m_postMutex;
    std::vector<std::function<void()>> m_posted;
    std::shared_ptr<bool> m_alive;

    RestWorker m_rest;
    Gateway m_gateway;
    std::string m_token;

    GatewayStatus m_status = GatewayStatus::Idle;
    std::string m_statusDetail;
    bool m_ready = false;
    User m_me;
    std::vector<Guild> m_guilds;
    std::vector<Channel> m_dms;
    std::map<Snowflake, ChannelMessages> m_messages;
    std::map<Snowflake, ForumPosts> m_forums;
    std::map<Snowflake, std::vector<std::pair<Snowflake, Typing>>> m_typing;
    std::map<Snowflake, std::set<Snowflake>> m_subscribed;  // guild -> channels
    std::set<Snowflake> m_fetchingChannels;
    std::map<Snowflake, std::set<Snowflake>> m_memberRequested;  // guild -> users asked for
    struct ProfileEntry {
        bool loaded = false, failed = false;
        UserProfile profile;
    };
    std::map<std::pair<Snowflake, Snowflake>, ProfileEntry> m_profiles;  // (user, guild)
    std::map<Snowflake, uint64_t> m_lastSent;  // channel -> time, for slowmode
    uint32_t m_structureVersion = 0;
    uint32_t m_messagesVersion = 0;

    void post(std::function<void()> fn);
    void handleReady(const json::Value &d);
    void handleMessageCreate(const json::Value &d);
    void handleChannelUpsert(const json::Value &d);
    void handleChannelDelete(const json::Value &d);
    void handleGuildCreate(const json::Value &d);
    void sortDms();
    void fetchForumPage(Snowflake forum_id);
    void requestMembers(Snowflake channel_id);
};

}  // namespace discord
