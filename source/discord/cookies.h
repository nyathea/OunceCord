// SPDX-License-Identifier: MIT
//
// A cookie jar for discord.com. The official client is a browser, so it
// sends back the cookies Discord sets (__dcfduid, __sdcfduid, __cfruid,
// _cfuvid...). Requests without them, particularly on the login endpoints,
// look automated and get captchas. Platform-free so it can be tested.
#pragma once

#include <map>
#include <string>
#include <string_view>

namespace discord {

class CookieJar {
public:
    // Takes one Set-Cookie header value. Returns true if the jar changed.
    bool absorb(std::string_view set_cookie);
    // The Cookie request header value ("a=1; b=2"), empty when none.
    std::string header() const;

    // One name=value per line.
    std::string serialize() const;
    void deserialize(std::string_view text);

    size_t size() const { return m_cookies.size(); }

private:
    std::map<std::string, std::string> m_cookies;
};

}  // namespace discord
