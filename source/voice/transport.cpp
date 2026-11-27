// SPDX-License-Identifier: MIT
#include "voice/transport.h"

#include <cstring>

#include <openssl/evp.h>

namespace voice {
namespace {

void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

uint32_t rotl(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// One AEAD operation with OpenSSL: AES-256-GCM, or ChaCha20-Poly1305 for the
// XChaCha mode (with the HChaCha20 subkey). nonce is the 24-byte nonce
// buffer: its first 4 bytes carry the counter, the rest are zero.
bool aead(bool encrypt, TransportMode mode, const uint8_t key[32], const uint8_t nonce[24], const uint8_t *ad,
          size_t ad_size, const uint8_t *in, size_t size, uint8_t *out, uint8_t tag[16])
{
    const EVP_CIPHER *cipher = nullptr;
    uint8_t subkey[32], iv[12] = {};
    const uint8_t *k = key;
    if (mode == TransportMode::AesGcm) {
        cipher = EVP_aes_256_gcm();
        memcpy(iv, nonce, 12);
    } else if (mode == TransportMode::XChaCha) {
        cipher = EVP_chacha20_poly1305();
        hchacha20(key, nonce, subkey);
        memcpy(iv + 4, nonce + 16, 8);
        k = subkey;
    } else {
        return false;
    }
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;
    int n = 0, total = 0;
    bool ok = EVP_CipherInit_ex(ctx, cipher, nullptr, nullptr, nullptr, encrypt) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, nullptr) == 1 &&
              EVP_CipherInit_ex(ctx, nullptr, nullptr, k, iv, encrypt) == 1 &&
              EVP_CipherUpdate(ctx, nullptr, &n, ad, (int)ad_size) == 1;
    if (ok && size) {
        ok = EVP_CipherUpdate(ctx, out, &n, in, (int)size) == 1;
        total = n;
    }
    if (ok && !encrypt)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, tag) == 1;
    if (ok)
        ok = EVP_CipherFinal_ex(ctx, out + total, &n) == 1;
    if (ok && encrypt)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    memset(subkey, 0, sizeof(subkey));
    return ok;
}

}  // namespace

TransportMode pickMode(const std::vector<std::string> &modes)
{
    TransportMode best = TransportMode::None;
    for (const auto &m : modes) {
        if (m == "aead_aes256_gcm_rtpsize")
            return TransportMode::AesGcm;
        if (m == "aead_xchacha20_poly1305_rtpsize")
            best = TransportMode::XChaCha;
    }
    return best;
}

const char *modeName(TransportMode m)
{
    switch (m) {
    case TransportMode::AesGcm: return "aead_aes256_gcm_rtpsize";
    case TransportMode::XChaCha: return "aead_xchacha20_poly1305_rtpsize";
    default: return "";
    }
}

bool parseRtp(const uint8_t *p, size_t n, RtpHeader &out)
{
    if (n < kRtpHeaderSize || (p[0] >> 6) != 2)
        return false;
    out = {};
    out.payload_type = p[1] & 0x7f;
    out.sequence = (uint16_t)(p[2] << 8 | p[3]);
    out.timestamp = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
    out.ssrc = (uint32_t)p[8] << 24 | (uint32_t)p[9] << 16 | (uint32_t)p[10] << 8 | p[11];
    size_t h = kRtpHeaderSize + (p[0] & 0x0f) * 4u;
    if (p[0] & 0x10) {
        h += 4;
        if (h > n)
            return false;
        out.extension = true;
        out.extension_words = (size_t)(p[h - 2] << 8 | p[h - 1]);
    }
    if (h > n)
        return false;
    out.header_size = h;
    return true;
}

void writeRtpHeader(uint8_t out[kRtpHeaderSize], uint16_t sequence, uint32_t timestamp, uint32_t ssrc)
{
    out[0] = 0x80;
    out[1] = kOpusPayloadType;
    out[2] = (uint8_t)(sequence >> 8);
    out[3] = (uint8_t)sequence;
    put32(out + 4, timestamp);
    put32(out + 8, ssrc);
}

Transport::~Transport() { memset(m_key, 0, sizeof(m_key)); }

void Transport::setKey(TransportMode mode, const uint8_t key[32])
{
    m_mode = mode;
    memcpy(m_key, key, 32);
    m_nonce = 0;
}

std::vector<uint8_t> Transport::seal(const uint8_t *packet, size_t header_size, size_t size)
{
    if (m_mode == TransportMode::None || header_size > size || m_nonce == UINT32_MAX)
        return {};
    uint8_t nonce[24] = {};
    put32(nonce, ++m_nonce);
    size_t body = size - header_size;
    std::vector<uint8_t> out(size + 16 + 4);
    memcpy(out.data(), packet, header_size);
    uint8_t tag[16];
    if (!aead(true, m_mode, m_key, nonce, packet, header_size, packet + header_size, body, out.data() + header_size,
              tag))
        return {};
    memcpy(out.data() + size, tag, 16);
    memcpy(out.data() + size + 16, nonce, 4);
    return out;
}

bool Transport::open(const uint8_t *packet, size_t size, const RtpHeader &h, std::vector<uint8_t> &out) const
{
    if (m_mode == TransportMode::None || size < h.header_size + 16 + 4)
        return false;
    uint8_t nonce[24] = {};
    memcpy(nonce, packet + size - 4, 4);
    uint8_t tag[16];
    memcpy(tag, packet + size - 20, 16);
    size_t body = size - h.header_size - 20;
    out.resize(body);
    if (!aead(false, m_mode, m_key, nonce, packet, h.header_size, packet + h.header_size, body, out.data(), tag))
        return false;
    // The extension's elements were encrypted with the payload.
    size_t skip = h.extension_words * 4;
    if (skip > out.size())
        return false;
    out.erase(out.begin(), out.begin() + skip);
    if (packet[0] & 0x20) {  // padding
        if (out.empty() || out.back() == 0 || out.back() > out.size())
            return false;
        out.resize(out.size() - out.back());
    }
    return true;
}

void hchacha20(const uint8_t key[32], const uint8_t in[16], uint8_t out[32])
{
    uint32_t x[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; i++)
        x[4 + i] = le32(key + i * 4);
    for (int i = 0; i < 4; i++)
        x[12 + i] = le32(in + i * 4);
    auto qr = [&](int a, int b, int c, int d) {
        x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 16);
        x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 12);
        x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 8);
        x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 7);
    };
    for (int i = 0; i < 10; i++) {
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15);
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14);
    }
    const int pick[8] = {0, 1, 2, 3, 12, 13, 14, 15};
    for (int i = 0; i < 8; i++) {
        uint32_t v = x[pick[i]];
        out[i * 4] = (uint8_t)v;
        out[i * 4 + 1] = (uint8_t)(v >> 8);
        out[i * 4 + 2] = (uint8_t)(v >> 16);
        out[i * 4 + 3] = (uint8_t)(v >> 24);
    }
}

}  // namespace voice
