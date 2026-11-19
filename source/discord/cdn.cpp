// SPDX-License-Identifier: MIT
#include "discord/cdn.h"

#include <cctype>
#include <initializer_list>
#include <cstdlib>
#include <vector>

namespace discord::cdn {

static std::string sized(std::string path, int size)
{
    return path + "?size=" + std::to_string(size);
}

std::string avatarPath(const User &u, int size)
{
    if (!u.avatar.empty())
        return sized("/avatars/" + std::to_string(u.id) + "/" + u.avatar + ".png", size);
    // Default avatars: by user ID on the new username system, by
    // discriminator for legacy names.
    int index;
    if (u.discriminator.empty() || u.discriminator == "0")
        index = (int)((u.id >> 22) % 6);
    else
        index = atoi(u.discriminator.c_str()) % 5;
    return "/embed/avatars/" + std::to_string(index) + ".png";
}

std::string guildIconPath(const Guild &g, int size)
{
    if (g.icon.empty())
        return {};
    return sized("/icons/" + std::to_string(g.id) + "/" + g.icon + ".png", size);
}

std::string dmIconPath(const Channel &c, int size)
{
    if (!c.icon.empty())
        return sized("/channel-icons/" + std::to_string(c.id) + "/" + c.icon + ".png", size);
    if (c.type == ChannelType_DM && !c.recipients.empty())
        return avatarPath(c.recipients[0], size);
    return {};
}

// Discord's media proxies, which resize on request.
static bool isProxyHost(const std::string &host)
{
    if (host == "media.discordapp.net")
        return true;
    // images-ext-1.discordapp.net, images-ext-2.discordapp.net, ...
    const std::string prefix = "images-ext-", suffix = ".discordapp.net";
    if (host.size() <= prefix.size() + suffix.size() || host.compare(0, prefix.size(), prefix) != 0 ||
        host.compare(host.size() - suffix.size(), suffix.size(), suffix) != 0)
        return false;
    for (size_t i = prefix.size(); i < host.size() - suffix.size(); i++)
        if (!isdigit((unsigned char)host[i]))
            return false;
    return true;
}

static bool isImageHost(const std::string &host)
{
    if (host == "cdn.discordapp.com" || isProxyHost(host))
        return true;
    // GIF providers, for gifv embeds.
    if (host == "media.tenor.com" || host == "media.giphy.com")
        return true;
    return host.size() == 16 && host.compare(0, 5, "media") == 0 && isdigit((unsigned char)host[5]) &&
           host.compare(6, 10, ".giphy.com") == 0;
}

bool splitImageUrl(const std::string &url, std::string &host, std::string &path)
{
    const std::string scheme = "https://";
    if (url.compare(0, scheme.size(), scheme) != 0)
        return false;
    size_t slash = url.find('/', scheme.size());
    host = url.substr(scheme.size(), slash == std::string::npos ? std::string::npos : slash - scheme.size());
    path = slash == std::string::npos ? "/" : url.substr(slash);
    for (char &c : host)
        c = (char)tolower((unsigned char)c);
    return isImageHost(host);
}

// Splits off the query and drops parameters whose names are in `drop`.
static std::string filterQuery(const std::string &url, std::initializer_list<const char *> drop,
                               std::string &base)
{
    size_t q = url.find('?');
    base = url.substr(0, q);
    if (q == std::string::npos)
        return {};
    std::string out, query = url.substr(q + 1);
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        std::string param = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        std::string name = param.substr(0, param.find('='));
        bool keep = !param.empty();
        for (const char *d : drop)
            if (name == d)
                keep = false;
        if (keep)
            out += (out.empty() ? "" : "&") + param;
        if (amp == std::string::npos)
            break;
        pos = amp + 1;
    }
    return out;
}

static std::string lowerExtension(const std::string &path)
{
    size_t slash = path.rfind('/');
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string ext = path.substr(dot + 1);
    for (auto &c : ext)
        c = (char)tolower((unsigned char)c);
    return ext;
}

std::string sizedImageUrl(const std::string &url, int width, int height)
{
    std::string host, path;
    if (!splitImageUrl(url, host, path) || !isProxyHost(host))
        return url;
    std::string base;
    std::string query = filterQuery(url, {"width", "height", "format", "quality"}, base);
    std::string ext = lowerExtension(base);
    bool decodable = ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif";
    std::string extra = "width=" + std::to_string(width) + "&height=" + std::to_string(height);
    if (!decodable)
        extra += "&format=png";
    return base + "?" + (query.empty() ? extra : query + "&" + extra);
}

std::string attachmentImageUrl(const Attachment &a)
{
    if (!a.proxy_url.empty())
        return a.proxy_url;
    const std::string cdn = "https://cdn.discordapp.com/";
    if (a.url.compare(0, cdn.size(), cdn) == 0)
        return "https://media.discordapp.net/" + a.url.substr(cdn.size());
    return a.url;
}

std::string imageCacheKey(const std::string &url)
{
    std::string base;
    std::string query = filterQuery(url, {"ex", "is", "hm"}, base);
    return query.empty() ? base : base + "?" + query;
}

}  // namespace discord::cdn
