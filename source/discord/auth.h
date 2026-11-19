// SPDX-License-Identifier: MIT
//
// Signing in with an email/phone and password, the way the official client
// does (https://docs.discord.food/authentication):
//
//   GET  /experiments            -> a tracking fingerprint (cached on SD)
//   POST /auth/login             -> a token, or an MFA ticket
//   POST /auth/mfa/sms/send      -> texts a code (when SMS is enabled)
//   POST /auth/mfa/{totp,sms,backup} with the ticket -> a token
//
// Logging in this way gives OunceCord a session of its own, rather than
// sharing a token with another client. Requests run on a worker thread;
// results are delivered by update() on the UI thread.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "discord/authparse.h"
#include "net/http.h"
#include "util/json.h"

namespace discord {

class Auth {
public:
    using Callback = std::function<void(const AuthResult &)>;

    Auth();
    ~Auth();

    // True while a request is in flight.
    bool busy() const { return m_pending > 0; }
    // Milliseconds until Discord's rate limit lets us try again (0 = now).
    uint64_t cooldownMs() const;

    void login(const std::string &login, const std::string &password, Callback cb);
    // type is "totp", "sms" or "backup". Uses the ticket from the last login.
    void submitMfa(const std::string &type, const std::string &code, Callback cb);
    void sendSms(Callback cb);
    // Forgets the MFA ticket (going back to the credentials form).
    void reset();
    // Invalidates a token on Discord's side. Fire and forget.
    void logout(const std::string &token);

    // Delivers finished results. Call once per frame on the UI thread.
    void update();

private:
    struct Job {
        std::function<AuthResult(HttpClient &)> run;
        Callback cb;
    };

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::vector<std::function<void()>> m_done;
    bool m_running = true;
    int m_pending = 0;  // queued or running jobs; UI thread only

    // Worker-thread state (and the ticket, written only by the worker).
    std::string m_ticket;
    std::string m_loginInstanceId;
    std::atomic<uint64_t> m_blockedUntil{0};  // after a 429

    void enqueue(std::function<AuthResult(HttpClient &)> run, Callback cb);
    void worker();
    // Sends a login-flow request. Handles 429 bookkeeping.
    HttpResponse post(HttpClient &http, const std::string &path, const json::Value &body,
                      const std::string &token = {});
    // Shared handling of /auth/login and /auth/mfa/* responses.
    AuthResult interpret(const HttpResponse &res, bool ticket_request);
};

}  // namespace discord
