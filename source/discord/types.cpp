// SPDX-License-Identifier: MIT
#include "discord/types.h"

#include <cctype>

#include <algorithm>

User User::fromJson(const json::Value &v)
{
    User u;
    u.id = v["id"].asId();
    u.username = v["username"].str();
    u.global_name = v["global_name"].str();
    u.avatar = v["avatar"].str();
    u.discriminator = v["discriminator"].str("0");
    u.bot = v["bot"].asBool();
    return u;
}

std::string Channel::label() const
{
    if (isDm()) {
        if (!name.empty())
            return name;
        std::string out;
        for (const auto &r : recipients) {
            if (!out.empty())
                out += ", ";
            out += r.displayName();
        }
        return out.empty() ? "Unnamed DM" : out;
    }
    if (type == ChannelType_GuildCategory)
        return name;
    if (type == ChannelType_GuildVoice || type == ChannelType_GuildStageVoice)
        return "~ " + name;
    return "# " + name;
}

void Channel::applyJson(const json::Value &v, const std::vector<User> *users)
{
    if (v.has("id"))
        id = v["id"].asId();
    if (v.has("guild_id"))
        guild_id = v["guild_id"].asId();
    if (v.has("parent_id"))
        parent_id = v["parent_id"].asId();
    if (v.has("last_message_id"))
        last_message_id = v["last_message_id"].asId();
    if (v.has("type"))
        type = (int)v["type"].asInt();
    if (v.has("position"))
        position = (int)v["position"].asInt();
    if (v.has("rate_limit_per_user"))
        rate_limit_per_user = (int)v["rate_limit_per_user"].asInt();
    if (v.has("name"))
        name = v["name"].str();
    if (v.has("topic"))
        topic = v["topic"].str();
    if (v.has("icon"))
        icon = v["icon"].str();
    if (v.has("permission_overwrites")) {
        overwrites.clear();
        for (const auto &o : v["permission_overwrites"].items()) {
            Overwrite ow;
            ow.id = o["id"].asId();
            ow.type = (int)o["type"].asInt();
            ow.allow = (uint64_t)o["allow"].asId();
            ow.deny = (uint64_t)o["deny"].asId();
            overwrites.push_back(ow);
        }
    }
    if (v.has("recipients")) {
        recipients.clear();
        for (const auto &r : v["recipients"].items())
            recipients.push_back(User::fromJson(r));
    } else if (v.has("recipient_ids") && users) {
        recipients.clear();
        for (const auto &rid : v["recipient_ids"].items()) {
            Snowflake uid = rid.asId();
            auto it = std::find_if(users->begin(), users->end(),
                                   [uid](const User &u) { return u.id == uid; });
            if (it != users->end()) {
                recipients.push_back(*it);
            } else {
                User u;
                u.id = uid;
                u.username = std::to_string(uid);
                recipients.push_back(u);
            }
        }
    }
}

void Guild::applyJson(const json::Value &v)
{
    // With newer client-state capabilities the guild's own fields are nested
    // in "properties"; read both layouts.
    const json::Value &props = v.has("properties") ? v["properties"] : v;
    id = v["id"].asId();
    if (props.has("name"))
        name = props["name"].str();
    if (props.has("owner_id"))
        owner_id = props["owner_id"].asId();
    if (props.has("icon"))
        icon = props["icon"].str();
    unavailable = v["unavailable"].asBool();
    if (v.has("roles")) {
        roles.clear();
        for (const auto &r : v["roles"].items()) {
            Role role;
            role.id = r["id"].asId();
            role.name = r["name"].str();
            role.permissions = r["permissions"].asId();
            role.position = (int)r["position"].asInt();
            role.color = (uint32_t)r["color"].asInt();
            roles.push_back(role);
        }
    }
    if (v.has("channels")) {
        channels.clear();
        for (const auto &c : v["channels"].items()) {
            Channel ch;
            ch.guild_id = id;
            ch.applyJson(c);
            ch.guild_id = id;
            channels.push_back(std::move(ch));
        }
    }
    if (v.has("threads")) {
        for (const auto &c : v["threads"].items()) {
            Channel ch;
            ch.applyJson(c);
            ch.guild_id = id;
            channels.push_back(std::move(ch));
        }
    }
    sortChannels();
}

Channel *Guild::findChannel(Snowflake cid)
{
    for (auto &c : channels)
        if (c.id == cid)
            return &c;
    return nullptr;
}

uint64_t Guild::permissionsIn(const Channel &c, Snowflake me) const
{
    if (me == owner_id)
        return perm::All;
    uint64_t base = 0;
    for (const auto &r : roles) {
        if (r.id == id)  // @everyone
            base |= r.permissions;
        else if (std::find(my_roles.begin(), my_roles.end(), r.id) != my_roles.end())
            base |= r.permissions;
    }
    if (base & perm::Administrator)
        return perm::All;

    const Channel *source = &c;
    // Threads take their permissions from the parent channel.
    if (c.type == ChannelType_PublicThread || c.type == ChannelType_PrivateThread ||
        c.type == ChannelType_AnnouncementThread) {
        for (const auto &p : channels)
            if (p.id == c.parent_id)
                source = &p;
    }

    uint64_t p = base;
    for (const auto &o : source->overwrites) {
        if (o.type == 0 && o.id == id) {
            p &= ~o.deny;
            p |= o.allow;
        }
    }
    uint64_t allow = 0, deny = 0;
    for (const auto &o : source->overwrites) {
        if (o.type == 0 && o.id != id &&
            std::find(my_roles.begin(), my_roles.end(), o.id) != my_roles.end()) {
            allow |= o.allow;
            deny |= o.deny;
        }
    }
    p &= ~deny;
    p |= allow;
    for (const auto &o : source->overwrites) {
        if (o.type == 1 && o.id == me) {
            p &= ~o.deny;
            p |= o.allow;
        }
    }
    return p;
}

void Guild::sortChannels()
{
    // Category positions, read before sorting: the comparator must not look
    // into the vector while it is being reordered.
    std::vector<std::pair<Snowflake, int>> categories;
    for (const auto &c : channels)
        if (c.type == ChannelType_GuildCategory)
            categories.emplace_back(c.id, c.position);
    auto categoryPos = [&](const Channel &c) -> std::pair<int, Snowflake> {
        if (c.type == ChannelType_GuildCategory)
            return {c.position, c.id};
        if (c.parent_id == 0)
            return {-1, 0};
        for (const auto &p : categories)
            if (p.first == c.parent_id)
                return {p.second, p.first};
        return {-1, 0};
    };
    auto isVoice = [](const Channel &c) {
        return c.type == ChannelType_GuildVoice || c.type == ChannelType_GuildStageVoice;
    };
    std::stable_sort(channels.begin(), channels.end(), [&](const Channel &a, const Channel &b) {
        auto ca = categoryPos(a), cb = categoryPos(b);
        if (ca != cb)
            return ca < cb;
        bool a_cat = a.type == ChannelType_GuildCategory;
        bool b_cat = b.type == ChannelType_GuildCategory;
        if (a_cat != b_cat)
            return a_cat;
        if (isVoice(a) != isVoice(b))
            return !isVoice(a);
        if (a.position != b.position)
            return a.position < b.position;
        return a.id < b.id;
    });
}

namespace {

void parseMentions(const json::Value &arr, std::vector<std::pair<Snowflake, std::string>> &names,
                   std::vector<Snowflake> *ids)
{
    for (const auto &u : arr.items()) {
        Snowflake id = u["id"].asId();
        if (!id)
            continue;
        std::string name = u["member"]["nick"].str();
        if (name.empty())
            name = User::fromJson(u).displayName();
        names.emplace_back(id, std::move(name));
        if (ids)
            ids->push_back(id);
    }
}

}  // namespace

const std::string *Message::mentionName(Snowflake id) const
{
    for (const auto &[uid, name] : mention_names)
        if (uid == id)
            return &name;
    return nullptr;
}

Message Message::fromJson(const json::Value &v)
{
    Message m;
    m.id = v["id"].asId();
    m.channel_id = v["channel_id"].asId();
    m.guild_id = v["guild_id"].asId();
    m.author = User::fromJson(v["author"]);
    m.content = v["content"].str();
    m.type = (int)v["type"].asInt();
    m.edited = !v["edited_timestamp"].isNull();
    parseMentions(v["mentions"], m.mention_names, &m.mention_users);
    for (const auto &r : v["mention_roles"].items())
        m.mention_roles.push_back(r.asId());
    m.mention_everyone = v["mention_everyone"].asBool();
    m.nonce = v["nonce"].isString() ? v["nonce"].asString() : v["nonce"].isNumber() ? std::to_string(v["nonce"].asId()) : "";
    for (const auto &a : v["attachments"].items()) {
        Attachment att;
        att.filename = a["filename"].str();
        att.url = a["url"].str();
        att.proxy_url = a["proxy_url"].str();
        att.content_type = a["content_type"].str();
        att.size = a["size"].asId();
        att.width = (int)a["width"].asInt();
        att.height = (int)a["height"].asInt();
        att.spoiler = att.filename.rfind("SPOILER_", 0) == 0 || (a["flags"].asInt() & 8);
        m.attachments.push_back(att);
    }
    m.embed_count = (int)v["embeds"].size();
    for (const auto &e : v["embeds"].items()) {
        const std::string &type = e["type"].asString();
        // Image and GIF links: the thumbnail is the picture (a still for gifv).
        // Other embeds: their large image, if any.
        const json::Value &img = (type == "image" || type == "gifv") ? e["thumbnail"] : e["image"];
        std::string url = img["proxy_url"].str();
        if (url.empty())
            url = img["url"].str();
        if (!url.empty() && m.embed_images.size() < 4) {
            EmbedImage ei;
            ei.url = url;
            ei.width = (int)img["width"].asInt();
            ei.height = (int)img["height"].asInt();
            ei.gifv = type == "gifv";
            ei.link = e["url"].str();
            ei.media_only = type == "gifv" || type == "image";
            ei.provider = e["provider"]["name"].str();
            if (ei.gifv) {
                ei.video_url = e["video"]["url"].str();
                ei.animated_url = gifvAnimatedUrl(ei.video_url);
            }
            m.embed_images.push_back(std::move(ei));
        }
    }
    for (const auto &e : v["embeds"].items()) {
        // The first embed with text gives the summary line.
        std::string summary = e["title"].str();
        std::string desc = e["description"].str();
        if (!desc.empty()) {
            if (!summary.empty())
                summary += " - ";
            summary += desc;
        }
        if (!summary.empty()) {
            m.embed_summary = summary;
            break;
        }
    }
    const auto &ref = v["referenced_message"];
    if (ref.isObject()) {
        m.has_reply = true;
        m.reply_author = User::fromJson(ref["author"]).displayName();
        m.reply_content = ref["content"].str();
        parseMentions(ref["mentions"], m.mention_names, nullptr);
    } else if (v["message_reference"].isObject() && m.type == 19) {
        m.has_reply = true;
        m.reply_content = "(original message was deleted)";
    }
    return m;
}

std::string gifvAnimatedUrl(const std::string &video_url)
{
    auto endsWith = [](const std::string &s, const std::string &suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    // Tenor: https://media.tenor.com/<id>AAAPo/<name>.mp4, where the last
    // five characters of the id pick the format: AAAPo is MP4, AAAAC GIF.
    const std::string tenor = "https://media.tenor.com/";
    if (video_url.compare(0, tenor.size(), tenor) == 0 && endsWith(video_url, ".mp4")) {
        size_t slash = video_url.find('/', tenor.size());
        if (slash == std::string::npos || slash - tenor.size() <= 5 || video_url.find('?') != std::string::npos)
            return {};
        std::string id = video_url.substr(tenor.size(), slash - tenor.size() - 5);
        std::string name = video_url.substr(slash + 1, video_url.size() - slash - 1 - 4);
        if (name.find('/') != std::string::npos)
            return {};
        return tenor + id + "AAAAC/" + name + ".gif";
    }
    // Giphy: https://media.giphy.com/media/<id>/giphy.mp4 (or mediaN.giphy.com)
    const std::string giphy_suffix = "/giphy.mp4";
    if (endsWith(video_url, giphy_suffix) && video_url.find('?') == std::string::npos) {
        std::string host = video_url.compare(0, 8, "https://") == 0
                               ? video_url.substr(8, video_url.find('/', 8) - 8)
                               : std::string();
        bool media_host = host == "media.giphy.com" ||
                          (host.size() == 16 && host.compare(0, 5, "media") == 0 && isdigit((unsigned char)host[5]) &&
                           host.compare(6, 10, ".giphy.com") == 0);
        if (media_host)
            return video_url.substr(0, video_url.size() - 4) + ".gif";
    }
    // Klipy names each format of a GIF with its own random file name, so its
    // GIF can't be derived from the MP4; Klipy GIFs show as a still.
    return {};
}

bool Message::contentIsEmbedLink() const
{
    size_t a = content.find_first_not_of(" \n\t"), b = content.find_last_not_of(" \n\t");
    if (a == std::string::npos)
        return false;
    std::string text = content.substr(a, b - a + 1);
    // <https://...> suppresses embeds, so it never matches one.
    for (const auto &e : embed_images)
        if (e.media_only && !e.link.empty() && text == e.link)
            return true;
    return false;
}

bool Attachment::isImage() const
{
    if (content_type.rfind("image/", 0) == 0)
        return true;
    if (!content_type.empty() || width <= 0 || height <= 0)
        return false;
    // Older messages may lack content_type: go by the extension.
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = filename.substr(dot + 1);
    for (auto &c : ext)
        c = (char)tolower((unsigned char)c);
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp";
}

void Message::applyUpdate(const json::Value &v)
{
    if (v.has("content"))
        content = v["content"].str();
    if (v.has("edited_timestamp"))
        edited = !v["edited_timestamp"].isNull();
    if (v.has("mentions") || v.has("mention_roles") || v.has("mention_everyone")) {
        Message fresh = fromJson(v);
        if (v.has("mentions")) {
            // Keep the reply preview's names, replace the message's own.
            for (auto &n : fresh.mention_names)
                if (!mentionName(n.first))
                    mention_names.push_back(n);
            for (auto &n : mention_names)
                if (const std::string *nn = fresh.mentionName(n.first))
                    n.second = *nn;
            mention_users = fresh.mention_users;
        }
        if (v.has("mention_roles"))
            mention_roles = fresh.mention_roles;
        if (v.has("mention_everyone"))
            mention_everyone = fresh.mention_everyone;
    }
    if (v.has("embeds") || v.has("attachments")) {
        Message fresh = fromJson(v);
        if (v.has("embeds")) {
            embed_count = fresh.embed_count;
            embed_summary = fresh.embed_summary;
            embed_images = fresh.embed_images;
        }
        if (v.has("attachments"))
            attachments = fresh.attachments;
    }
}
