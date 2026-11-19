// SPDX-License-Identifier: MIT
//
// Browser-like state shared by every request to discord.com: the cookie jar
// and, for requests made before logging in, the device fingerprint from
// GET /experiments. Both are kept on the SD card so the console looks like
// the same device from one run to the next. Thread-safe.
#pragma once

#include <mutex>
#include <string>

#include "discord/cookies.h"
#include "net/http.h"

namespace discord {

class ClientSession {
public:
    static ClientSession &get();

    // Adds the Cookie header, and X-Fingerprint when fingerprint is true
    // (unauthenticated login requests).
    void apply(HttpHeaders &headers, bool fingerprint);
    // Stores cookies from a response's Set-Cookie headers.
    void absorb(const HttpResponse &res);
    // Makes sure we have a fingerprint (and Discord's first cookies),
    // asking Discord if needed. On failure, error says why.
    bool ensureFingerprint(HttpClient &http, std::string &error);

private:
    std::mutex m_mutex;
    bool m_loaded = false;
    CookieJar m_jar;
    std::string m_fingerprint;

    void loadLocked();
};

}  // namespace discord
