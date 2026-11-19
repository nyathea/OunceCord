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

    ChannelMessages &messages(Snowflake channel_id) { return m_messages[channel_id]; }
    void loadMessages(Snowflake channel_id);  // latest page, if not loaded
    void loadOlder(Snowflake channel_id);
    // Refetches the latest page; ignored while on cooldown.
    void reloadMessages(Snowflake channel_id);
    // Milliseconds until slowmode lets us send in this channel (0 = now).
    uint64_t sendCooldownMs(const Channel &c);
    // Sends a message, with a file when upload is given (content may then be empty).
    void sendMessage(Snowflake channel_id, const std::string &content, const Upload *upload = nullptr);
    // Marks a channel as being viewed: subscribes to its guild's events.
    void focusChannel(Snowflake channel_id);
    std::string typingText(Snowflake channel_id);

    // Bumped whenever guilds/channels change, so the UI can rebuild lists.
    uint32_t structureVersion() const { return m_structureVersion; }
    // Bumped when a channel's messages change.
    uint32_t messagesVersion() const { return m_messagesVersion; }

    // Called when Discord hands out a refreshed token (AUTH_TOKEN_REFRESH).
    std::function<void(const std::string &)> onTokenRefresh;

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
    std::map<Snowflake, std::vector<std::pair<Snowflake, Typing>>> m_typing;
    std::map<Snowflake, std::set<Snowflake>> m_subscribed;  // guild -> channels
    std::set<Snowflake> m_fetchingChannels;
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
};

}  // namespace discord
