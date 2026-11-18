// SPDX-License-Identifier: MIT
#include "discord/ratelimit.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "util/json.h"

namespace discord {

static bool allDigits(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

std::string RateLimiter::routeKey(const std::string &method, const std::string &path)
{
    std::string p = path.substr(0, path.find('?'));
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= p.size()) {
        size_t slash = p.find('/', start);
        if (slash == std::string::npos)
            slash = p.size();
        parts.push_back(p.substr(start, slash - start));
        start = slash + 1;
    }
    std::string out = method + " ";
    for (size_t i = 0; i < parts.size(); i++) {
        const std::string &prev = i > 0 ? parts[i - 1] : std::string();
        std::string seg = parts[i];
        if (allDigits(seg)) {
            bool major = prev == "channels" || prev == "guilds" || prev == "webhooks";
            if (!major)
                seg = ":id";
        } else if (prev == "reactions") {
            seg = ":emoji";
        }
        if (i > 0)
            out += '/';
        out += seg;
    }
    return out;
}

std::string RateLimiter::majorParams(const std::string &route)
{
    // The kept IDs, in order.
    std::string out;
    size_t i = 0;
    while (i < route.size()) {
        size_t j = route.find('/', i);
        if (j == std::string::npos)
            j = route.size();
        std::string seg = route.substr(i, j - i);
        if (allDigits(seg)) {
            out += seg;
            out += ':';
        }
        i = j + 1;
    }
    return out;
}

std::string RateLimiter::bucketKey(const std::string &route) const
{
    auto it = m_routeBucket.find(route);
    if (it == m_routeBucket.end())
        return "route:" + route;  // until Discord tells us the bucket
    return it->second + "|" + majorParams(route);
}

void RateLimiter::prune(uint64_t now)
{
    while (!m_recentSends.empty() && now - m_recentSends.front() >= 1000)
        m_recentSends.pop_front();
    while (!m_recentInvalid.empty() && now - m_recentInvalid.front() >= kInvalidWindowMs)
        m_recentInvalid.pop_front();
}

uint64_t RateLimiter::readyAt(const std::string &route, uint64_t now) const
{
    uint64_t at = now;
    if (m_globalUntil > at)
        at = m_globalUntil;

    // Global pacing over a sliding one-second window.
    size_t in_window = 0;
    for (uint64_t t : m_recentSends)
        if (now - t < 1000)
            in_window++;
    if (in_window >= (size_t)kGlobalPerSecond && !m_recentSends.empty())
        at = std::max(at, m_recentSends[m_recentSends.size() - kGlobalPerSecond] + 1000);

    // Too many invalid requests: wait for the oldest to leave the window.
    size_t invalid = 0;
    for (uint64_t t : m_recentInvalid)
        if (now - t < kInvalidWindowMs)
            invalid++;
    if (invalid >= (size_t)kInvalidLimit && !m_recentInvalid.empty())
        at = std::max(at, m_recentInvalid[m_recentInvalid.size() - kInvalidLimit] + kInvalidWindowMs);

    auto it = m_buckets.find(bucketKey(route));
    if (it != m_buckets.end()) {
        const Bucket &b = it->second;
        if (b.remaining == 0 && b.reset_at > now)
            at = std::max(at, b.reset_at);
    }
    return at;
}

void RateLimiter::onSend(const std::string &route, uint64_t now)
{
    prune(now);
    m_recentSends.push_back(now);
    auto it = m_buckets.find(bucketKey(route));
    if (it == m_buckets.end())
        return;
    Bucket &b = it->second;
    if (b.reset_at <= now && b.limit > 0)
        b.remaining = b.limit;  // the window rolled over
    if (b.remaining > 0)
        b.remaining--;
}

uint64_t RateLimiter::onResponse(const std::string &route, int status, const HeaderFn &header,
                                 const std::string &body, uint64_t now)
{
    prune(now);
    if (status == 401 || status == 403 || status == 429)
        m_recentInvalid.push_back(now);

    if (const std::string *hash = header("x-ratelimit-bucket"))
        m_routeBucket[route] = *hash;

    Bucket &b = m_buckets[bucketKey(route)];
    if (const std::string *v = header("x-ratelimit-limit"))
        b.limit = atoi(v->c_str());
    if (const std::string *v = header("x-ratelimit-remaining"))
        b.remaining = atoi(v->c_str());
    if (const std::string *v = header("x-ratelimit-reset-after"))
        b.reset_at = now + (uint64_t)(strtod(v->c_str(), nullptr) * 1000.0) + 50;

    if (status != 429)
        return 0;

    double retry_after = 1.0;
    bool global = false;
    json::Value err;
    if (json::parse(body, err)) {
        retry_after = err["retry_after"].asDouble(retry_after);
        global = err["global"].asBool();
    } else if (const std::string *v = header("retry-after")) {
        retry_after = strtod(v->c_str(), nullptr);
    }
    if (const std::string *v = header("x-ratelimit-global"))
        global = global || *v == "true";
    if (const std::string *v = header("x-ratelimit-scope"))
        global = global || *v == "global";

    uint64_t delay = (uint64_t)(retry_after * 1000.0) + 100;
    if (global) {
        m_globalUntil = std::max(m_globalUntil, now + delay);
    } else {
        b.remaining = 0;
        b.reset_at = std::max(b.reset_at, now + delay);
    }
    return delay;
}

}  // namespace discord
