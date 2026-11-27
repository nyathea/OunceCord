// SPDX-License-Identifier: MIT
//
// DAVE, Discord's end-to-end encryption for calls, through libdave's C API:
// the MLS group negotiation over the voice gateway (opcodes 21-31) and the
// per-frame encryption of our Opus frames and decryption of everyone else's.
// Discord disconnects clients without it (close code 4017), so audio is only
// sent once a key exists; it never falls back to unencrypted media in a call
// that uses DAVE. Platform-free. Not thread-safe: use from the voice thread.
//
// The opcode handling follows DiscordNX2's voice engine (MIT) and the DAVE
// protocol whitepaper (daveprotocol.com).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace voice {

class Dave {
public:
    // Sends a voice gateway message: JSON text, or binary (opcode byte, then
    // the payload).
    using Sender = std::function<void(bool binary, const std::string &data)>;

    Dave(uint64_t self, uint64_t channel, Sender send);
    ~Dave();
    Dave(const Dave &) = delete;
    Dave &operator=(const Dave &) = delete;

    static uint16_t maxVersion();

    // Opcode 4 (session description): the version the call starts with,
    // 0 for none (stage channels).
    void start(uint16_t version);
    void clientsConnect(const std::vector<uint64_t> &users);  // opcode 11
    void clientDisconnect(uint64_t user);                      // opcode 13
    void prepareTransition(uint16_t transition, uint16_t version);              // 21
    void executeTransition(uint16_t transition);                                 // 22
    void prepareEpoch(uint16_t transition, uint16_t version, uint64_t epoch);   // 24
    // Binary opcodes 25, 27, 29 and 30; p is the payload after the opcode.
    void binary(uint8_t op, const uint8_t *p, size_t n);
    void setSsrc(uint32_t ssrc);

    // Our Opus frame, encrypted for the call (as is when the call has no
    // DAVE). False when it can't be sent yet (no key).
    bool encrypt(const uint8_t *frame, size_t n, std::vector<uint8_t> &out);
    // Someone's frame back to Opus. False when it can't be decrypted.
    bool decrypt(uint64_t user, const uint8_t *frame, size_t n, std::vector<uint8_t> &out);

    uint16_t version() const { return m_version; }
    // Whether our audio is being end-to-end encrypted (or the call has no DAVE).
    bool canSend() const { return m_version == 0 ? m_started : m_sendKey; }
    bool failed() const { return m_failed; }

private:
    struct Impl;
    Impl *m;
    uint64_t m_self, m_channel;
    Sender m_send;
    uint16_t m_version = 0;
    uint32_t m_ssrc = 0;
    bool m_started = false;
    bool m_sendKey = false;
    bool m_failed = false;
    bool m_pending = false;
    uint16_t m_pendingTransition = 0;
    uint16_t m_pendingVersion = 0;

    void init(uint16_t version);
    void sendKeyPackage();
    void sendTransition(int op, uint16_t transition);
    void sendBinary(uint8_t op, const uint8_t *p, size_t n);
    void updateReceivers();
    void ready(uint16_t transition, uint16_t version);
    void apply(uint16_t version);
};

}  // namespace voice
