// SPDX-License-Identifier: MIT
//
// A voice call: the voice gateway (its own WebSocket), the UDP media
// connection, DAVE end-to-end encryption, Opus encoding and decoding, the
// microphone and the speakers, all run on one thread of its own. The main
// gateway's VOICE_STATE_UPDATE / VOICE_SERVER_UPDATE supply what start()
// needs (see discord::Client::joinVoice).
//
// Audio goes out only while we're talking (a simple voice activity gate),
// and only once DAVE has a key: never unencrypted in a call that uses DAVE.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

namespace voice {

struct JoinInfo {
    uint64_t server_id = 0;   // the guild (or the DM channel)
    uint64_t channel_id = 0;
    uint64_t user_id = 0;
    std::string session_id;   // from our VOICE_STATE_UPDATE
    std::string token;        // from VOICE_SERVER_UPDATE
    std::string endpoint;     // host[:port], from VOICE_SERVER_UPDATE
};

enum class CallState { Connecting, Connected, Failed, Ended };

struct CallStatus {
    CallState state = CallState::Connecting;
    std::string detail;     // why it failed, for the UI
    bool encrypted = false; // DAVE end to end
    bool microphone = true; // false when no microphone could be opened
};

class Connection {
public:
    Connection() = default;
    ~Connection() { stop(); }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    void start(const JoinInfo &info, bool muted, bool deafened);
    void stop();
    void setMuted(bool muted) { m_muted = muted; }
    void setDeafened(bool deafened) { m_deafened = deafened; }

    // Thread-safe snapshots for the UI.
    CallStatus status();
    // Users heard in the last moment (by user id), and whether we are talking.
    std::set<uint64_t> speaking();
    bool selfSpeaking() const { return m_selfSpeaking; }
    // True once when the voice session expired or its server is down, and
    // Discord must be asked for a new one (leave and join again on the main
    // gateway).
    bool takeRejoin() { return m_wantsRejoin.exchange(false); }

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_muted{false}, m_deafened{false}, m_selfSpeaking{false}, m_wantsRejoin{false};
    std::mutex m_mutex;
    CallStatus m_status;
    std::map<uint64_t, uint64_t> m_heard;  // user -> time last heard (ms)
    JoinInfo m_info;

    void run();
    void setStatus(CallState s, const std::string &detail = {});
    void requestRejoin(const char *why);
    friend struct Session;
};

}  // namespace voice
