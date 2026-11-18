// SPDX-License-Identifier: MIT
//
// Discord REST rate limiting, following Discord's documented model:
// - Each route maps to a bucket (X-RateLimit-Bucket). Buckets are per major
//   parameter (channel, guild, webhook), so the same bucket hash for two
//   channels is two independent limits.
// - Responses say how many requests remain and when the bucket resets; we
//   stop sending on a bucket once it is empty, until the reset.
// - A 429 carries retry_after; with X-RateLimit-Global (or scope "global")
//   it pauses every request.
// - On top of that we pace all requests globally and back off hard when
//   many requests fail (401/403/429), since Cloudflare bans IPs that send
//   10,000 invalid requests in 10 minutes.
//
// Not thread-safe; owned by the REST worker thread. Times are milliseconds
// on any monotonic clock, passed in so the logic is testable.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

namespace discord {

class RateLimiter {
public:
    using HeaderFn = std::function<const std::string *(const char *name)>;

    // Global pacing: at most this many requests per second (Discord allows 50).
    static constexpr int kGlobalPerSecond = 40;
    // Invalid requests (401, 403, 429) allowed per 10 minutes before we
    // pause. Far below Cloudflare's 10,000.
    static constexpr int kInvalidLimit = 500;
    static constexpr uint64_t kInvalidWindowMs = 10 * 60 * 1000;

    // "GET /channels/123/messages?limit=50" -> route key
    // "GET /channels/123/messages": major parameters kept, other IDs as ":id",
    // and the query dropped.
    static std::string routeKey(const std::string &method, const std::string &path);

    // Earliest time a request on this route may be sent (<= now means now).
    uint64_t readyAt(const std::string &route, uint64_t now) const;
    // Records that a request on this route is being sent.
    void onSend(const std::string &route, uint64_t now);
    // Records a response. Returns the retry delay in ms for a 429 (0 otherwise).
    uint64_t onResponse(const std::string &route, int status, const HeaderFn &header,
                        const std::string &body, uint64_t now);

    uint64_t globalPausedUntil() const { return m_globalUntil; }

private:
    struct Bucket {
        int limit = -1;      // -1: unknown
        int remaining = -1;  // -1: unknown
        uint64_t reset_at = 0;
    };

    std::map<std::string, std::string> m_routeBucket;  // route -> bucket hash
    std::map<std::string, Bucket> m_buckets;           // hash + major -> state
    uint64_t m_globalUntil = 0;
    std::deque<uint64_t> m_recentSends;    // for global pacing
    std::deque<uint64_t> m_recentInvalid;  // for the invalid-request guard

    std::string bucketKey(const std::string &route) const;
    static std::string majorParams(const std::string &route);
    void prune(uint64_t now);
};

}  // namespace discord
