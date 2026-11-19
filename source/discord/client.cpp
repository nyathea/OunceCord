// SPDX-License-Identifier: MIT
#include "discord/client.h"

#include <algorithm>

#include <switch.h>

#include "util/util.h"

namespace discord {

static constexpr size_t kMaxCachedMessages = 300;

Client::Client()
    : m_alive(std::make_shared<bool>(true)),
      m_rest([this](std::function<void()> fn) { post(std::move(fn)); }),
      m_gateway([this](std::function<void()> fn) { post(std::move(fn)); }, this)
{
    m_rest.onUnauthorized = [this] {
        m_gateway.stop();
        onStatus(GatewayStatus::AuthFailed, "Discord rejected the token (401).");
    };
}

Client::~Client()
{
    stop();
    *m_alive = false;
}

void Client::post(std::function<void()> fn)
{
    std::lock_guard<std::mutex> lock(m_postMutex);
    m_posted.push_back(std::move(fn));
}

void Client::update()
{
    std::vector<std::function<void()>> run;
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        run.swap(m_posted);
    }
    for (auto &fn : run)
        fn();
}

void Client::start(const std::string &token)
{
    stop();
    m_token = token;
    m_ready = false;
    m_guilds.clear();
    m_dms.clear();
    m_messages.clear();
    m_subscribed.clear();
    m_fetchingChannels.clear();
    m_lastSent.clear();
    m_status = GatewayStatus::Connecting;
    m_rest.start(token);
    m_gateway.start(token);
}

void Client::stop()
{
    m_gateway.stop();
    m_rest.stop();
    std::lock_guard<std::mutex> lock(m_postMutex);
    m_posted.clear();
}

Guild *Client::findGuild(Snowflake id)
{
    for (auto &g : m_guilds)
        if (g.id == id)
            return &g;
    return nullptr;
}

Channel *Client::findChannel(Snowflake id)
{
    for (auto &c : m_dms)
        if (c.id == id)
            return &c;
    for (auto &g : m_guilds)
        if (Channel *c = g.findChannel(id))
            return c;
    return nullptr;
}

bool Client::canView(const Channel &c)
{
    if (c.guild_id == 0)
        return true;
    Guild *g = findGuild(c.guild_id);
    if (!g)
        return true;
    return g->permissionsIn(c, m_me.id) & perm::ViewChannel;
}

bool Client::canSend(const Channel &c)
{
    if (c.guild_id == 0)
        return true;
    Guild *g = findGuild(c.guild_id);
    if (!g)
        return true;
    uint64_t p = g->permissionsIn(c, m_me.id);
    return (p & perm::ViewChannel) && (p & perm::SendMessages);
}

void Client::onStatus(GatewayStatus status, const std::string &detail)
{
    m_status = status;
    m_statusDetail = detail;
}

void Client::sortDms()
{
    std::sort(m_dms.begin(), m_dms.end(), [](const Channel &a, const Channel &b) {
        Snowflake la = a.last_message_id ? a.last_message_id : a.id;
        Snowflake lb = b.last_message_id ? b.last_message_id : b.id;
        return la > lb;
    });
}

void Client::handleReady(const json::Value &d)
{
    m_me = User::fromJson(d["user"]);
    m_guilds.clear();
    m_dms.clear();
    m_messages.clear();
    m_subscribed.clear();

    std::vector<User> users;
    for (const auto &u : d["users"].items())
        users.push_back(User::fromJson(u));

    const auto &guilds = d["guilds"];
    const auto &merged = d["merged_members"];
    for (size_t i = 0; i < guilds.size(); i++) {
        Guild g;
        g.applyJson(guilds[i]);
        // Our own member entry: merged_members (aligned with guilds) or the
        // guild's members list.
        auto findRoles = [&](const json::Value &members) {
            for (const auto &m : members.items()) {
                Snowflake uid = m.has("user_id") ? m["user_id"].asId() : m["user"]["id"].asId();
                if (uid == m_me.id) {
                    for (const auto &r : m["roles"].items())
                        g.my_roles.push_back(r.asId());
                    return true;
                }
            }
            return false;
        };
        if (!findRoles(merged[i]))
            findRoles(guilds[i]["members"]);
        m_guilds.push_back(std::move(g));
    }

    // Order guilds as the user arranged them, when settings say so
    // (guild_folders in the JSON user settings); otherwise keep READY order.
    const auto &folders = d["user_settings"]["guild_folders"];
    if (folders.isArray() && folders.size()) {
        std::vector<Snowflake> order;
        for (const auto &f : folders.items())
            for (const auto &gid : f["guild_ids"].items())
                order.push_back(gid.asId());
        std::stable_sort(m_guilds.begin(), m_guilds.end(), [&](const Guild &a, const Guild &b) {
            auto ia = std::find(order.begin(), order.end(), a.id);
            auto ib = std::find(order.begin(), order.end(), b.id);
            return ia < ib;
        });
    }

    for (const auto &c : d["private_channels"].items()) {
        Channel ch;
        ch.applyJson(c, &users);
        m_dms.push_back(std::move(ch));
    }
    sortDms();

    std::string refreshed = d["auth_token"].str();
    if (!refreshed.empty() && refreshed != m_token) {
        m_token = refreshed;
        m_rest.setToken(refreshed);
        m_gateway.setToken(refreshed);
        if (onTokenRefresh)
            onTokenRefresh(refreshed);
    }

    m_ready = true;
    m_structureVersion++;
    logf("client: ready as %s, %zu guilds, %zu DMs", m_me.username.c_str(), m_guilds.size(),
         m_dms.size());
}

void Client::handleGuildCreate(const json::Value &d)
{
    Snowflake id = d["id"].asId();
    Guild *g = findGuild(id);
    if (!g) {
        m_guilds.emplace_back();
        g = &m_guilds.back();
    }
    std::vector<Snowflake> roles = g->my_roles;
    g->applyJson(d);
    g->my_roles = roles;
    for (const auto &m : d["members"].items()) {
        if (m["user"]["id"].asId() == m_me.id) {
            g->my_roles.clear();
            for (const auto &r : m["roles"].items())
                g->my_roles.push_back(r.asId());
        }
    }
    m_structureVersion++;
}

void Client::handleChannelUpsert(const json::Value &d)
{
    Snowflake id = d["id"].asId();
    Snowflake gid = d["guild_id"].asId();
    if (gid == 0) {
        auto it = std::find_if(m_dms.begin(), m_dms.end(), [id](const Channel &c) { return c.id == id; });
        if (it == m_dms.end()) {
            m_dms.emplace_back();
            it = m_dms.end() - 1;
        }
        it->applyJson(d);
        sortDms();
    } else if (Guild *g = findGuild(gid)) {
        Channel *c = g->findChannel(id);
        if (!c) {
            g->channels.emplace_back();
            c = &g->channels.back();
        }
        c->applyJson(d);
        c->guild_id = gid;
        g->sortChannels();
    }
    m_structureVersion++;
}

void Client::handleChannelDelete(const json::Value &d)
{
    Snowflake id = d["id"].asId();
    m_dms.erase(std::remove_if(m_dms.begin(), m_dms.end(), [id](const Channel &c) { return c.id == id; }),
                m_dms.end());
    for (auto &g : m_guilds)
        g.channels.erase(std::remove_if(g.channels.begin(), g.channels.end(),
                                        [id](const Channel &c) { return c.id == id; }),
                         g.channels.end());
    m_messages.erase(id);
    m_structureVersion++;
}

void Client::handleMessageCreate(const json::Value &d)
{
    Message m = Message::fromJson(d);
    if (Channel *c = findChannel(m.channel_id)) {
        c->last_message_id = m.id;
        if (c->isDm()) {
            sortDms();
            m_structureVersion++;
        }
    } else if (d["guild_id"].isNull() && !m_fetchingChannels.count(m.channel_id)) {
        // A DM we did not know about; fetch it (once) so it shows up.
        Snowflake cid = m.channel_id;
        m_fetchingChannels.insert(cid);
        std::weak_ptr<bool> alive = m_alive;
        m_rest.request("GET", "/channels/" + std::to_string(cid), "",
                       [this, alive, cid](const HttpResponse &res) {
                           if (alive.expired())
                               return;
                           m_fetchingChannels.erase(cid);
                           if (res.status != 200)
                               return;
                           json::Value v;
                           if (json::parse(res.body, v))
                               handleChannelUpsert(v);
                       });
    }

    // Clear the author's typing indicator.
    auto tit = m_typing.find(m.channel_id);
    if (tit != m_typing.end())
        tit->second.erase(std::remove_if(tit->second.begin(), tit->second.end(),
                                         [&](const auto &t) { return t.first == m.author.id; }),
                          tit->second.end());

    auto it = m_messages.find(m.channel_id);
    if (it == m_messages.end() || !it->second.loaded)
        return;
    auto &list = it->second.messages;
    // Replace our pending copy, matched by nonce.
    if (!m.nonce.empty()) {
        for (auto &existing : list) {
            if (existing.pending && existing.nonce == m.nonce) {
                existing = std::move(m);
                m_messagesVersion++;
                return;
            }
        }
    }
    for (const auto &existing : list)
        if (existing.id == m.id)
            return;
    list.push_back(std::move(m));
    if (list.size() > kMaxCachedMessages) {
        list.erase(list.begin(), list.begin() + (list.size() - kMaxCachedMessages));
        it->second.reached_start = false;
    }
    m_messagesVersion++;
}

void Client::onDispatch(const std::string &type, const json::Value &d)
{
    if (type == "READY") {
        handleReady(d);
    } else if (type == "MESSAGE_CREATE") {
        handleMessageCreate(d);
    } else if (type == "MESSAGE_UPDATE") {
        auto it = m_messages.find(d["channel_id"].asId());
        if (it == m_messages.end())
            return;
        Snowflake id = d["id"].asId();
        for (auto &m : it->second.messages) {
            if (m.id == id) {
                m.applyUpdate(d);
                m_messagesVersion++;
            }
        }
    } else if (type == "MESSAGE_DELETE") {
        auto it = m_messages.find(d["channel_id"].asId());
        if (it == m_messages.end())
            return;
        Snowflake id = d["id"].asId();
        auto &list = it->second.messages;
        list.erase(std::remove_if(list.begin(), list.end(), [id](const Message &m) { return m.id == id; }),
                   list.end());
        m_messagesVersion++;
    } else if (type == "CHANNEL_CREATE" || type == "CHANNEL_UPDATE" ||
               type == "THREAD_CREATE" || type == "THREAD_UPDATE") {
        handleChannelUpsert(d);
    } else if (type == "CHANNEL_DELETE" || type == "THREAD_DELETE") {
        handleChannelDelete(d);
    } else if (type == "GUILD_CREATE") {
        handleGuildCreate(d);
    } else if (type == "GUILD_UPDATE") {
        if (Guild *g = findGuild(d["id"].asId())) {
            const json::Value &props = d.has("properties") ? d["properties"] : d;
            if (props.has("name"))
                g->name = props["name"].str();
            if (props.has("owner_id"))
                g->owner_id = props["owner_id"].asId();
            if (props.has("icon"))
                g->icon = props["icon"].str();
            m_structureVersion++;
        }
    } else if (type == "GUILD_DELETE") {
        Snowflake id = d["id"].asId();
        if (d["unavailable"].asBool()) {
            if (Guild *g = findGuild(id))
                g->unavailable = true;
        } else {
            m_guilds.erase(std::remove_if(m_guilds.begin(), m_guilds.end(),
                                          [id](const Guild &g) { return g.id == id; }),
                           m_guilds.end());
        }
        m_structureVersion++;
    } else if (type == "GUILD_ROLE_CREATE" || type == "GUILD_ROLE_UPDATE") {
        if (Guild *g = findGuild(d["guild_id"].asId())) {
            const auto &r = d["role"];
            Snowflake rid = r["id"].asId();
            Role *role = nullptr;
            for (auto &x : g->roles)
                if (x.id == rid)
                    role = &x;
            if (!role) {
                g->roles.emplace_back();
                role = &g->roles.back();
            }
            role->id = rid;
            role->name = r["name"].str();
            role->permissions = r["permissions"].asId();
            role->position = (int)r["position"].asInt();
            role->color = (uint32_t)r["color"].asInt();
            m_structureVersion++;
        }
    } else if (type == "GUILD_MEMBER_UPDATE") {
        if (d["user"]["id"].asId() == m_me.id) {
            if (Guild *g = findGuild(d["guild_id"].asId())) {
                g->my_roles.clear();
                for (const auto &r : d["roles"].items())
                    g->my_roles.push_back(r.asId());
                m_structureVersion++;
            }
        }
    } else if (type == "TYPING_START") {
        Snowflake uid = d["user_id"].asId();
        if (uid == m_me.id)
            return;
        std::string name;
        const auto &member = d["member"];
        if (member.isObject()) {
            name = member["nick"].str();
            if (name.empty())
                name = User::fromJson(member["user"]).displayName();
        }
        if (name.empty()) {
            if (Channel *c = findChannel(d["channel_id"].asId()))
                for (const auto &r : c->recipients)
                    if (r.id == uid)
                        name = r.displayName();
        }
        if (name.empty())
            name = "Someone";
        auto &list = m_typing[d["channel_id"].asId()];
        list.erase(std::remove_if(list.begin(), list.end(), [uid](const auto &t) { return t.first == uid; }),
                   list.end());
        list.push_back({uid, {name, nowMs() + 10000}});
    } else if (type == "AUTH_TOKEN_REFRESH") {
        std::string token = d["token"].str();
        if (!token.empty()) {
            m_token = token;
            m_rest.setToken(token);
            m_gateway.setToken(token);
            if (onTokenRefresh)
                onTokenRefresh(token);
        }
    }
}

std::string Client::typingText(Snowflake channel_id)
{
    auto it = m_typing.find(channel_id);
    if (it == m_typing.end())
        return {};
    uint64_t now = nowMs();
    auto &list = it->second;
    list.erase(std::remove_if(list.begin(), list.end(), [now](const auto &t) { return t.second.until_ms < now; }),
               list.end());
    if (list.empty())
        return {};
    if (list.size() == 1)
        return list[0].second.name + " is typing...";
    if (list.size() == 2)
        return list[0].second.name + " and " + list[1].second.name + " are typing...";
    return "Several people are typing...";
}

void Client::loadMessages(Snowflake channel_id)
{
    ChannelMessages &cm = m_messages[channel_id];
    if (cm.loaded || cm.loading)
        return;
    cm.loading = true;
    cm.error.clear();
    std::weak_ptr<bool> alive = m_alive;
    m_rest.request("GET", "/channels/" + std::to_string(channel_id) + "/messages?limit=50", "",
                   [this, alive, channel_id](const HttpResponse &res) {
                       if (alive.expired())
                           return;
                       ChannelMessages &cm = m_messages[channel_id];
                       cm.loading = false;
                       json::Value v;
                       if (res.status != 200 || !json::parse(res.body, v) || !v.isArray()) {
                           cm.error = res.status == 403   ? "You can't view this channel."
                                      : res.status == 0 ? "Network error: " + res.error
                                                        : "Couldn't load messages (HTTP " +
                                                              std::to_string(res.status) + ")";
                           m_messagesVersion++;
                           return;
                       }
                       // Keep messages that arrived over the gateway meanwhile.
                       std::vector<Message> live = std::move(cm.messages);
                       cm.messages.clear();
                       for (size_t i = v.size(); i-- > 0;)
                           cm.messages.push_back(Message::fromJson(v[i]));
                       Snowflake newest = cm.messages.empty() ? 0 : cm.messages.back().id;
                       for (auto &m : live)
                           if (m.id > newest || m.pending)
                               cm.messages.push_back(std::move(m));
                       cm.reached_start = v.size() < 50;
                       cm.loaded = true;
                       m_messagesVersion++;
                   });
}

void Client::loadOlder(Snowflake channel_id)
{
    ChannelMessages &cm = m_messages[channel_id];
    if (!cm.loaded || cm.loading || cm.reached_start || cm.messages.empty() ||
        nowMs() < cm.older_retry_at)
        return;
    Snowflake before = 0;
    for (const auto &m : cm.messages) {
        if (!m.pending) {
            before = m.id;
            break;
        }
    }
    if (!before)
        return;
    cm.loading = true;
    std::weak_ptr<bool> alive = m_alive;
    m_rest.request("GET",
                   "/channels/" + std::to_string(channel_id) + "/messages?limit=50&before=" +
                       std::to_string(before),
                   "", [this, alive, channel_id](const HttpResponse &res) {
                       if (alive.expired())
                           return;
                       ChannelMessages &cm = m_messages[channel_id];
                       cm.loading = false;
                       json::Value v;
                       if (res.status != 200 || !json::parse(res.body, v) || !v.isArray()) {
                           // Don't retry every frame: the UI asks whenever
                           // the view is near the top.
                           cm.older_retry_at = nowMs() + (res.status == 403 ? 60000 : 5000);
                           if (res.status == 403)
                               cm.reached_start = true;
                           return;
                       }
                       std::vector<Message> older;
                       for (size_t i = v.size(); i-- > 0;)
                           older.push_back(Message::fromJson(v[i]));
                       cm.messages.insert(cm.messages.begin(), older.begin(), older.end());
                       cm.reached_start = v.size() < 50;
                       m_messagesVersion++;
                   });
}

void Client::sendMessage(Snowflake channel_id, const std::string &content, const Upload *upload)
{
    if (content.empty() && !upload)
        return;
    if (Channel *c = findChannel(channel_id)) {
        if (sendCooldownMs(*c) > 0)
            return;
    }
    m_lastSent[channel_id] = nowMs();
    // Discord nonces are snowflake-shaped: the current time in the high bits.
    u64 unix_now = 0;
    timeGetCurrentTime(TimeType_Default, &unix_now);
    uint64_t nonce_value = ((unix_now * 1000ULL - 1420070400000ULL) << 22);
    uint32_t low;
    randomBytes(&low, sizeof(low));
    nonce_value |= low & 0x3FFFFF;
    std::string nonce = std::to_string(nonce_value);

    ChannelMessages &cm = m_messages[channel_id];
    Message pending;
    pending.channel_id = channel_id;
    pending.author = m_me;
    pending.content = content;
    pending.nonce = nonce;
    pending.pending = true;
    if (upload)
        pending.uploading = upload->filename;
    cm.messages.push_back(pending);
    m_messagesVersion++;

    json::Value body = json::Value::object();
    body.set("content", json::Value::string(content));
    body.set("nonce", json::Value::string(nonce));
    body.set("tts", json::Value::boolean(false));
    body.set("flags", json::Value::number((int64_t)0));
    body.set("mobile_network_type", json::Value::string("unknown"));
    std::string request_body;
    HttpHeaders extra = {{"X-Context-Properties", base64Encode(std::string("{\"location\":\"chat_input\"}"))}};
    if (upload) {
        json::Value att = json::Value::object();
        att.set("id", json::Value::string("0"));
        att.set("filename", json::Value::string(safeFilename(upload->filename)));
        json::Value list = json::Value::array();
        list.push(std::move(att));
        body.set("attachments", std::move(list));
        unsigned char rnd[16];
        randomBytes(rnd, sizeof(rnd));
        std::string boundary = multipartBoundary(rnd);
        request_body = multipartBody(boundary, body.dump(), *upload);
        extra.emplace_back("Content-Type", "multipart/form-data; boundary=" + boundary);
    } else {
        request_body = body.dump();
    }

    std::weak_ptr<bool> alive = m_alive;
    m_rest.request(
        "POST", "/channels/" + std::to_string(channel_id) + "/messages", std::move(request_body),
        [this, alive, channel_id, nonce](const HttpResponse &res) {
            if (alive.expired())
                return;
            auto &list = m_messages[channel_id].messages;
            for (auto &m : list) {
                if (!m.pending || m.nonce != nonce)
                    continue;
                json::Value v;
                if (res.status == 200 && json::parse(res.body, v)) {
                    m = Message::fromJson(v);
                } else {
                    m.pending = false;
                    m.failed = true;
                    json::Value err;
                    json::parse(res.body, err);
                    if (res.status == 429) {
                        double after = err["retry_after"].asDouble(0);
                        m.error = "Slow down: try again in " + std::to_string((int)after + 1) + "s.";
                        // Respect what the server said for our own cooldown.
                        m_lastSent[channel_id] = nowMs() + (uint64_t)(after * 1000);
                    } else if (res.status == 0) {
                        m.error = "Network error.";
                    } else if (!err["message"].str().empty()) {
                        m.error = err["message"].str();
                    } else {
                        m.error = "HTTP " + std::to_string(res.status);
                    }
                }
                m_messagesVersion++;
                break;
            }
        },
        std::move(extra));
}

void Client::focusChannel(Snowflake channel_id)
{
    Channel *c = findChannel(channel_id);
    if (!c || c->guild_id == 0)
        return;
    // Ask for this guild's live events (op 37, guild subscriptions bulk), as
    // the official client does when a guild is opened. Large accounts get no
    // MESSAGE_CREATE for guilds they have not subscribed to.
    Snowflake gid = c->guild_id;
    // Subscribe once per channel per session; repeats would only spend the
    // gateway's send budget (120 per minute).
    std::set<Snowflake> &subs_for_guild = m_subscribed[gid];
    if (subs_for_guild.count(channel_id))
        return;
    subs_for_guild.insert(channel_id);
    using json::Value;
    Value range = Value::array();
    range.push(Value::number((int64_t)0));
    range.push(Value::number((int64_t)99));
    Value ranges = Value::array();
    ranges.push(std::move(range));
    Value channels = Value::object();
    channels.set(std::to_string(channel_id), std::move(ranges));
    Value sub = Value::object();
    sub.set("typing", Value::boolean(true));
    sub.set("threads", Value::boolean(true));
    sub.set("activities", Value::boolean(true));
    sub.set("channels", std::move(channels));
    Value subs = Value::object();
    subs.set(std::to_string(gid), std::move(sub));
    Value d = Value::object();
    d.set("subscriptions", std::move(subs));
    Value payload = Value::object();
    payload.set("op", Value::number((int64_t)37));
    payload.set("d", std::move(d));
    m_gateway.send(std::move(payload));
}

void Client::reloadMessages(Snowflake channel_id)
{
    ChannelMessages &cm = m_messages[channel_id];
    uint64_t now = nowMs();
    if (cm.loading || now < cm.reload_at)
        return;
    cm.reload_at = now + 3000;
    cm.loaded = false;
    loadMessages(channel_id);
}

uint64_t Client::sendCooldownMs(const Channel &c)
{
    uint64_t until = 0;
    auto it = m_lastSent.find(c.id);
    if (it == m_lastSent.end())
        return 0;
    // A server-imposed wait (stored in the future) always applies.
    if (it->second > nowMs())
        until = it->second;
    if (c.rate_limit_per_user > 0 && c.guild_id) {
        Guild *g = findGuild(c.guild_id);
        uint64_t p = g ? g->permissionsIn(c, m_me.id) : 0;
        if (!(p & (perm::ManageMessages | perm::ManageChannels)))
            until = std::max(until, it->second + (uint64_t)c.rate_limit_per_user * 1000);
    }
    uint64_t now = nowMs();
    return until > now ? until - now : 0;
}

}  // namespace discord
