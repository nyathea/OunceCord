// SPDX-License-Identifier: MIT
#include "discord/remoteauth.h"

#include <algorithm>
#include <cstdlib>

#include "discord/rest.h"
#include "discord/session.h"
#include "net/http.h"
#include "net/websocket.h"
#include "util/crypto.h"
#include "util/json.h"
#include "util/util.h"

namespace discord {

static constexpr const char *kRemoteAuthHost = "remote-auth-gateway.discord.gg";
static constexpr const char *kQrPrefix = "https://discord.com/ra/";

RemoteAuth::~RemoteAuth()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

void RemoteAuth::start()
{
    if (m_running)
        return;
    if (m_thread.joinable())
        m_thread.join();  // a previous worker that was told to stop
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_token.clear();
        m_snap.phase = Phase::Preparing;
        m_snap.message.clear();
    }
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void RemoteAuth::stop()
{
    m_running = false;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_snap.phase = Phase::Idle;
}

void RemoteAuth::refresh()
{
    bool expired;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        expired = m_snap.phase == Phase::Expired || m_snap.phase == Phase::Failed;
    }
    if (expired) {
        m_running = false;
        start();
    }
}

RemoteAuth::Snapshot RemoteAuth::snapshot()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snap;
}

bool RemoteAuth::takeToken(std::string &out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_token.empty())
        return false;
    out = std::move(m_token);
    m_token.clear();
    return true;
}

void RemoteAuth::setPhase(Phase p, std::string message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_snap.phase = p;
    m_snap.message = std::move(message);
}

void RemoteAuth::sleepFor(uint64_t ms)
{
    uint64_t until = nowMs() + ms;
    while (m_running && nowMs() < until)
        svcSleepThread(100'000'000LL);
}

void RemoteAuth::run()
{
    int idle_codes = 0, failures = 0;
    uint64_t last_connect = 0;
    while (m_running) {
        // Never reconnect more than once every few seconds.
        uint64_t since = nowMs() - last_connect;
        if (last_connect && since < 3000)
            sleepFor(3000 - since);
        if (!m_running)
            break;
        last_connect = nowMs();

        std::string error;
        End end = session(error);
        switch (end) {
        case End::Stopped:
            break;
        case End::LoggedIn:
            m_running = false;
            break;
        case End::Cancelled:
            // Cancelled on the phone: offer a fresh code straight away.
            idle_codes = 0;
            failures = 0;
            break;
        case End::Expired:
            failures = 0;
            if (++idle_codes >= kMaxIdleCodes) {
                logf("remoteauth: %d codes expired unscanned, pausing", idle_codes);
                setPhase(Phase::Expired);
                m_running = false;
            }
            break;
        case End::Failed: {
            failures++;
            uint64_t wait = std::min<uint64_t>(60000, 2000ull << std::min(failures, 5));
            wait += (uint64_t)(rand() % 1000);
            logf("remoteauth: %s; retrying in %llu ms", error.c_str(), (unsigned long long)wait);
            setPhase(Phase::Failed, error);
            if (failures >= 8) {
                m_running = false;  // give up until refresh()
                break;
            }
            sleepFor(wait);
            break;
        }
        }
    }
}

// Decrypts a base64 RSA-OAEP field.
static bool decryptField(const crypto::RsaKey &key, const json::Value &v, std::string &out)
{
    std::string raw;
    return v.isString() && crypto::base64Decode(v.asString(), raw) && key.decryptOaepSha256(raw, out);
}

RemoteAuth::End RemoteAuth::session(std::string &error)
{
    setPhase(Phase::Preparing);
    uint64_t t0 = nowMs();
    crypto::RsaKey key = crypto::RsaKey::generate(2048, [](void *buf, size_t n) { randomBytes(buf, n); });
    logf("remoteauth: generated a key in %llu ms", (unsigned long long)(nowMs() - t0));
    if (!m_running)
        return End::Stopped;

    struct Wipe {
        crypto::RsaKey &k;
        ~Wipe() { k.clear(); }
    } wipe{key};

    // Look like the same browser session the exchange will come from: get
    // the fingerprint and Discord's cookies before showing a code. The
    // official client has both long before anyone scans anything.
    HttpClient http(kApiHost);
    if (!ClientSession::get().ensureFingerprint(http, error))
        return End::Failed;

    const Identity &id = Identity::get();
    HttpHeaders headers = {
        {"User-Agent", id.userAgent()},
        {"Origin", "https://discord.com"},
        {"Cache-Control", "no-cache"},
        {"Pragma", "no-cache"},
        {"Accept-Language", id.locale},
    };
    WebSocket ws;
    if (!ws.connect(kRemoteAuthHost, "/?v=2", headers, &error)) {
        if (error.empty())
            error = "Could not reach Discord";
        return End::Failed;
    }

    bool got_code = false, ack_pending = false;
    uint64_t heartbeat_interval = 0, next_heartbeat = UINT64_MAX;
    auto send = [&ws](json::Value v) { return ws.sendText(v.dump()); };

    while (m_running) {
        uint64_t now = nowMs();
        if (now >= next_heartbeat) {
            if (ack_pending) {
                logf("remoteauth: heartbeat not acknowledged");
                ws.close(4000);
                error = "Lost the connection to Discord";
                return End::Failed;
            }
            json::Value hb = json::Value::object();
            hb.set("op", json::Value::string("heartbeat"));
            send(std::move(hb));
            ack_pending = true;
            next_heartbeat = now + heartbeat_interval;
        }

        WebSocket::Message msg;
        WebSocket::Result r = ws.receive(msg, 100);
        if (r == WebSocket::Result::Timeout)
            continue;
        if (r != WebSocket::Result::Message) {
            uint16_t code = ws.closeCode();
            logf("remoteauth: closed (code %u)", code);
            if (got_code)
                return End::Expired;  // codes time out after a couple of minutes
            error = "Discord closed the QR login (" + std::to_string(code) + ")";
            return End::Failed;
        }

        json::Value v;
        if (!json::parse(msg.data, v) || !v.isObject())
            continue;
        const std::string &op = v["op"].asString();

        if (op == "hello") {
            heartbeat_interval = (uint64_t)std::max<int64_t>(5000, v["heartbeat_interval"].asInt(41250));
            next_heartbeat = nowMs() + heartbeat_interval;
            std::vector<uint8_t> spki = key.publicKeySpki();
            json::Value init = json::Value::object();
            init.set("op", json::Value::string("init"));
            init.set("encoded_public_key", json::Value::string(base64Encode(spki.data(), spki.size())));
            send(std::move(init));
        } else if (op == "heartbeat_ack") {
            ack_pending = false;
        } else if (op == "heartbeat") {
            json::Value hb = json::Value::object();
            hb.set("op", json::Value::string("heartbeat"));
            send(std::move(hb));
        } else if (op == "nonce_proof") {
            std::string nonce;
            if (!decryptField(key, v["encrypted_nonce"], nonce)) {
                ws.close();
                error = "Could not decrypt Discord's challenge";
                return End::Failed;
            }
            json::Value proof = json::Value::object();
            proof.set("op", json::Value::string("nonce_proof"));
            proof.set("nonce", json::Value::string(crypto::base64UrlEncode(nonce.data(), nonce.size())));
            send(std::move(proof));
        } else if (op == "pending_remote_init") {
            std::string fp = v["fingerprint"].asString();
            if (fp.empty())
                continue;
            got_code = true;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snap.phase = Phase::WaitingScan;
            m_snap.url = kQrPrefix + fp;
            m_snap.url_version++;
            m_snap.user = User();
            m_snap.message.clear();
        } else if (op == "pending_ticket") {
            std::string payload;
            User u;
            if (decryptField(key, v["encrypted_user_payload"], payload)) {
                // id:discriminator:avatar:username (the username may contain ':')
                size_t a = payload.find(':'), b = payload.find(':', a + 1), c = payload.find(':', b + 1);
                if (a != std::string::npos && b != std::string::npos && c != std::string::npos) {
                    u.id = strtoull(payload.substr(0, a).c_str(), nullptr, 10);
                    u.discriminator = payload.substr(a + 1, b - a - 1);
                    u.avatar = payload.substr(b + 1, c - b - 1);
                    u.username = payload.substr(c + 1);
                }
            }
            logf("remoteauth: code scanned");
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snap.phase = Phase::Scanned;
            m_snap.user = u;
        } else if (op == "pending_login") {
            std::string ticket = v["ticket"].asString();
            ws.close();
            if (ticket.empty()) {
                error = "Discord sent no login ticket";
                return End::Failed;
            }
            setPhase(Phase::LoggingIn);
            json::Value body = json::Value::object();
            body.set("ticket", json::Value::string(ticket));
            HttpHeaders h = id.restHeaders({}, "https://discord.com/login");
            h.emplace_back("Content-Type", "application/json");
            ClientSession::get().apply(h, true);
            HttpResponse res;
            for (int attempt = 0; attempt < 2; attempt++) {
                res = http.request("POST", std::string(kApiBase) + "/users/@me/remote-auth/login", h, body.dump());
                ClientSession::get().absorb(res);
                if (res.status != 429)
                    break;
                json::Value rv;
                double after = json::parse(res.body, rv) ? rv["retry_after"].asDouble(5) : 5;
                if (after > 10)
                    break;
                sleepFor((uint64_t)(after * 1000) + 250);
            }
            http.close();
            logf("remoteauth: ticket exchange -> %d", res.status);
            json::Value rv;
            std::string token;
            if (res.status == 200 && json::parse(res.body, rv) && decryptField(key, rv["encrypted_token"], token) &&
                !token.empty()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_token = token;
                return End::LoggedIn;
            }
            if (json::parse(res.body, rv) && rv["captcha_key"].isArray())
                error = "Discord asked for a captcha. Try again later.";
            else
                error = "Discord didn't accept the QR login (" + std::to_string(res.status) + ")";
            return End::Failed;
        } else if (op == "cancel") {
            logf("remoteauth: cancelled on the phone");
            ws.close();
            return End::Cancelled;
        }
    }
    ws.close();
    return End::Stopped;
}

}  // namespace discord
