// SPDX-License-Identifier: MIT
#include "discord/gateway.h"

#include <algorithm>
#include <cstring>

#include <switch.h>

#include "util/util.h"

namespace discord {

// Capabilities that Paicord sends: lazy user notes, non-channel read states,
// auth token refresh, user settings proto and debounced message reactions.
static constexpr int64_t kCapabilities = (1 << 0) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 13);

enum Op {
    Op_Dispatch = 0,
    Op_Heartbeat = 1,
    Op_Identify = 2,
    Op_Resume = 6,
    Op_Reconnect = 7,
    Op_InvalidSession = 9,
    Op_Hello = 10,
    Op_HeartbeatAck = 11,
};

void Gateway::start(std::string token)
{
    stop();
    m_token = std::move(token);
    m_seq = -1;
    m_sessionId.clear();
    m_resumeHost.clear();
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void Gateway::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void Gateway::setToken(std::string token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_token = std::move(token);
}

void Gateway::send(json::Value payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outbox.push_back(payload.dump());
}

void Gateway::status(GatewayStatus s, std::string detail)
{
    GatewayListener *l = m_listener;
    m_post([l, s, detail] { l->onStatus(s, detail); });
}

void Gateway::resetZlib()
{
    if (m_zsReady)
        inflateEnd(&m_zs);
    memset(&m_zs, 0, sizeof(m_zs));
    m_zsReady = inflateInit(&m_zs) == Z_OK;
    m_zbuf.clear();
}

void Gateway::sleepFor(uint64_t ms)
{
    uint64_t until = nowMs() + ms;
    while (m_running && nowMs() < until)
        svcSleepThread(50000000LL);
}

bool Gateway::sendPayload(const std::string &text, bool priority)
{
    // Discord closes connections that send more than 120 payloads per 60
    // seconds. Ordinary payloads stop at 110 so heartbeats, identify and
    // resume always have room.
    uint64_t now = nowMs();
    while (!m_sendTimes.empty() && now - m_sendTimes.front() >= kSendWindowMs)
        m_sendTimes.pop_front();
    size_t cap = priority ? kSendLimit : kSendLimit - kSendReserve;
    if (m_sendTimes.size() >= cap)
        return false;
    m_sendTimes.push_back(now);
    m_ws.sendText(text);
    return true;
}

uint64_t Gateway::identifyWaitMs()
{
    uint64_t now = nowMs();
    while (!m_identifyTimes.empty() && now - m_identifyTimes.front() >= kIdentifyWindowMs)
        m_identifyTimes.pop_front();
    uint64_t wait = 0;
    // At most one identify per 5 seconds.
    if (!m_identifyTimes.empty() && now - m_identifyTimes.back() < kIdentifySpacingMs)
        wait = kIdentifySpacingMs - (now - m_identifyTimes.back());
    // A burst of fresh sessions looks like abuse to Discord: cap them per hour.
    if (m_identifyTimes.size() >= kMaxIdentifiesPerWindow)
        wait = std::max(wait, m_identifyTimes.front() + kIdentifyWindowMs - now);
    return wait;
}

bool Gateway::inflateMessage(const std::string &in, std::string &out)
{
    // zlib-stream: one inflate context for the connection; each message ends
    // with a sync flush marker (00 00 FF FF).
    m_zbuf += in;
    if (m_zbuf.size() < 4 || memcmp(m_zbuf.data() + m_zbuf.size() - 4, "\x00\x00\xff\xff", 4) != 0)
        return false;
    m_zs.next_in = (Bytef *)m_zbuf.data();
    m_zs.avail_in = m_zbuf.size();
    static thread_local char chunk[65536];
    do {
        m_zs.next_out = (Bytef *)chunk;
        m_zs.avail_out = sizeof(chunk);
        int r = inflate(&m_zs, Z_SYNC_FLUSH);
        if (r != Z_OK && r != Z_BUF_ERROR && r != Z_STREAM_END) {
            logf("gateway: inflate error %d", r);
            m_zbuf.clear();
            return false;
        }
        out.append(chunk, sizeof(chunk) - m_zs.avail_out);
        if (r == Z_BUF_ERROR)
            break;
    } while (m_zs.avail_in > 0 || m_zs.avail_out == 0);
    m_zbuf.clear();
    return true;
}

void Gateway::sendIdentify()
{
    using json::Value;
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_token;
    }
    Value presence = Value::object();
    presence.set("status", Value::string("unknown"));
    presence.set("since", Value::number((int64_t)0));
    presence.set("activities", Value::array());
    presence.set("afk", Value::boolean(false));

    Value client_state = Value::object();
    client_state.set("guild_versions", Value::object());

    Value d = Value::object();
    d.set("token", Value::string(token));
    d.set("capabilities", Value::number(kCapabilities));
    d.set("properties", Identity::get().properties(true));
    d.set("presence", std::move(presence));
    d.set("compress", Value::boolean(false));
    d.set("client_state", std::move(client_state));

    Value payload = Value::object();
    payload.set("op", Value::number((int64_t)Op_Identify));
    payload.set("d", std::move(d));
    uint64_t wait = identifyWaitMs();
    if (wait) {
        logf("gateway: waiting %llu ms before identifying", (unsigned long long)wait);
        sleepFor(wait);
    }
    m_identifyTimes.push_back(nowMs());
    sendPayload(payload.dump(), true);
    logf("gateway: sent identify");
}

void Gateway::sendResume()
{
    using json::Value;
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_token;
    }
    Value d = Value::object();
    d.set("token", Value::string(token));
    d.set("session_id", Value::string(m_sessionId));
    d.set("seq", Value::number(m_seq));
    Value payload = Value::object();
    payload.set("op", Value::number((int64_t)Op_Resume));
    payload.set("d", std::move(d));
    sendPayload(payload.dump(), true);
    logf("gateway: sent resume (seq %lld)", (long long)m_seq);
}

void Gateway::sendHeartbeat()
{
    std::string hb = "{\"op\":1,\"d\":" + (m_seq >= 0 ? std::to_string(m_seq) : "null") + "}";
    sendPayload(hb, true);
    m_ackPending = true;
}

bool Gateway::handlePayload(const std::string &text, bool &resumable, bool &reconnect_now)
{
    auto doc = std::make_shared<json::Value>();
    std::string err;
    if (!json::parse(text, *doc, &err)) {
        logf("gateway: bad JSON (%s), %zu bytes", err.c_str(), text.size());
        return true;
    }
    int op = (int)(*doc)["op"].asInt(-1);
    switch (op) {
    case Op_Hello: {
        m_heartbeatInterval = (uint64_t)(*doc)["d"]["heartbeat_interval"].asInt(41250);
        uint32_t jitter;
        randomBytes(&jitter, sizeof(jitter));
        m_nextHeartbeat = nowMs() + (uint64_t)(m_heartbeatInterval * ((jitter % 1000) / 1000.0));
        m_ackPending = false;
        if (resumable && !m_sessionId.empty() && m_seq >= 0)
            sendResume();
        else
            sendIdentify();
        break;
    }
    case Op_HeartbeatAck:
        m_ackPending = false;
        break;
    case Op_Heartbeat:
        sendHeartbeat();
        break;
    case Op_Reconnect:
        logf("gateway: server asked to reconnect");
        resumable = true;
        reconnect_now = true;
        break;
    case Op_InvalidSession: {
        bool can_resume = (*doc)["d"].asBool();
        logf("gateway: invalid session (resumable=%d)", can_resume);
        resumable = can_resume;
        if (!can_resume) {
            m_sessionId.clear();
            m_seq = -1;
        }
        // Discord asks for a random 1-5 second wait before identifying again.
        uint32_t r;
        randomBytes(&r, sizeof(r));
        sleepFor(1000 + r % 4000);
        reconnect_now = true;
        break;
    }
    case Op_Dispatch: {
        if ((*doc)["s"].isNumber())
            m_seq = (*doc)["s"].asInt();
        std::string type = (*doc)["t"].str();
        if (type == "READY") {
            const auto &d = (*doc)["d"];
            m_sessionId = d["session_id"].str();
            std::string url = d["resume_gateway_url"].str();
            // "wss://gateway-us-east1-b.discord.gg" -> host part
            size_t p = url.find("://");
            m_resumeHost = p == std::string::npos ? url : url.substr(p + 3);
            while (!m_resumeHost.empty() && m_resumeHost.back() == '/')
                m_resumeHost.pop_back();
            status(GatewayStatus::Connected);
            logf("gateway: READY (%zu bytes)", text.size());
        } else if (type == "RESUMED") {
            status(GatewayStatus::Connected);
            logf("gateway: resumed");
        }
        GatewayListener *l = m_listener;
        m_post([l, type, doc] { l->onDispatch(type, (*doc)["d"]); });
        break;
    }
    default:
        break;
    }
    return true;
}

bool Gateway::runConnection(bool &resumable)
{
    std::string host = (resumable && !m_resumeHost.empty()) ? m_resumeHost : kGatewayHost;
    const Identity &id = Identity::get();
    HttpHeaders headers = {
        {"User-Agent", id.userAgent()},
        {"Origin", "https://discord.com"},
        {"Cache-Control", "no-cache"},
        {"Pragma", "no-cache"},
        {"Accept-Language", id.locale},
    };
    std::string error;
    status(resumable ? GatewayStatus::Reconnecting : GatewayStatus::Connecting);
    if (!m_ws.connect(host, "/?encoding=json&v=9&compress=zlib-stream", headers, &error)) {
        status(GatewayStatus::Reconnecting, error);
        return true;
    }
    resetZlib();
    m_sendTimes.clear();  // the send limit is per connection
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_outbox.clear();
    }

    bool reconnect_now = false;
    m_nextHeartbeat = UINT64_MAX;
    while (m_running && !reconnect_now) {
        uint64_t now = nowMs();
        if (now >= m_nextHeartbeat) {
            if (m_ackPending) {
                // No ACK for the last heartbeat: the connection is a zombie.
                logf("gateway: heartbeat not acknowledged, reconnecting");
                m_ws.close(4000);
                resumable = true;
                return true;
            }
            sendHeartbeat();
            m_nextHeartbeat = now + m_heartbeatInterval;
        }

        // Send queued payloads while the send budget allows; the rest wait.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (!m_outbox.empty() && sendPayload(m_outbox.front(), false))
                m_outbox.pop_front();
        }

        uint64_t wait = m_nextHeartbeat == UINT64_MAX ? 100 : (m_nextHeartbeat > now ? m_nextHeartbeat - now : 0);
        if (wait > 100)
            wait = 100;  // stay responsive to stop() and the outbox

        WebSocket::Message msg;
        WebSocket::Result r = m_ws.receive(msg, (uint32_t)wait);
        if (r == WebSocket::Result::Timeout)
            continue;
        if (r != WebSocket::Result::Message) {
            uint16_t code = m_ws.closeCode();
            logf("gateway: disconnected (code %u)", code);
            switch (code) {
            case 4004:
                status(GatewayStatus::AuthFailed, "Discord rejected the token (4004).");
                return false;
            case 4010: case 4011: case 4012: case 4013: case 4014:
                status(GatewayStatus::Fatal, "Gateway closed with code " + std::to_string(code));
                return false;
            case 4008:
                // We sent too fast. Resume after a longer pause.
                logf("gateway: rate limited by Discord (4008)");
                m_rateLimitedClose = true;
                resumable = true;
                break;
            case 4007: case 4009:
                resumable = false;
                m_sessionId.clear();
                m_seq = -1;
                break;
            default:
                resumable = true;
                break;
            }
            return true;
        }

        std::string text;
        if (msg.binary) {
            if (!inflateMessage(msg.data, text))
                continue;
        } else {
            text = std::move(msg.data);
        }
        handlePayload(text, resumable, reconnect_now);
    }
    m_ws.close(reconnect_now ? 4000 : 1000);
    return m_running;
}

void Gateway::run()
{
    bool resumable = false;
    int failures = 0;
    while (m_running) {
        uint64_t started = nowMs();
        if (!runConnection(resumable))
            break;
        if (!m_running)
            break;
        // Back off exponentially when connections keep failing quickly, with
        // jitter so many clients do not reconnect in lockstep.
        if (nowMs() - started < 10000)
            failures++;
        else
            failures = 0;
        uint64_t delay = failures == 0 ? 1000 : std::min<uint64_t>(1000ULL << std::min(failures, 6), 60000);
        if (m_rateLimitedClose)
            delay = std::max<uint64_t>(delay, 15000);
        m_rateLimitedClose = false;
        uint32_t r;
        randomBytes(&r, sizeof(r));
        delay += r % (delay / 2 + 1);
        // A fresh session must also respect the identify limits.
        if (!resumable || m_sessionId.empty()) {
            uint64_t id_wait = identifyWaitMs();
            if (id_wait > delay) {
                delay = id_wait;
                if (id_wait > 60000)
                    status(GatewayStatus::Reconnecting,
                           "Too many reconnects. Waiting " + std::to_string(id_wait / 60000 + 1) +
                               " min before signing in again.");
            }
        }
        logf("gateway: reconnecting in %llu ms", (unsigned long long)delay);
        sleepFor(delay);
    }
    m_ws.close();
    if (m_zsReady) {
        inflateEnd(&m_zs);
        m_zsReady = false;
    }
}

}  // namespace discord
