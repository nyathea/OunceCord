// SPDX-License-Identifier: MIT
#include "voice/voice.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opus/opus.h>

#include "discord/rest.h"
#include "net/websocket.h"
#include "util/json.h"
#include "util/util.h"
#include "voice/audio.h"
#include "voice/dave.h"
#include "voice/transport.h"

namespace voice {

bool initRandom();  // platform.cpp

namespace {

constexpr uint8_t kSilence[3] = {0xF8, 0xFF, 0xFE};  // Opus silence frame
constexpr int kSilenceFramesAfterTalk = 5;
constexpr uint64_t kTalkHangoverMs = 400;
// Voice activity: a frame counts as talking when its level is well above
// the background noise the microphone hears, and above a small minimum.
constexpr double kTalkMinRms = 90;         // RMS of a 16-bit frame
constexpr double kTalkOverNoise = 2.5;
constexpr double kMaxGain = 6;             // quiet microphones are boosted up to this
constexpr double kTargetPeak = 14000;
constexpr uint64_t kStatsMs = 5000;        // how often the call's counters are logged
constexpr size_t kPeerPrimeFrames = 3;     // buffered before a voice starts playing
constexpr size_t kPeerMaxFrames = 12;      // at most 240 ms behind
constexpr uint64_t kHeardMs = 300;         // speaking indicator hold
constexpr uint64_t kUdpPingMs = 5000;

using json::Value;

uint64_t idOf(const Value &v) { return v.asId(); }

struct Peer {
    uint64_t user = 0;
    OpusDecoder *decoder = nullptr;
    std::deque<int16_t> pcm;  // interleaved stereo
    bool playing = false;
    bool haveSeq = false;
    uint16_t seq = 0;
    ~Peer()
    {
        if (decoder)
            opus_decoder_destroy(decoder);
    }
};

}  // namespace

// One connection attempt's state; lives on the voice thread.
struct Session {
    Connection &c;
    JoinInfo info;
    WebSocket ws;
    int udp = -1;
    sockaddr_in server{};
    uint32_t ssrc = 0;
    Transport transport;
    std::unique_ptr<Dave> dave;
    OpusEncoder *encoder = nullptr;
    std::map<uint32_t, std::unique_ptr<Peer>> peers;  // by SSRC
    std::map<uint64_t, uint32_t> userSsrc;
    AudioIO audio;
    bool audioOpen = false;
    bool sessionReady = false;
    bool resumable = false;
    int64_t seqAck = -1;
    uint64_t heartbeatInterval = 13750, nextHeartbeat = 0, nextPing = 0;
    uint32_t pingSeq = 0;
    uint16_t rtpSeq = 0;
    uint32_t rtpTs = 0;
    bool talking = false;
    int silenceLeft = 0;
    uint64_t lastVoice = 0;
    double noiseFloor = 200, speechPeak = 4000, gain = 1;
    // Counters for the log, reset every kStatsMs.
    struct {
        unsigned frames = 0, voiced = 0, encoded = 0, sent = 0, blocked = 0;
        unsigned received = 0, badTransport = 0, badDave = 0, unknownSsrc = 0, played = 0;
        int peak = 0;
        double rmsSum = 0;
    } stats;
    uint64_t nextStats = 0;

    void logStats()
    {
        auto &s = stats;
        logf("voice: mic %s, %u frames (%llu samples total), peak %d, avg rms %.0f, noise %.0f, gain %.1f, "
             "voiced %u; sent %u/%u (blocked %u); dave v%u %s; heard %u (bad transport %u, bad e2ee %u, "
             "unknown ssrc %u), played %u, peers %zu",
             audio.hasMicrophone() ? (audio.capturing() ? "on" : "off") : "missing", s.frames,
             (unsigned long long)audio.capturedFrames(), s.peak, s.frames ? s.rmsSum / s.frames : 0.0, noiseFloor,
             gain, s.voiced, s.sent, s.encoded, s.blocked, dave ? (unsigned)dave->version() : 0u,
             dave && dave->canSend() ? "can send" : "no send key", s.received, s.badTransport, s.badDave,
             s.unknownSsrc, s.played, peers.size());
        stats = {};
    }

    explicit Session(Connection &conn, const JoinInfo &i) : c(conn), info(i)
    {
        randomBytes(&rtpSeq, sizeof(rtpSeq));
        randomBytes(&rtpTs, sizeof(rtpTs));
    }
    ~Session()
    {
        audio.close();
        if (udp >= 0)
            ::close(udp);
        if (encoder)
            opus_encoder_destroy(encoder);
        ws.close();
    }

    void sendJson(int op, Value d)
    {
        Value v = Value::object();
        v.set("op", Value::number((int64_t)op));
        v.set("d", std::move(d));
        ws.sendText(v.dump());
    }

    void identify()
    {
        Value d = Value::object();
        d.set("server_id", Value::string(std::to_string(info.server_id)));
        d.set("user_id", Value::string(std::to_string(info.user_id)));
        d.set("session_id", Value::string(info.session_id));
        d.set("token", Value::string(info.token));
        d.set("video", Value::boolean(false));
        d.set("max_dave_protocol_version", Value::number((int64_t)Dave::maxVersion()));
        sendJson(0, std::move(d));
    }

    void resume()
    {
        Value d = Value::object();
        d.set("server_id", Value::string(std::to_string(info.server_id)));
        d.set("session_id", Value::string(info.session_id));
        d.set("token", Value::string(info.token));
        d.set("seq_ack", Value::number(seqAck));
        sendJson(7, std::move(d));
    }

    void heartbeat()
    {
        Value d = Value::object();
        d.set("t", Value::number((int64_t)nowMs()));
        d.set("seq_ack", Value::number(seqAck));
        sendJson(3, std::move(d));
    }

    void setSpeaking(bool on)
    {
        Value d = Value::object();
        d.set("speaking", Value::number((int64_t)(on ? 1 : 0)));
        d.set("delay", Value::number((int64_t)0));
        d.set("ssrc", Value::number((int64_t)ssrc));
        sendJson(5, std::move(d));
    }

    // Opens the UDP socket and learns our public address (IP discovery).
    bool openUdp(const std::string &ip, int port, std::string &my_ip, int &my_port)
    {
        udp = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp < 0)
            return false;
        server.sin_family = AF_INET;
        server.sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, ip.c_str(), &server.sin_addr) != 1)
            return false;
        if (connect(udp, (sockaddr *)&server, sizeof(server)) != 0)
            return false;
        fcntl(udp, F_SETFL, fcntl(udp, F_GETFL, 0) | O_NONBLOCK);
        uint8_t req[74] = {};
        req[1] = 1;   // request
        req[3] = 70;  // length
        req[4] = (uint8_t)(ssrc >> 24);
        req[5] = (uint8_t)(ssrc >> 16);
        req[6] = (uint8_t)(ssrc >> 8);
        req[7] = (uint8_t)ssrc;
        for (int attempt = 0; attempt < 5 && c.m_running; attempt++) {
            send(udp, req, sizeof(req), 0);
            uint64_t until = nowMs() + 1000;
            while (nowMs() < until && c.m_running) {
                pollfd pfd{udp, POLLIN, 0};
                if (poll(&pfd, 1, 100) <= 0)
                    continue;
                uint8_t res[128];
                ssize_t n = recv(udp, res, sizeof(res), 0);
                if (n >= 74 && res[1] == 2) {
                    res[8 + 63] = 0;
                    my_ip = (const char *)res + 8;
                    my_port = res[72] << 8 | res[73];
                    return true;
                }
            }
        }
        return false;
    }

    bool handleText(const std::string &text, bool &fatal)
    {
        Value v;
        if (!json::parse(text, v))
            return true;
        if (v.has("seq"))
            seqAck = v["seq"].asInt();
        int op = (int)v["op"].asInt(-1);
        const Value &d = v["d"];
        switch (op) {
        case 8:  // hello
            heartbeatInterval = (uint64_t)d["heartbeat_interval"].asInt(13750);
            nextHeartbeat = nowMs() + heartbeatInterval;
            break;
        case 2: {  // ready
            ssrc = (uint32_t)d["ssrc"].asInt();
            std::vector<std::string> modes;
            for (const auto &m : d["modes"].items())
                modes.push_back(m.str());
            TransportMode mode = pickMode(modes);
            if (mode == TransportMode::None) {
                c.setStatus(CallState::Failed, "No supported voice encryption");
                fatal = true;
                return false;
            }
            std::string my_ip;
            int my_port = 0;
            if (!openUdp(d["ip"].str(), (int)d["port"].asInt(), my_ip, my_port)) {
                c.setStatus(CallState::Failed, "Couldn't reach the voice server");
                fatal = true;
                return false;
            }
            logf("voice: ready, ssrc %u, udp %s:%d (we are %s:%d), %s", (unsigned)ssrc, d["ip"].str().c_str(),
                 (int)d["port"].asInt(), my_ip.c_str(), my_port, modeName(mode));
            dave = std::make_unique<Dave>(info.user_id, info.channel_id, [this](bool binary, const std::string &s) {
                if (binary)
                    ws.sendBinary(s);
                else
                    ws.sendText(s);
            });
            dave->setSsrc(ssrc);
            Value data = Value::object();
            data.set("address", Value::string(my_ip));
            data.set("port", Value::number((int64_t)my_port));
            data.set("mode", Value::string(modeName(mode)));
            Value codec = Value::object();
            codec.set("name", Value::string("opus"));
            codec.set("type", Value::string("audio"));
            codec.set("priority", Value::number((int64_t)1000));
            codec.set("payload_type", Value::number((int64_t)kOpusPayloadType));
            Value codecs = Value::array();
            codecs.push(std::move(codec));
            Value sel = Value::object();
            sel.set("protocol", Value::string("udp"));
            sel.set("data", std::move(data));
            sel.set("codecs", std::move(codecs));
            sendJson(1, std::move(sel));
            break;
        }
        case 4: {  // session description
            std::vector<std::string> one{d["mode"].str()};
            TransportMode mode = pickMode(one);
            const auto &key = d["secret_key"].items();
            if (mode == TransportMode::None || key.size() != 32 || !dave) {
                c.setStatus(CallState::Failed, "Bad session description");
                fatal = true;
                return false;
            }
            uint8_t k[32];
            for (int i = 0; i < 32; i++)
                k[i] = (uint8_t)key[i].asInt();
            transport.setKey(mode, k);
            memset(k, 0, sizeof(k));
            uint16_t version = (uint16_t)d["dave_protocol_version"].asInt(0);
            dave->start(version);
            sessionReady = true;
            resumable = true;
            int err = 0;
            encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_VOIP, &err);
            if (encoder) {
                opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));
                opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
                opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
            }
            audioOpen = audio.open();
            {
                std::lock_guard<std::mutex> lock(c.m_mutex);
                c.m_status.microphone = audioOpen;
            }
            if (!audioOpen)
                logf("voice: audio devices unavailable");
            logf("voice: session ready, DAVE version %u", (unsigned)version);
            c.setStatus(CallState::Connected);
            break;
        }
        case 5: {  // speaking: who sends with which SSRC
            uint64_t user = idOf(d["user_id"]);
            uint32_t s = (uint32_t)d["ssrc"].asInt();
            if (user && s) {
                auto &p = peers[s];
                if (!p)
                    p = std::make_unique<Peer>();
                p->user = user;
                userSsrc[user] = s;
                if (dave)
                    dave->clientsConnect({user});
            }
            break;
        }
        case 9:  // resumed
            logf("voice: resumed");
            break;
        case 11: {  // clients connect
            std::vector<uint64_t> users;
            for (const auto &u : d["user_ids"].items())
                users.push_back(idOf(u));
            if (dave)
                dave->clientsConnect(users);
            break;
        }
        case 13: {  // client disconnect
            uint64_t user = idOf(d["user_id"]);
            if (dave)
                dave->clientDisconnect(user);
            auto it = userSsrc.find(user);
            if (it != userSsrc.end()) {
                peers.erase(it->second);
                userSsrc.erase(it);
            }
            break;
        }
        case 21:
            if (dave)
                dave->prepareTransition((uint16_t)d["transition_id"].asInt(), (uint16_t)d["protocol_version"].asInt());
            break;
        case 22:
            if (dave)
                dave->executeTransition((uint16_t)d["transition_id"].asInt());
            break;
        case 24:
            if (dave)
                dave->prepareEpoch((uint16_t)d["transition_id"].asInt(), (uint16_t)d["protocol_version"].asInt(),
                                   (uint64_t)d["epoch"].asInt());
            break;
        default:
            break;
        }
        return true;
    }

    void handleBinary(const std::string &data)
    {
        if (data.size() < 3)
            return;
        const uint8_t *p = (const uint8_t *)data.data();
        seqAck = p[0] << 8 | p[1];
        if (dave)
            dave->binary(p[2], p + 3, data.size() - 3);
    }

    void receiveUdp()
    {
        uint8_t buf[2048];
        for (int k = 0; k < 64; k++) {
            ssize_t n = recv(udp, buf, sizeof(buf), 0);
            if (n <= 0)
                return;
            if ((size_t)n < kRtpHeaderSize || (buf[1] & 0x7f) != kOpusPayloadType)
                continue;  // RTCP, pings, discovery
            RtpHeader h;
            if (!parseRtp(buf, (size_t)n, h))
                continue;
            stats.received++;
            auto it = peers.find(h.ssrc);
            if (it == peers.end() || !it->second->user) {
                stats.unknownSsrc++;
                continue;
            }
            Peer &peer = *it->second;
            std::vector<uint8_t> payload, opus;
            if (!transport.open(buf, (size_t)n, h, payload)) {
                stats.badTransport++;
                continue;
            }
            if (!dave || !dave->decrypt(peer.user, payload.data(), payload.size(), opus)) {
                stats.badDave++;
                continue;
            }
            if (peer.haveSeq && (int16_t)(h.sequence - peer.seq) <= 0)
                continue;  // late or repeated
            int lost = peer.haveSeq ? (int16_t)(h.sequence - peer.seq) - 1 : 0;
            peer.haveSeq = true;
            peer.seq = h.sequence;
            if (opus.size() == 3 && !memcmp(opus.data(), kSilence, 3))
                continue;
            if (!peer.decoder) {
                int err = 0;
                peer.decoder = opus_decoder_create(48000, 2, &err);
                if (!peer.decoder)
                    continue;
            }
            int16_t pcm[5760 * 2];
            // Conceal up to two lost frames (Opus packet loss concealment).
            for (int i = 0; i < std::min(lost, 2); i++) {
                int f = opus_decode(peer.decoder, nullptr, 0, pcm, kFrameSamples, 0);
                if (f > 0)
                    peer.pcm.insert(peer.pcm.end(), pcm, pcm + f * 2);
            }
            int frames = opus_decode(peer.decoder, opus.data(), (opus_int32)opus.size(), pcm, 5760, 0);
            if (frames <= 0)
                continue;
            peer.pcm.insert(peer.pcm.end(), pcm, pcm + frames * 2);
            size_t cap = kPeerMaxFrames * kFrameSamples * 2;
            if (peer.pcm.size() > cap)
                peer.pcm.erase(peer.pcm.begin(), peer.pcm.begin() + (peer.pcm.size() - cap));
            std::lock_guard<std::mutex> lock(c.m_mutex);
            c.m_heard[peer.user] = nowMs();
        }
    }

    void sendOpus(const uint8_t *opus, size_t n)
    {
        std::vector<uint8_t> e2ee;
        if (!dave || !dave->encrypt(opus, n, e2ee))
            return;
        std::vector<uint8_t> packet(kRtpHeaderSize + e2ee.size());
        writeRtpHeader(packet.data(), rtpSeq, rtpTs, ssrc);
        memcpy(packet.data() + kRtpHeaderSize, e2ee.data(), e2ee.size());
        std::vector<uint8_t> sealed = transport.seal(packet.data(), kRtpHeaderSize, packet.size());
        if (sealed.empty())
            return;
        if (send(udp, sealed.data(), sealed.size(), 0) == (ssize_t)sealed.size()) {
            rtpSeq++;
            stats.sent++;
        } else {
            stats.blocked++;
        }
    }

    void captureTick()
    {
        bool muted = c.m_muted || c.m_deafened;
        audio.setCapture(!muted);
        int16_t mono[kFrameSamples];
        while (audio.readFrame(mono)) {
            double sum = 0;
            int peak = 0;
            for (int i = 0; i < kFrameSamples; i++) {
                sum += (double)mono[i] * mono[i];
                peak = std::max(peak, std::abs((int)mono[i]));
            }
            double rms = std::sqrt(sum / kFrameSamples);
            stats.frames++;
            stats.rmsSum += rms;
            stats.peak = std::max(stats.peak, peak);
            // Track the background noise: follow it down fast, up slowly.
            noiseFloor = rms < noiseFloor ? noiseFloor * 0.9 + rms * 0.1 : noiseFloor * 0.998 + rms * 0.002;
            noiseFloor = std::max(noiseFloor, 5.0);
            bool voiced = !muted && rms >= std::max(kTalkMinRms, noiseFloor * kTalkOverNoise);
            uint64_t now = nowMs();
            if (voiced) {
                lastVoice = now;
                stats.voiced++;
                // Boost quiet microphones so speech peaks land near kTargetPeak.
                speechPeak = speechPeak * 0.95 + peak * 0.05;
                gain = std::clamp(kTargetPeak / std::max(speechPeak, 1.0), 1.0, kMaxGain);
            }
            bool active = !muted && lastVoice && now - lastVoice < kTalkHangoverMs;
            // Audio goes out only with a key (or in a call without DAVE).
            bool want = active && dave && dave->canSend();
            if (want && !talking) {
                talking = true;
                setSpeaking(true);
            }
            if (want && encoder) {
                int16_t stereo[kFrameSamples * 2];
                for (int i = 0; i < kFrameSamples; i++) {
                    int v = std::clamp((int)std::lround(mono[i] * gain), -32768, 32767);
                    stereo[i * 2] = stereo[i * 2 + 1] = (int16_t)v;
                }
                uint8_t opus[1275];
                int n = opus_encode(encoder, stereo, kFrameSamples, opus, sizeof(opus));
                if (n > 0) {
                    stats.encoded++;
                    sendOpus(opus, (size_t)n);
                }
                silenceLeft = kSilenceFramesAfterTalk;
            } else if (talking) {
                // Five silence frames, so receivers stop interpolating.
                if (silenceLeft > 0) {
                    sendOpus(kSilence, sizeof(kSilence));
                    silenceLeft--;
                }
                if (silenceLeft == 0) {
                    talking = false;
                    setSpeaking(false);
                }
            }
            rtpTs += kFrameSamples;
            // Our own speaking ring shows that the microphone hears us,
            // even before there is anyone to send to.
            c.m_selfSpeaking = active;
        }
        if (muted)
            c.m_selfSpeaking = false;
    }

    void playbackTick()
    {
        while (audio.wantsFrame()) {
            int32_t mix[kFrameSamples * 2] = {};
            for (auto &[s, p] : peers) {
                if (!p->playing && p->pcm.size() >= kPeerPrimeFrames * kFrameSamples * 2)
                    p->playing = true;
                if (!p->playing)
                    continue;
                if (p->pcm.size() < (size_t)kFrameSamples * 2) {
                    p->playing = false;  // ran dry: buffer up again
                    continue;
                }
                for (int i = 0; i < kFrameSamples * 2; i++)
                    mix[i] += p->pcm[i];
                stats.played++;
                p->pcm.erase(p->pcm.begin(), p->pcm.begin() + kFrameSamples * 2);
            }
            int16_t out[kFrameSamples * 2];
            bool deaf = c.m_deafened;
            for (int i = 0; i < kFrameSamples * 2; i++)
                out[i] = deaf ? 0 : (int16_t)std::clamp(mix[i], -32768, 32767);
            audio.playFrame(out);
        }
    }

    // Runs one WebSocket connection. Returns true to try resuming.
    bool runOnce(bool resuming)
    {
        // The endpoint is host:port, and the port matters: Discord's voice
        // servers also listen on 2053, 2083, 2087, 2096... and the server on
        // 443 of that host doesn't know a session made for another port
        // (it closes with 4006 at once).
        std::string host = info.endpoint;
        int port = 443;
        if (size_t colon = host.rfind(':'); colon != std::string::npos) {
            int p = atoi(host.c_str() + colon + 1);
            if (p > 0 && p < 65536)
                port = p;
            host.resize(colon);
        }
        const discord::Identity &id = discord::Identity::get();
        HttpHeaders headers = {{"User-Agent", id.userAgent()}};
        std::string error;
        if (!ws.connect(host, "/?v=8", headers, &error, port)) {
            logf("voice: connecting to %s:%d failed: %s", host.c_str(), port, error.c_str());
            if (resuming)
                return true;
            // The server Discord picked is down (Cloudflare answers 522 when
            // it can't reach it): ask for another one.
            c.requestRejoin("the voice server didn't answer");
            return false;
        }
        if (resuming)
            resume();
        else
            identify();
        while (c.m_running) {
            uint64_t now = nowMs();
            if (nextHeartbeat && now >= nextHeartbeat) {
                heartbeat();
                nextHeartbeat = now + heartbeatInterval;
            }
            if (udp >= 0 && sessionReady && now >= nextPing) {
                uint8_t ping[8] = {0x13, 0x37, 0xCA, 0xFE};
                pingSeq++;
                memcpy(ping + 4, &pingSeq, 4);
                send(udp, ping, sizeof(ping), 0);
                nextPing = now + kUdpPingMs;
            }
            WebSocket::Message msg;
            WebSocket::Result r = ws.receive(msg, sessionReady ? 2 : 50);
            if (r == WebSocket::Result::Message) {
                bool fatal = false;
                if (msg.binary)
                    handleBinary(msg.data);
                else if (!handleText(msg.data, fatal) && fatal)
                    return false;
                if (dave && dave->failed()) {
                    c.setStatus(CallState::Failed, "Encryption setup failed");
                    return false;
                }
                if (dave) {
                    std::lock_guard<std::mutex> lock(c.m_mutex);
                    c.m_status.encrypted = dave->version() > 0 && dave->canSend();
                }
            } else if (r == WebSocket::Result::Closed || r == WebSocket::Result::Error) {
                uint16_t code = ws.closeCode();
                logf("voice: connection closed (%u)", (unsigned)code);
                switch (code) {
                case 4014:
                case 4022: c.setStatus(CallState::Ended, "Disconnected from the call"); return false;
                case 4017: c.setStatus(CallState::Failed, "This call needs end-to-end encryption"); return false;
                case 4004:
                case 4006:
                case 4009:
                    // The voice session is gone (the server moved, or a
                    // resume came too late): Discord needs to hand out a
                    // new one, through the main gateway.
                    c.requestRejoin("the voice session expired");
                    return false;
                case 4021: c.setStatus(CallState::Failed, "Rate limited"); return false;
                default: return resumable;
                }
            }
            if (sessionReady && now >= nextStats) {
                if (nextStats)
                    logStats();
                nextStats = now + kStatsMs;
            }
            if (sessionReady && udp >= 0) {
                receiveUdp();
                if (audioOpen) {
                    captureTick();
                    playbackTick();
                }
            }
        }
        return false;
    }
};

void Connection::start(const JoinInfo &info, bool muted, bool deafened)
{
    stop();
    static bool random_ready = initRandom();
    if (!random_ready) {
        setStatus(CallState::Failed, "No secure random numbers");
        return;
    }
    m_info = info;
    m_muted = muted;
    m_deafened = deafened;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = CallStatus{};
        m_heard.clear();
    }
    m_wantsRejoin = false;
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void Connection::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_selfSpeaking = false;
}

void Connection::requestRejoin(const char *why)
{
    logf("voice: %s, asking Discord for a new voice server", why);
    setStatus(CallState::Connecting, "Reconnecting");
    m_wantsRejoin = true;
}

void Connection::setStatus(CallState s, const std::string &detail)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = s;
    m_status.detail = detail;
}

CallStatus Connection::status()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::set<uint64_t> Connection::speaking()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::set<uint64_t> out;
    uint64_t now = nowMs();
    for (const auto &[u, t] : m_heard)
        if (now - t < kHeardMs)
            out.insert(u);
    return out;
}

void Connection::run()
{
    logf("voice: joining channel %llu on %s", (unsigned long long)m_info.channel_id, m_info.endpoint.c_str());
    auto session = std::make_unique<Session>(*this, m_info);
    bool resuming = false;
    for (int attempt = 0; m_running; attempt++) {
        bool again = session->runOnce(resuming);
        if (!again || !m_running || attempt >= 3)
            break;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status.encrypted = false;
        }
        logf("voice: reconnecting");
        setStatus(CallState::Connecting);
        session->ws.close();
        resuming = true;
        for (int i = 0; i < 20 && m_running; i++)
            usleep(50000);
    }
    CallState state = status().state;
    if (m_running && !m_wantsRejoin && state != CallState::Failed && state != CallState::Ended) {
        if (resuming)
            requestRejoin("resuming didn't work");  // start over
        else
            setStatus(CallState::Failed, "Lost the voice connection");
    }
    session.reset();
    m_selfSpeaking = false;
}

}  // namespace voice
