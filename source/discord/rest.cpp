// SPDX-License-Identifier: MIT
#include "discord/rest.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>

#include <switch.h>

#include "util/util.h"

namespace discord {

// Values from Paicord's macOS desktop profile (SuperProperties.swift).
static constexpr const char *kClientVersion = "0.0.398";
static constexpr const char *kElectronVersion = "37.6.0";
static constexpr const char *kChromeVersion = "138.0.7204.251";
static constexpr const char *kOsVersion = "24.5.0";
static constexpr int kClientBuildNumber = 575562;
static constexpr int kNativeBuildNumber = 85861;

Identity &Identity::get()
{
    static Identity identity;
    return identity;
}

void Identity::init()
{
    locale = "en-US";
    if (R_SUCCEEDED(setInitialize())) {
        u64 code = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&code))) {
            char buf[9] = {};
            memcpy(buf, &code, 8);
            if (buf[0])
                locale = buf;
        }
        setExit();
    }

    timezone = "Etc/UTC";
    TimeLocationName name{};
    if (R_SUCCEEDED(timeGetDeviceLocationName(&name)) && name.name[0]) {
        char buf[sizeof(name.name) + 1] = {};
        memcpy(buf, name.name, sizeof(name.name));
        timezone = buf;
    }

    launch_id = randomUuid();
    launch_signature = randomUuid();
    heartbeat_session_id = randomUuid();
}

std::string Identity::userAgent() const
{
    return std::string("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                       "(KHTML, like Gecko) discord/") +
           kClientVersion + " Chrome/" + kChromeVersion + " Electron/" + kElectronVersion +
           " Safari/537.36";
}

json::Value Identity::properties(bool ws) const
{
    using json::Value;
    Value p = Value::object();
    p.set("os", Value::string("Mac OS X"));
    p.set("browser", Value::string("Discord Client"));
    p.set("release_channel", Value::string("stable"));
    p.set("client_version", Value::string(kClientVersion));
    p.set("os_version", Value::string(kOsVersion));
    p.set("os_arch", Value::string("arm64"));
    p.set("app_arch", Value::string("arm64"));
    p.set("system_locale", Value::string(locale));
    p.set("has_client_mods", Value::boolean(false));
    p.set("client_launch_id", Value::string(launch_id));
    p.set("browser_user_agent", Value::string(userAgent()));
    p.set("browser_version", Value::string(kElectronVersion));
    p.set("os_sdk_version", Value::string("24"));
    p.set("client_build_number", Value::number((int64_t)kClientBuildNumber));
    p.set("native_build_number", Value::number((int64_t)kNativeBuildNumber));
    p.set("client_event_source", Value());
    p.set("launch_signature", Value::string(launch_signature));
    p.set("client_heartbeat_session_id", Value::string(heartbeat_session_id));
    p.set("client_app_state", Value::string("focused"));
    (void)ws;
    return p;
}

std::string Identity::superPropertiesHeader() const
{
    return base64Encode(properties(false).dump());
}

HttpHeaders Identity::restHeaders(const std::string &token) const
{
    return {
        {"Authorization", token},
        {"User-Agent", userAgent()},
        {"X-Super-Properties", superPropertiesHeader()},
        {"X-Discord-Locale", locale},
        {"X-Discord-Timezone", timezone},
        {"X-Debug-Options", "bugReporterEnabled"},
        {"Accept", "*/*"},
        {"Accept-Language", locale},
        {"Origin", "https://discord.com"},
        {"Referer", "https://discord.com/channels/@me"},
        {"Sec-Fetch-Dest", "empty"},
        {"Sec-Fetch-Mode", "cors"},
        {"Sec-Fetch-Site", "same-origin"},
    };
}

void RestWorker::start(std::string token)
{
    stop();
    m_token = std::move(token);
    m_unauthorized = false;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void RestWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running)
            return;
        m_running = false;
        m_jobs.clear();
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void RestWorker::setToken(std::string token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_token = std::move(token);
    m_unauthorized = false;
}

void RestWorker::request(std::string method, std::string path, std::string body, Callback cb,
                         HttpHeaders extra)
{
    Job job;
    job.route = RateLimiter::routeKey(method, path);
    job.method = std::move(method);
    job.path = std::move(path);
    job.body = std::move(body);
    job.extra = std::move(extra);
    job.cb = std::move(cb);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_unauthorized) {
            // The token was rejected: sending more only counts toward the
            // invalid-request limit. Fail locally.
            if (job.cb) {
                auto cb = std::move(job.cb);
                m_post([cb] {
                    HttpResponse res;
                    res.status = 401;
                    cb(res);
                });
            }
            return;
        }
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void RestWorker::deliver(Job &job, HttpResponse res)
{
    if (!job.cb)
        return;
    auto shared = std::make_shared<HttpResponse>(std::move(res));
    auto cb = std::move(job.cb);
    m_post([cb, shared] { cb(*shared); });
}

void RestWorker::run()
{
    HttpClient http(kApiHost);
    RateLimiter limiter;
    while (true) {
        Job job;
        std::string token;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running || !m_jobs.empty(); });
            if (!m_running)
                break;
            // Take the first job whose bucket is ready. Jobs on the same route
            // share readiness, so their order is kept; a busy bucket does not
            // hold up requests to other buckets.
            uint64_t now = nowMs();
            uint64_t earliest = UINT64_MAX;
            auto pick = m_jobs.end();
            for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
                uint64_t at = std::max(limiter.readyAt(it->route, now), it->not_before);
                if (at <= now) {
                    pick = it;
                    break;
                }
                earliest = std::min(earliest, at);
            }
            if (pick == m_jobs.end()) {
                // Sleep until a bucket frees up, or a new job/stop arrives.
                m_cv.wait_for(lock, std::chrono::milliseconds(std::min<uint64_t>(earliest - now, 1000)));
                continue;
            }
            job = std::move(*pick);
            m_jobs.erase(pick);
            token = m_token;
            limiter.onSend(job.route, now);
        }

        HttpHeaders headers = Identity::get().restHeaders(token);
        if (!job.body.empty())
            headers.emplace_back("Content-Type", "application/json");
        for (auto &h : job.extra)
            headers.push_back(h);

        HttpResponse res = http.request(job.method, std::string(kApiBase) + job.path, headers, job.body);
        uint64_t retry = limiter.onResponse(
            job.route, res.status, [&res](const char *name) { return res.header(name); }, res.body,
            nowMs());

        if (res.status == 429) {
            logf("rest: 429 on %s, retry in %llu ms%s", job.route.c_str(), (unsigned long long)retry,
                 limiter.globalPausedUntil() > nowMs() ? " (global)" : "");
            // Short waits are retried; long ones (slowmode, abuse limits) go
            // back to the caller with the 429 so the UI can say so.
            if (retry <= kMaxRetryWaitMs && ++job.attempts <= kMaxAttempts) {
                std::lock_guard<std::mutex> lock(m_mutex);
                job.not_before = nowMs() + retry;
                m_jobs.push_front(std::move(job));
                continue;
            }
        } else if (res.status == 0) {
            logf("rest: %s %s failed: %s", job.method.c_str(), job.route.c_str(), res.error.c_str());
        } else if (res.status >= 400) {
            logf("rest: %s -> %d %.200s", job.route.c_str(), res.status, res.body.c_str());
        }

        if (res.status == 401) {
            // Invalid token: drop everything queued and stop sending.
            std::deque<Job> dropped;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_unauthorized = true;
                dropped.swap(m_jobs);
            }
            logf("rest: token rejected (401), dropping %zu queued requests", dropped.size());
            for (auto &d : dropped) {
                HttpResponse r;
                r.status = 401;
                deliver(d, r);
            }
            if (onUnauthorized)
                m_post(onUnauthorized);
        }
        deliver(job, std::move(res));
    }
    http.close();
}

}  // namespace discord
