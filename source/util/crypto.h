// SPDX-License-Identifier: MIT
//
// The bits of cryptography QR-code login needs: SHA-256, base64 decoding,
// and RSA-2048 with OAEP (SHA-256) decryption. Portable C++ with no
// platform dependencies, so it can be checked against OpenSSL on a desktop.
//
// Only what Discord's remote-auth handshake requires is here. The private key
// lives in memory for one login attempt and is never written anywhere.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace crypto {

using Sha256Digest = std::array<uint8_t, 32>;
Sha256Digest sha256(const void *data, size_t size);
inline Sha256Digest sha256(std::string_view s) { return sha256(s.data(), s.size()); }

// Standard or URL-safe alphabet, padding optional. False on bad input.
bool base64Decode(std::string_view in, std::string &out);
// URL-safe alphabet, no padding (RFC 4648 section 5).
std::string base64UrlEncode(const void *data, size_t size);

// Fills a buffer with cryptographically secure random bytes.
using RandomFn = std::function<void(void *, size_t)>;

// Arbitrary-precision unsigned integer, little-endian 32-bit limbs.
class BigInt {
public:
    BigInt() = default;
    explicit BigInt(uint32_t v);
    static BigInt fromBytes(const uint8_t *be, size_t n);  // big-endian
    // Big-endian, left-padded with zeros to len bytes (0 = minimal length).
    std::vector<uint8_t> toBytes(size_t len = 0) const;

    bool isZero() const { return d.empty(); }
    bool isOdd() const { return !d.empty() && (d[0] & 1); }
    size_t bits() const;
    bool bit(size_t i) const;

    static int cmp(const BigInt &a, const BigInt &b);
    static BigInt add(const BigInt &a, const BigInt &b);
    static BigInt sub(const BigInt &a, const BigInt &b);  // requires a >= b
    static BigInt mul(const BigInt &a, const BigInt &b);
    // q = a / b, r = a % b. b must be non-zero.
    static void divmod(const BigInt &a, const BigInt &b, BigInt *q, BigInt *r);
    static BigInt mod(const BigInt &a, const BigInt &m);
    uint32_t modSmall(uint32_t m) const;
    // base^exp mod m, for odd m (Montgomery multiplication).
    static BigInt powmod(const BigInt &base, const BigInt &exp, const BigInt &m);
    // a^-1 mod m; false when gcd(a, m) != 1.
    static bool invmod(const BigInt &a, const BigInt &m, BigInt &out);

    std::vector<uint32_t> d;  // no leading zero limbs

private:
    void trim();
};

struct RsaKey {
    BigInt n, e;
    BigInt p, q, dp, dq, qinv;  // CRT private key

    // Generates a key with a modulus of the given size and e = 65537.
    static RsaKey generate(int bits, const RandomFn &random);
    // The public key as DER SubjectPublicKeyInfo, what Discord calls
    // "encoded_public_key" once base64-encoded.
    std::vector<uint8_t> publicKeySpki() const;
    // RSAES-OAEP decryption with SHA-256, MGF1-SHA-256 and an empty label.
    bool decryptOaepSha256(std::string_view ciphertext, std::string &plain) const;
    // Raw RSA private operation (c^d mod n), via CRT.
    BigInt privateOp(const BigInt &c) const;
    // Wipes the private parts.
    void clear();
};

// Probabilistic primality test (trial division, then Miller-Rabin).
bool isProbablePrime(const BigInt &n, int rounds, const RandomFn &random);

}  // namespace crypto
