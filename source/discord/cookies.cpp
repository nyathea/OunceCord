// SPDX-License-Identifier: MIT
#include "discord/cookies.h"

#include <cctype>

namespace discord {

static std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

static bool iequalsPrefix(std::string_view s, std::string_view prefix)
{
    if (s.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); i++)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
            return false;
    return true;
}

bool CookieJar::absorb(std::string_view set_cookie)
{
    size_t semi = set_cookie.find(';');
    std::string_view pair = trim(set_cookie.substr(0, semi));
    size_t eq = pair.find('=');
    if (eq == std::string_view::npos || eq == 0)
        return false;
    std::string name(trim(pair.substr(0, eq)));
    std::string value(trim(pair.substr(eq + 1)));
    // Values must be safe to put back in a header and in our file.
    for (char c : name + value)
        if ((unsigned char)c < 0x21 || c == ';' || c == ',' || c == 0x7f)
            return false;

    bool remove = value.empty();
    std::string_view attrs = semi == std::string_view::npos ? std::string_view() : set_cookie.substr(semi + 1);
    while (!attrs.empty()) {
        size_t next = attrs.find(';');
        std::string_view a = trim(attrs.substr(0, next));
        if (iequalsPrefix(a, "max-age=")) {
            std::string_view v = a.substr(8);
            if (!v.empty() && (v[0] == '-' || v == "0"))
                remove = true;
        }
        if (next == std::string_view::npos)
            break;
        attrs.remove_prefix(next + 1);
    }

    auto it = m_cookies.find(name);
    if (remove) {
        if (it == m_cookies.end())
            return false;
        m_cookies.erase(it);
        return true;
    }
    if (it != m_cookies.end() && it->second == value)
        return false;
    m_cookies[name] = value;
    return true;
}

std::string CookieJar::header() const
{
    std::string out;
    for (const auto &c : m_cookies) {
        if (!out.empty())
            out += "; ";
        out += c.first + "=" + c.second;
    }
    return out;
}

std::string CookieJar::serialize() const
{
    std::string out;
    for (const auto &c : m_cookies)
        out += c.first + "=" + c.second + "\n";
    return out;
}

void CookieJar::deserialize(std::string_view text)
{
    m_cookies.clear();
    while (!text.empty()) {
        size_t nl = text.find('\n');
        absorb(text.substr(0, nl));
        if (nl == std::string_view::npos)
            break;
        text.remove_prefix(nl + 1);
    }
}

}  // namespace discord
