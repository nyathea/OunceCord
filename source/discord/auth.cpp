// SPDX-License-Identifier: MIT
#include "discord/auth.h"

#include <memory>

#include "config.h"
#include "discord/rest.h"
#include "discord/session.h"
#include "util/util.h"

namespace discord {

static constexpr const char *kLoginReferer = "https://discord.com/login";

Auth::Auth()
{
    m_thread = std::thread([this] { worker(); });
}

Auth::~Auth()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        m_jobs.clear();
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

uint64_t Auth::cooldownMs() const
{
    uint64_t now = nowMs(), until = m_blockedUntil;
    return until > now ? until - now : 0;
}

void Auth::enqueue(std::function<AuthResult(HttpClient &)> run, Callback cb)
{
    m_pending++;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push_back({std::move(run), std::move(cb)});
    }
    m_cv.notify_one();
}

void Auth::update()
{
    std::vector<std::function<void()>> done;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        done.swap(m_done);
    }
    for (auto &fn : done) {
        m_pending--;
        fn();
    }
}

void Auth::worker()
{
    HttpClient http(kApiHost);
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running || !m_jobs.empty(); });
            if (!m_running)
                break;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        auto result = std::make_shared<AuthResult>(job.run(http));
        auto cb = std::move(job.cb);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_done.push_back([cb, result] {
            if (cb)
                cb(*result);
        });
    }
    http.close();
}

HttpResponse Auth::post(HttpClient &http, const std::string &path, const json::Value &body,
                        const std::string &token)
{
    HttpHeaders headers = Identity::get().restHeaders(token, kLoginReferer);
    headers.emplace_back("Content-Type", "application/json");
    ClientSession::get().apply(headers, token.empty());
    HttpResponse res = http.request("POST", std::string(kApiBase) + path, headers, body.dump());
    ClientSession::get().absorb(res);
    // Never log request bodies here: they hold the password or codes.
    if (res.status == 0)
        logf("auth: POST %s failed: %s", path.c_str(), res.error.c_str());
    else
        logf("auth: POST %s -> %d", path.c_str(), res.status);
    return res;
}

AuthResult Auth::interpret(const HttpResponse &res, bool ticket_request)
{
    double retry = 0;
    std::string ticket, instance;
    AuthResult r = parseAuthResponse(res.status, res.body, ticket, instance, retry);
    if (retry > 0)
        m_blockedUntil = nowMs() + (uint64_t)(retry * 1000);
    if (r.kind == AuthResult::Kind::Mfa && ticket_request) {
        m_ticket = ticket;
        m_loginInstanceId = instance;
    }
    if (r.kind == AuthResult::Kind::Token || r.ticket_expired) {
        m_ticket.clear();
        m_loginInstanceId.clear();
    }
    if (r.kind == AuthResult::Kind::Captcha)
        logf("auth: captcha required");
    else if (r.kind == AuthResult::Kind::Error)
        logf("auth: %s", r.message.c_str());
    return r;
}

static AuthResult failure(std::string message)
{
    AuthResult r;
    r.message = std::move(message);
    return r;
}

void Auth::login(const std::string &login, const std::string &password, Callback cb)
{
    enqueue(
        [this, login, pw = std::string(password)](HttpClient &http) mutable {
            AuthResult r;
            std::string error;
            if (cooldownMs() > 0)
                r = failure("Too many attempts. Wait a little and try again.");
            else if (!ClientSession::get().ensureFingerprint(http, error))
                r = failure(error);
            else {
                m_ticket.clear();
                json::Value body = json::Value::object();
                body.set("login", json::Value::string(login));
                body.set("password", json::Value::string(pw));
                body.set("undelete", json::Value::boolean(false));
                r = interpret(post(http, "/auth/login", body), true);
            }
            // Best effort: don't keep the password around in this closure.
            pw.assign(pw.size(), '\0');
            pw.clear();
            return r;
        },
        std::move(cb));
}

void Auth::submitMfa(const std::string &type, const std::string &code, Callback cb)
{
    enqueue(
        [this, type, code](HttpClient &http) {
            if (m_ticket.empty()) {
                AuthResult r = failure("That sign-in expired. Log in again.");
                r.ticket_expired = true;
                return r;
            }
            if (cooldownMs() > 0)
                return failure("Too many attempts. Wait a little and try again.");
            json::Value body = json::Value::object();
            body.set("code", json::Value::string(code));
            body.set("ticket", json::Value::string(m_ticket));
            if (!m_loginInstanceId.empty())
                body.set("login_instance_id", json::Value::string(m_loginInstanceId));
            return interpret(post(http, "/auth/mfa/" + type, body), false);
        },
        std::move(cb));
}

void Auth::sendSms(Callback cb)
{
    enqueue(
        [this](HttpClient &http) {
            if (m_ticket.empty()) {
                AuthResult r = failure("That sign-in expired. Log in again.");
                r.ticket_expired = true;
                return r;
            }
            if (cooldownMs() > 0)
                return failure("Too many attempts. Wait a little and try again.");
            json::Value body = json::Value::object();
            body.set("ticket", json::Value::string(m_ticket));
            HttpResponse res = post(http, "/auth/mfa/sms/send", body);
            json::Value v;
            if (res.status == 200 && json::parse(res.body, v)) {
                AuthResult r;
                r.kind = AuthResult::Kind::SmsSent;
                r.phone = v["phone"].str();
                return r;
            }
            // Same error shapes as the MFA endpoints.
            AuthResult r = interpret(res, false);
            if (r.kind != AuthResult::Kind::Error && r.kind != AuthResult::Kind::Captcha)
                r = failure("Could not send a text message.");
            return r;
        },
        std::move(cb));
}

void Auth::reset()
{
    enqueue(
        [this](HttpClient &) {
            m_ticket.clear();
            m_loginInstanceId.clear();
            return AuthResult{};
        },
        nullptr);
}

void Auth::logout(const std::string &token)
{
    if (token.empty())
        return;
    enqueue(
        [this, token](HttpClient &http) {
            json::Value body = json::Value::object();
            body.set("provider", json::Value());
            body.set("voip_provider", json::Value());
            post(http, "/auth/logout", body, token);
            return AuthResult{};
        },
        nullptr);
}

}  // namespace discord
