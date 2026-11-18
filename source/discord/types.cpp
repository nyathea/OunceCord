// SPDX-License-Identifier: MIT
#include "discord/types.h"

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
    m.nonce = v["nonce"].isString() ? v["nonce"].asString() : v["nonce"].isNumber() ? std::to_string(v["nonce"].asId()) : "";
    for (const auto &a : v["attachments"].items()) {
        Attachment att;
        att.filename = a["filename"].str();
        att.url = a["url"].str();
        att.size = a["size"].asId();
        m.attachments.push_back(att);
    }
    m.embed_count = (int)v["embeds"].size();
    if (m.embed_count) {
        const auto &e = v["embeds"][(size_t)0];
        m.embed_summary = e["title"].str();
        std::string desc = e["description"].str();
        if (!desc.empty()) {
            if (!m.embed_summary.empty())
                m.embed_summary += " - ";
            m.embed_summary += desc;
        }
    }
    const auto &ref = v["referenced_message"];
    if (ref.isObject()) {
        m.has_reply = true;
        m.reply_author = User::fromJson(ref["author"]).displayName();
        m.reply_content = ref["content"].str();
    } else if (v["message_reference"].isObject() && m.type == 19) {
        m.has_reply = true;
        m.reply_content = "(original message was deleted)";
    }
    return m;
}

void Message::applyUpdate(const json::Value &v)
{
    if (v.has("content"))
        content = v["content"].str();
    if (v.has("edited_timestamp"))
        edited = !v["edited_timestamp"].isNull();
    if (v.has("embeds") || v.has("attachments")) {
        Message fresh = fromJson(v);
        if (v.has("embeds")) {
            embed_count = fresh.embed_count;
            embed_summary = fresh.embed_summary;
        }
        if (v.has("attachments"))
            attachments = fresh.attachments;
    }
}
