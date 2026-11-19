// SPDX-License-Identifier: MIT
#include "discord/session.h"

#include <strings.h>

#include "config.h"
#include "discord/rest.h"
#include "util/json.h"
#include "util/util.h"

namespace discord {

ClientSession &ClientSession::get()
{
    static ClientSession session;
    return session;
}

void ClientSession::loadLocked()
{
    if (m_loaded)
        return;
    m_loaded = true;
    std::string text;
    if (config::readText(config::kCookiesPath, text, 16 * 1024))
        m_jar.deserialize(text);
    if (config::readText(config::kFingerprintPath, text, 256))
        m_fingerprint = config::normalizeToken(text);
}

void ClientSession::apply(HttpHeaders &headers, bool fingerprint)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    loadLocked();
    std::string cookie = m_jar.header();
    if (!cookie.empty())
        headers.emplace_back("Cookie", cookie);
    if (fingerprint && !m_fingerprint.empty())
        headers.emplace_back("X-Fingerprint", m_fingerprint);
}

void ClientSession::absorb(const HttpResponse &res)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    loadLocked();
    bool changed = false;
    for (const auto &h : res.headers)
        if (strcasecmp(h.first.c_str(), "set-cookie") == 0)
            changed |= m_jar.absorb(h.second);
    if (changed)
        config::writeText(config::kCookiesPath, m_jar.serialize());
}

bool ClientSession::ensureFingerprint(HttpClient &http, std::string &error)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        loadLocked();
        // Without cookies (e.g. a fingerprint saved by an older version),
        // ask again: /experiments is also where Discord sets them.
        if (!m_fingerprint.empty() && m_jar.size() > 0)
            return true;
    }
    HttpHeaders headers = Identity::get().restHeaders({}, "https://discord.com/login");
    apply(headers, false);
    HttpResponse res = http.request("GET", std::string(kApiBase) + "/experiments", headers);
    absorb(res);
    json::Value v;
    if (res.status == 200 && json::parse(res.body, v) && v["fingerprint"].isString()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fingerprint = v["fingerprint"].asString();
        config::writeText(config::kFingerprintPath, m_fingerprint);
        logf("session: got a fingerprint (%zu cookies)", m_jar.size());
        return true;
    }
    logf("session: /experiments -> %d %s", res.status, res.error.c_str());
    error = res.status == 0 ? "Could not reach Discord. Check the internet connection."
                            : "Discord would not start a login. Try again later.";
    return false;
}

}  // namespace discord
