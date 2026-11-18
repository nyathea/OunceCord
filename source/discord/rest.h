// SPDX-License-Identifier: MIT
//
// Discord REST: client identity headers and a worker thread that performs
// requests off the UI thread, with keep-alive and 429 handling.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "discord/ratelimit.h"
#include "net/http.h"
#include "util/json.h"

namespace discord {

constexpr const char *kApiHost = "discord.com";
constexpr const char *kApiBase = "/api/v9";
constexpr const char *kGatewayHost = "gateway.discord.gg";

// Client identity. OunceCord presents itself like the official desktop
// client (as Paicord does), since user tokens are expected to come from one.
// Edit these in one place when Discord's build numbers move on.
struct Identity {
    std::string locale;    // "en-US"
    std::string timezone;  // "Europe/London"
    std::string launch_id, launch_signature, heartbeat_session_id;

    static Identity &get();
    void init();  // Reads locale and time zone from the system.

    std::string userAgent() const;
    // The connection properties object, for IDENTIFY (ws=true) and for the
    // X-Super-Properties header (ws=false).
    json::Value properties(bool ws) const;
    std::string superPropertiesHeader() const;
    // Headers every authenticated REST request carries.
    HttpHeaders restHeaders(const std::string &token) const;
};

using MainThreadPost = std::function<void(std::function<void()>)>;

class RestWorker {
public:
    using Callback = std::function<void(const HttpResponse &)>;

    // Requests that hit a 429 are retried when the wait is at most this long.
    static constexpr uint64_t kMaxRetryWaitMs = 10000;
    static constexpr int kMaxAttempts = 3;

    // post runs a closure on the UI thread; callbacks are delivered through it.
    explicit RestWorker(MainThreadPost post) : m_post(std::move(post)) {}
    ~RestWorker() { stop(); }

    void start(std::string token);
    void stop();
    void setToken(std::string token);

    // Queues a request; path is relative to the API base ("/users/@me").
    // extra headers are appended to the identity headers. Requests are sent
    // in order per rate-limit bucket.
    void request(std::string method, std::string path, std::string body, Callback cb,
                 HttpHeaders extra = {});

    // Posted to the UI thread when Discord answers 401 (token invalid).
    std::function<void()> onUnauthorized;

private:
    struct Job {
        std::string method, path, body, route;
        HttpHeaders extra;
        Callback cb;
        uint64_t not_before = 0;
        int attempts = 0;
    };

    MainThreadPost m_post;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::string m_token;
    bool m_running = false;
    bool m_unauthorized = false;

    void run();
    void deliver(Job &job, HttpResponse res);
};

}  // namespace discord
