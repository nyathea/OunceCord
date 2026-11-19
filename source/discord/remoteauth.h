// SPDX-License-Identifier: MIT
//
// QR-code login through Discord's remote-auth gateway (v2), as the desktop
// client and Paicord do:
//
//   connect wss://remote-auth-gateway.discord.gg/?v=2
//   <- hello {heartbeat_interval, timeout_ms}
//   -> init {encoded_public_key: SPKI of a fresh RSA-2048 key}
//   <- nonce_proof {encrypted_nonce}       (RSA-OAEP-SHA256)
//   -> nonce_proof {nonce: base64url of the decrypted nonce}
//   <- pending_remote_init {fingerprint}   QR = https://discord.com/ra/<fp>
//   <- pending_ticket {encrypted_user_payload}  "id:discrim:avatar:username"
//   <- pending_login {ticket}
//   POST /users/@me/remote-auth/login {ticket} -> {encrypted_token}
//
// The phone app does the confirming, so no password or captcha is involved
// here. The RSA key exists only in memory, for one QR code.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "discord/types.h"

namespace discord {

class RemoteAuth {
public:
    enum class Phase {
        Idle,
        Preparing,    // generating a key / connecting
        WaitingScan,  // QR code ready
        Scanned,      // the phone scanned it; waiting for the user to confirm
        LoggingIn,    // exchanging the ticket for a token
        Expired,      // stopped refreshing; refresh() to get a new code
        Failed,       // message says why; retrying after a delay
    };

    struct Snapshot {
        Phase phase = Phase::Idle;
        std::string url;  // what the QR code encodes
        uint32_t url_version = 0;
        User user;  // who scanned it (Scanned / LoggingIn)
        std::string message;
    };

    RemoteAuth() = default;
    ~RemoteAuth();

    void start();    // no-op while running
    void stop();     // asks the worker to finish; does not wait
    void refresh();  // after Expired: start over
    Snapshot snapshot();
    // The token, once a login went through (taken once).
    bool takeToken(std::string &out);

    // How many codes in a row may expire unscanned before we stop
    // refreshing (each lasts about two minutes).
    static constexpr int kMaxIdleCodes = 5;

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::mutex m_mutex;
    Snapshot m_snap;
    std::string m_token;

    void run();
    void setPhase(Phase p, std::string message = {});
    void sleepFor(uint64_t ms);
    // One gateway session. Returns how it ended.
    enum class End { Stopped, LoggedIn, Expired, Cancelled, Failed };
    End session(std::string &error);
};

}  // namespace discord
