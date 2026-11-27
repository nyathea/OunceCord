// SPDX-License-Identifier: MIT
//
// Voice media transport: RTP packets and their encryption between us and
// Discord's voice server (the "rtpsize" AEAD modes). This sits under DAVE,
// which encrypts the audio itself end to end. Platform-free (OpenSSL).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace voice {

enum class TransportMode { None, AesGcm, XChaCha };

// "aead_aes256_gcm_rtpsize" (preferred) or "aead_xchacha20_poly1305_rtpsize"
// (always offered): the best of the modes the server lists, or None.
TransportMode pickMode(const std::vector<std::string> &modes);
const char *modeName(TransportMode m);

constexpr uint8_t kOpusPayloadType = 120;
constexpr size_t kRtpHeaderSize = 12;

struct RtpHeader {
    uint8_t payload_type = 0;
    uint16_t sequence = 0;
    uint32_t timestamp = 0;
    uint32_t ssrc = 0;
    size_t header_size = 0;  // clear part: fixed header, CSRCs, extension preamble
    bool extension = false;
    size_t extension_words = 0;  // 32-bit words of extension data after the preamble
};

// Reads the clear part of an RTP packet; false if it isn't one.
bool parseRtp(const uint8_t *p, size_t n, RtpHeader &out);
void writeRtpHeader(uint8_t out[kRtpHeaderSize], uint16_t sequence, uint32_t timestamp, uint32_t ssrc);

class Transport {
public:
    ~Transport();
    void setKey(TransportMode mode, const uint8_t key[32]);
    TransportMode mode() const { return m_mode; }

    // Encrypts an RTP packet (header_size clear bytes, then the payload) for
    // sending: header, ciphertext, tag, then the 4-byte nonce. Empty on error.
    std::vector<uint8_t> seal(const uint8_t *packet, size_t header_size, size_t size);
    // Decrypts a received packet in place of out: the RTP payload, with the
    // header extension's elements (encrypted with it) and padding removed.
    bool open(const uint8_t *packet, size_t size, const RtpHeader &h, std::vector<uint8_t> &out) const;

private:
    TransportMode m_mode = TransportMode::None;
    uint8_t m_key[32] = {};
    uint32_t m_nonce = 0;
};

// HChaCha20 (the XChaCha20 subkey derivation): 32-byte key, 16-byte input.
void hchacha20(const uint8_t key[32], const uint8_t in[16], uint8_t out[32]);

}  // namespace voice
