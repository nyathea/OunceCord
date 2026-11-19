// SPDX-License-Identifier: MIT
#include "util/crypto.h"

#include <algorithm>
#include <cstring>

namespace crypto {

// --- SHA-256 (FIPS 180-4) ----------------------------------------------------

static const uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256Block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

Sha256Digest sha256(const void *data, size_t size)
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *p = (const uint8_t *)data;
    size_t left = size;
    while (left >= 64) {
        sha256Block(h, p);
        p += 64;
        left -= 64;
    }
    uint8_t tail[128] = {};
    memcpy(tail, p, left);
    tail[left] = 0x80;
    size_t tail_len = left < 56 ? 64 : 128;
    uint64_t bit_len = (uint64_t)size * 8;
    for (int i = 0; i < 8; i++)
        tail[tail_len - 1 - i] = (uint8_t)(bit_len >> (8 * i));
    sha256Block(h, tail);
    if (tail_len == 128)
        sha256Block(h, tail + 64);
    Sha256Digest out;
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)h[i];
    }
    return out;
}

// --- Base64 ------------------------------------------------------------------

bool base64Decode(std::string_view in, std::string &out)
{
    out.clear();
    out.reserve(in.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    size_t end = in.size();
    while (end > 0 && in[end - 1] == '=')
        end--;
    for (size_t i = 0; i < end; i++) {
        char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+' || c == '-')
            v = 62;
        else if (c == '/' || c == '_')
            v = 63;
        else if (c == '\n' || c == '\r')
            continue;
        else
            return false;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((acc >> bits) & 0xFF));
        }
    }
    return bits < 6;  // a lone trailing character is malformed
}

std::string base64UrlEncode(const void *data, size_t size)
{
    static const char *kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const uint8_t *p = (const uint8_t *)data;
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < size; i++) {
        acc = (acc << 8) | p[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(kAlphabet[(acc >> bits) & 63]);
        }
    }
    if (bits > 0)
        out.push_back(kAlphabet[(acc << (6 - bits)) & 63]);
    return out;
}

// --- BigInt ------------------------------------------------------------------

BigInt::BigInt(uint32_t v)
{
    if (v)
        d.push_back(v);
}

void BigInt::trim()
{
    while (!d.empty() && d.back() == 0)
        d.pop_back();
}

BigInt BigInt::fromBytes(const uint8_t *be, size_t n)
{
    BigInt r;
    r.d.assign((n + 3) / 4, 0);
    for (size_t i = 0; i < n; i++) {
        size_t pos = n - 1 - i;  // byte significance
        r.d[pos / 4] |= (uint32_t)be[i] << (8 * (pos % 4));
    }
    r.trim();
    return r;
}

std::vector<uint8_t> BigInt::toBytes(size_t len) const
{
    size_t need = (bits() + 7) / 8;
    if (len == 0)
        len = need ? need : 1;
    std::vector<uint8_t> out(len, 0);
    for (size_t pos = 0; pos < len && pos < d.size() * 4; pos++)
        out[len - 1 - pos] = (uint8_t)(d[pos / 4] >> (8 * (pos % 4)));
    return out;
}

size_t BigInt::bits() const
{
    if (d.empty())
        return 0;
    return 32 * (d.size() - 1) + (32 - __builtin_clz(d.back()));
}

bool BigInt::bit(size_t i) const
{
    return i / 32 < d.size() && ((d[i / 32] >> (i % 32)) & 1);
}

int BigInt::cmp(const BigInt &a, const BigInt &b)
{
    if (a.d.size() != b.d.size())
        return a.d.size() < b.d.size() ? -1 : 1;
    for (size_t i = a.d.size(); i-- > 0;)
        if (a.d[i] != b.d[i])
            return a.d[i] < b.d[i] ? -1 : 1;
    return 0;
}

BigInt BigInt::add(const BigInt &a, const BigInt &b)
{
    const BigInt &big = a.d.size() >= b.d.size() ? a : b;
    const BigInt &small = a.d.size() >= b.d.size() ? b : a;
    BigInt r;
    r.d.resize(big.d.size() + 1);
    uint64_t carry = 0;
    for (size_t i = 0; i < big.d.size(); i++) {
        uint64_t s = (uint64_t)big.d[i] + (i < small.d.size() ? small.d[i] : 0) + carry;
        r.d[i] = (uint32_t)s;
        carry = s >> 32;
    }
    r.d[big.d.size()] = (uint32_t)carry;
    r.trim();
    return r;
}

BigInt BigInt::sub(const BigInt &a, const BigInt &b)
{
    BigInt r;
    r.d.resize(a.d.size());
    int64_t borrow = 0;
    for (size_t i = 0; i < a.d.size(); i++) {
        int64_t s = (int64_t)a.d[i] - (i < b.d.size() ? b.d[i] : 0) - borrow;
        borrow = s < 0;
        r.d[i] = (uint32_t)(s + (borrow << 32));
    }
    r.trim();
    return r;
}

BigInt BigInt::mul(const BigInt &a, const BigInt &b)
{
    if (a.isZero() || b.isZero())
        return {};
    BigInt r;
    r.d.assign(a.d.size() + b.d.size(), 0);
    for (size_t i = 0; i < a.d.size(); i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < b.d.size(); j++) {
            uint64_t t = (uint64_t)a.d[i] * b.d[j] + r.d[i + j] + carry;
            r.d[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        r.d[i + b.d.size()] = (uint32_t)carry;
    }
    r.trim();
    return r;
}

void BigInt::divmod(const BigInt &a, const BigInt &b, BigInt *q, BigInt *r)
{
    // Binary long division: simple, and fast enough for the few divisions
    // key generation needs (exponentiation uses Montgomery instead).
    BigInt quot, rem;
    if (cmp(a, b) < 0) {
        if (q)
            *q = {};
        if (r)
            *r = a;
        return;
    }
    size_t n = a.bits();
    quot.d.assign(a.d.size(), 0);
    rem.d.reserve(b.d.size() + 1);
    for (size_t i = n; i-- > 0;) {
        // rem = rem * 2 + bit
        uint32_t carry = a.bit(i);
        for (auto &limb : rem.d) {
            uint32_t next = limb >> 31;
            limb = (limb << 1) | carry;
            carry = next;
        }
        if (carry)
            rem.d.push_back(carry);
        if (cmp(rem, b) >= 0) {
            rem = sub(rem, b);
            quot.d[i / 32] |= 1u << (i % 32);
        }
    }
    quot.trim();
    if (q)
        *q = std::move(quot);
    if (r)
        *r = std::move(rem);
}

BigInt BigInt::mod(const BigInt &a, const BigInt &m)
{
    BigInt r;
    divmod(a, m, nullptr, &r);
    return r;
}

uint32_t BigInt::modSmall(uint32_t m) const
{
    uint64_t r = 0;
    for (size_t i = d.size(); i-- > 0;)
        r = ((r << 32) | d[i]) % m;
    return (uint32_t)r;
}

namespace {

// Montgomery arithmetic modulo an odd m with k limbs.
struct Mont {
    std::vector<uint32_t> m;
    size_t k;
    uint32_t minv;  // -m^-1 mod 2^32

    explicit Mont(const BigInt &mod) : m(mod.d), k(mod.d.size())
    {
        uint32_t inv = 1;
        for (int i = 0; i < 5; i++)
            inv *= 2 - m[0] * inv;
        minv = (uint32_t)(0u - inv);
    }

    // out = a * b * R^-1 mod m. a, b, out have k limbs (out may alias).
    void mul(const uint32_t *a, const uint32_t *b, uint32_t *out) const
    {
        std::vector<uint32_t> t(k + 2, 0);
        for (size_t i = 0; i < k; i++) {
            uint64_t c = 0;
            for (size_t j = 0; j < k; j++) {
                uint64_t s = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + c;
                t[j] = (uint32_t)s;
                c = s >> 32;
            }
            uint64_t s = (uint64_t)t[k] + c;
            t[k] = (uint32_t)s;
            t[k + 1] = (uint32_t)(s >> 32);

            uint32_t mm = t[0] * minv;
            s = (uint64_t)t[0] + (uint64_t)mm * m[0];
            c = s >> 32;
            for (size_t j = 1; j < k; j++) {
                s = (uint64_t)t[j] + (uint64_t)mm * m[j] + c;
                t[j - 1] = (uint32_t)s;
                c = s >> 32;
            }
            s = (uint64_t)t[k] + c;
            t[k - 1] = (uint32_t)s;
            t[k] = t[k + 1] + (uint32_t)(s >> 32);
        }
        // Final subtraction if t >= m.
        bool ge = t[k] != 0;
        if (!ge) {
            ge = true;
            for (size_t i = k; i-- > 0;) {
                if (t[i] != m[i]) {
                    ge = t[i] > m[i];
                    break;
                }
            }
        }
        if (ge) {
            int64_t borrow = 0;
            for (size_t i = 0; i < k; i++) {
                int64_t s = (int64_t)t[i] - m[i] - borrow;
                borrow = s < 0;
                t[i] = (uint32_t)(s + (borrow << 32));
            }
        }
        memcpy(out, t.data(), k * 4);
    }
};

std::vector<uint32_t> padded(const BigInt &x, size_t k)
{
    std::vector<uint32_t> v(k, 0);
    std::copy(x.d.begin(), x.d.begin() + std::min(k, x.d.size()), v.begin());
    return v;
}

}  // namespace

BigInt BigInt::powmod(const BigInt &base, const BigInt &exp, const BigInt &m)
{
    Mont mont(m);
    size_t k = mont.k;
    // R mod m and R^2 mod m, with R = 2^(32k).
    BigInt R;
    R.d.assign(k + 1, 0);
    R.d[k] = 1;
    BigInt r1 = mod(R, m);
    BigInt r2 = mod(mul(r1, r1), m);

    std::vector<uint32_t> b = padded(mod(base, m), k), rr = padded(r2, k);
    std::vector<uint32_t> x = padded(r1, k);  // 1 in Montgomery form
    mont.mul(b.data(), rr.data(), b.data());  // base in Montgomery form
    for (size_t i = exp.bits(); i-- > 0;) {
        mont.mul(x.data(), x.data(), x.data());
        if (exp.bit(i))
            mont.mul(x.data(), b.data(), x.data());
    }
    std::vector<uint32_t> one(k, 0);
    one[0] = 1;
    mont.mul(x.data(), one.data(), x.data());
    BigInt out;
    out.d = std::move(x);
    out.trim();
    return out;
}

bool BigInt::invmod(const BigInt &a, const BigInt &m, BigInt &out)
{
    // Extended Euclid on (m, a), tracking a's coefficient with a sign.
    BigInt r0 = m, r1 = mod(a, m);
    BigInt s0, s1(1);
    bool n0 = false, n1 = false;
    while (!r1.isZero()) {
        BigInt q, r2;
        divmod(r0, r1, &q, &r2);
        BigInt t = mul(q, s1);  // sign n1
        BigInt s2;
        bool n2;
        if (n0 != n1) {
            s2 = add(s0, t);
            n2 = n0;
        } else if (cmp(s0, t) >= 0) {
            s2 = sub(s0, t);
            n2 = n0;
        } else {
            s2 = sub(t, s0);
            n2 = !n0;
        }
        if (s2.isZero())
            n2 = false;
        r0 = std::move(r1);
        r1 = std::move(r2);
        s0 = std::move(s1);
        n0 = n1;
        s1 = std::move(s2);
        n1 = n2;
    }
    if (cmp(r0, BigInt(1)) != 0)
        return false;
    BigInt s = mod(s0, m);
    out = (n0 && !s.isZero()) ? sub(m, s) : s;
    return true;
}

// --- Primes and RSA ----------------------------------------------------------

static const std::vector<uint32_t> &smallPrimes()
{
    static std::vector<uint32_t> primes = [] {
        std::vector<uint32_t> out;
        const uint32_t limit = 2000;
        std::vector<bool> composite(limit, false);
        for (uint32_t i = 2; i < limit; i++) {
            if (composite[i])
                continue;
            out.push_back(i);
            for (uint32_t j = i * i; j < limit; j += i)
                composite[j] = true;
        }
        return out;
    }();
    return primes;
}

static BigInt randomBelow(const BigInt &bound, const RandomFn &random)
{
    // Uniform enough for Miller-Rabin bases: reduce a value 64 bits wider.
    size_t bytes = (bound.bits() + 7) / 8 + 8;
    std::vector<uint8_t> buf(bytes);
    random(buf.data(), buf.size());
    return BigInt::mod(BigInt::fromBytes(buf.data(), buf.size()), bound);
}

bool isProbablePrime(const BigInt &n, int rounds, const RandomFn &random)
{
    if (n.bits() <= 1)
        return false;
    for (uint32_t p : smallPrimes()) {
        if (n.d.size() == 1 && n.d[0] == p)
            return true;
        if (n.modSmall(p) == 0)
            return false;
    }
    BigInt one(1), nm1 = BigInt::sub(n, one);
    BigInt d = nm1;
    size_t s = 0;
    while (!d.isOdd()) {
        // d >>= 1
        for (size_t i = 0; i < d.d.size(); i++)
            d.d[i] = (d.d[i] >> 1) | (i + 1 < d.d.size() ? d.d[i + 1] << 31 : 0);
        while (!d.d.empty() && d.d.back() == 0)
            d.d.pop_back();
        s++;
    }
    BigInt three(3), two(2);
    BigInt range = BigInt::sub(n, three);  // bases in [2, n-2]
    for (int r = 0; r < rounds; r++) {
        BigInt a = BigInt::add(randomBelow(range, random), two);
        BigInt x = BigInt::powmod(a, d, n);
        if (BigInt::cmp(x, one) == 0 || BigInt::cmp(x, nm1) == 0)
            continue;
        bool witness = true;
        for (size_t i = 1; i < s; i++) {
            x = BigInt::powmod(x, two, n);
            if (BigInt::cmp(x, nm1) == 0) {
                witness = false;
                break;
            }
        }
        if (witness)
            return false;
    }
    return true;
}

static BigInt randomPrime(int bits, uint32_t e, const RandomFn &random)
{
    size_t bytes = (bits + 7) / 8;
    std::vector<uint8_t> buf(bytes);
    while (true) {
        random(buf.data(), buf.size());
        int extra = (int)bytes * 8 - bits;
        buf[0] &= (uint8_t)(0xFF >> extra);
        // Top two bits set, so the product of two such primes has exactly
        // 2 * bits bits; odd.
        buf[0] |= (uint8_t)(0xC0 >> extra);
        if (extra == 7)  // the second bit spills into the next byte
            buf[1] |= 0x80;
        buf[bytes - 1] |= 1;
        BigInt p = BigInt::fromBytes(buf.data(), buf.size());
        if (p.modSmall(e) == 1)  // gcd(e, p - 1) must be 1
            continue;
        if (isProbablePrime(p, 8, random)) {
            std::fill(buf.begin(), buf.end(), 0);
            return p;
        }
    }
}

RsaKey RsaKey::generate(int bits, const RandomFn &random)
{
    const uint32_t e = 65537;
    RsaKey k;
    k.e = BigInt(e);
    while (true) {
        k.p = randomPrime(bits / 2, e, random);
        k.q = randomPrime(bits - bits / 2, e, random);
        if (BigInt::cmp(k.p, k.q) == 0)
            continue;
        k.n = BigInt::mul(k.p, k.q);
        if ((int)k.n.bits() != bits)
            continue;
        BigInt one(1);
        if (!BigInt::invmod(k.e, BigInt::sub(k.p, one), k.dp) ||
            !BigInt::invmod(k.e, BigInt::sub(k.q, one), k.dq) || !BigInt::invmod(k.q, k.p, k.qinv))
            continue;
        return k;
    }
}

BigInt RsaKey::privateOp(const BigInt &c) const
{
    BigInt m1 = BigInt::powmod(c, dp, p);
    BigInt m2 = BigInt::powmod(c, dq, q);
    BigInt m2p = BigInt::mod(m2, p);
    BigInt diff = BigInt::cmp(m1, m2p) >= 0 ? BigInt::sub(m1, m2p) : BigInt::sub(BigInt::add(m1, p), m2p);
    BigInt h = BigInt::mod(BigInt::mul(diff, qinv), p);
    return BigInt::add(m2, BigInt::mul(h, q));
}

static void derLength(std::vector<uint8_t> &out, size_t len)
{
    if (len < 0x80) {
        out.push_back((uint8_t)len);
    } else if (len < 0x100) {
        out.push_back(0x81);
        out.push_back((uint8_t)len);
    } else {
        out.push_back(0x82);
        out.push_back((uint8_t)(len >> 8));
        out.push_back((uint8_t)len);
    }
}

static void derTlv(std::vector<uint8_t> &out, uint8_t tag, const std::vector<uint8_t> &body)
{
    out.push_back(tag);
    derLength(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
}

static std::vector<uint8_t> derInteger(const BigInt &v)
{
    std::vector<uint8_t> bytes = v.toBytes();
    if (bytes[0] & 0x80)
        bytes.insert(bytes.begin(), 0);
    std::vector<uint8_t> out;
    derTlv(out, 0x02, bytes);
    return out;
}

std::vector<uint8_t> RsaKey::publicKeySpki() const
{
    std::vector<uint8_t> rsa_key_body = derInteger(n), e_der = derInteger(e);
    rsa_key_body.insert(rsa_key_body.end(), e_der.begin(), e_der.end());
    std::vector<uint8_t> rsa_key;
    derTlv(rsa_key, 0x30, rsa_key_body);

    // AlgorithmIdentifier { rsaEncryption (1.2.840.113549.1.1.1), NULL }
    static const uint8_t kAlg[] = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                   0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    std::vector<uint8_t> bit_string(1, 0);  // no unused bits
    bit_string.insert(bit_string.end(), rsa_key.begin(), rsa_key.end());

    std::vector<uint8_t> body(kAlg, kAlg + sizeof(kAlg));
    derTlv(body, 0x03, bit_string);
    std::vector<uint8_t> out;
    derTlv(out, 0x30, body);
    return out;
}

static std::vector<uint8_t> mgf1(const uint8_t *seed, size_t seed_len, size_t len)
{
    std::vector<uint8_t> out;
    std::vector<uint8_t> buf(seed, seed + seed_len);
    buf.resize(seed_len + 4);
    for (uint32_t counter = 0; out.size() < len; counter++) {
        buf[seed_len] = (uint8_t)(counter >> 24);
        buf[seed_len + 1] = (uint8_t)(counter >> 16);
        buf[seed_len + 2] = (uint8_t)(counter >> 8);
        buf[seed_len + 3] = (uint8_t)counter;
        Sha256Digest h = sha256(buf.data(), buf.size());
        out.insert(out.end(), h.begin(), h.end());
    }
    out.resize(len);
    return out;
}

bool RsaKey::decryptOaepSha256(std::string_view ciphertext, std::string &plain) const
{
    const size_t k = (n.bits() + 7) / 8, hlen = 32;
    if (ciphertext.size() != k || k < 2 * hlen + 2)
        return false;
    BigInt c = BigInt::fromBytes((const uint8_t *)ciphertext.data(), k);
    if (BigInt::cmp(c, n) >= 0)
        return false;
    std::vector<uint8_t> em = privateOp(c).toBytes(k);

    uint8_t *masked_seed = em.data() + 1;
    uint8_t *masked_db = em.data() + 1 + hlen;
    size_t db_len = k - hlen - 1;
    std::vector<uint8_t> seed_mask = mgf1(masked_db, db_len, hlen);
    for (size_t i = 0; i < hlen; i++)
        masked_seed[i] ^= seed_mask[i];
    std::vector<uint8_t> db_mask = mgf1(masked_seed, hlen, db_len);
    for (size_t i = 0; i < db_len; i++)
        masked_db[i] ^= db_mask[i];

    // DB = lHash || PS (zeros) || 0x01 || M. Check everything before
    // deciding, so failures don't leak which part was wrong.
    static const Sha256Digest empty_hash = sha256("", 0);
    uint8_t bad = em[0];
    for (size_t i = 0; i < hlen; i++)
        bad |= masked_db[i] ^ empty_hash[i];
    size_t msg_start = 0;
    bool found = false;
    for (size_t i = hlen; i < db_len; i++) {
        if (!found && masked_db[i] == 0x01) {
            found = true;
            msg_start = i + 1;
        } else if (!found && masked_db[i] != 0) {
            bad |= 1;
        }
    }
    bool ok = !bad && found;
    if (ok)
        plain.assign((const char *)masked_db + msg_start, db_len - msg_start);
    std::fill(em.begin(), em.end(), 0);
    return ok;
}

void RsaKey::clear()
{
    for (BigInt *v : {&p, &q, &dp, &dq, &qinv}) {
        std::fill(v->d.begin(), v->d.end(), 0);
        v->d.clear();
    }
}

}  // namespace crypto
