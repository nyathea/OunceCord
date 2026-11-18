// SPDX-License-Identifier: MIT
//
// The Discord gateway connection, run on its own thread. It identifies,
// heartbeats, resumes after drops, and hands dispatch events to the UI
// thread through the post function.
#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <zlib.h>

#include "discord/rest.h"
#include "net/websocket.h"
#include "util/json.h"

namespace discord {

enum class GatewayStatus { Idle, Connecting, Connected, Reconnecting, AuthFailed, Fatal };

class GatewayListener {
public:
    virtual ~GatewayListener() = default;
    // Called on the UI thread.
    virtual void onDispatch(const std::string &type, const json::Value &d) = 0;
    virtual void onStatus(GatewayStatus status, const std::string &detail) = 0;
};

class Gateway {
public:
    Gateway(MainThreadPost post, GatewayListener *listener)
        : m_post(std::move(post)), m_listener(listener)
    {
    }
    ~Gateway() { stop(); }

    void start(std::string token);
    void stop();
    // Queues a payload ({"op":..,"d":..}) to send. Thread-safe.
    void send(json::Value payload);
    void setToken(std::string token);

private:
    MainThreadPost m_post;
    GatewayListener *m_listener;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    std::mutex m_mutex;
    std::deque<std::string> m_outbox;
    std::string m_token;

    // Connection state, touched only by the gateway thread.
    WebSocket m_ws;
    z_stream m_zs{};
    bool m_zsReady = false;
    std::string m_zbuf;
    int64_t m_seq = -1;
    std::string m_sessionId;
    std::string m_resumeHost;
    std::deque<uint64_t> m_sendTimes;      // this connection's recent sends
    std::deque<uint64_t> m_identifyTimes;  // across connections
    bool m_rateLimitedClose = false;
    uint64_t m_heartbeatInterval = 41250;
    uint64_t m_nextHeartbeat = 0;
    bool m_ackPending = false;

    static constexpr uint64_t kSendWindowMs = 60000;
    static constexpr size_t kSendLimit = 120;
    static constexpr size_t kSendReserve = 10;
    static constexpr uint64_t kIdentifySpacingMs = 5500;
    static constexpr uint64_t kIdentifyWindowMs = 60 * 60 * 1000;
    static constexpr size_t kMaxIdentifiesPerWindow = 12;

    void run();
    void sleepFor(uint64_t ms);
    // Sends if this connection's send budget allows; false otherwise.
    bool sendPayload(const std::string &text, bool priority);
    uint64_t identifyWaitMs();
    // Runs one connection until it drops. Returns false when the gateway
    // must not reconnect (bad token, fatal close code, or stop()).
    bool runConnection(bool &resumable);
    bool handlePayload(const std::string &text, bool &resumable, bool &reconnect_now);
    bool inflateMessage(const std::string &in, std::string &out);
    void sendIdentify();
    void sendResume();
    void sendHeartbeat();
    void status(GatewayStatus s, std::string detail = {});
    void resetZlib();
};

}  // namespace discord
