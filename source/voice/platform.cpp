// SPDX-License-Identifier: MIT
//
// What the voice libraries (OpenSSL, built for the console with no entropy
// source of its own) need from the system: randomness from the console's
// hardware CSRNG, and a few POSIX bits newlib lacks. As in DiscordNX2 (MIT).
#if defined(__SWITCH__)
#include <cerrno>
#include <cstdlib>
#include <malloc.h>

#include <openssl/rand.h>
#include <switch.h>

#include "util/util.h"

namespace {
bool g_csrng = false;  // the csrng service is open

// Random bytes for OpenSSL: the csrng service, in pieces it accepts, else
// libnx's generator (seeded from the kernel's entropy).
int randomBytesFor(unsigned char *out, int n)
{
    if (n < 0)
        return 0;
    size_t left = (size_t)n;
    while (left) {
        size_t piece = left < 0x1000 ? left : 0x1000;
        if (!g_csrng || R_FAILED(csrngGetRandomBytes(out, piece)))
            randomGet(out, piece);
        out += piece;
        left -= piece;
    }
    return 1;
}
int randomStatus() { return 1; }
const RAND_METHOD kRandom = {nullptr, randomBytesFor, nullptr, nullptr, randomBytesFor, randomStatus};
}  // namespace

namespace voice {
// Called once before any voice crypto runs.
// RAND_set_rand_method is deprecated in OpenSSL 3 but still the simplest
// way to hand it an entropy source without writing a provider.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
bool initRandom()
{
    // The service has to be opened before csrngGetRandomBytes works;
    // without it every request failed and OpenSSL couldn't make keys.
    Result rc = csrngInitialize();
    g_csrng = R_SUCCEEDED(rc);
    if (g_csrng) {
        unsigned char probe[16];
        g_csrng = R_SUCCEEDED(csrngGetRandomBytes(probe, sizeof(probe)));
    }
    logf("voice: random numbers from %s (csrng 0x%x)", g_csrng ? "csrng" : "the kernel-seeded generator", rc);
    if (RAND_set_rand_method(&kRandom) != 1)
        return false;
    unsigned char test[32];
    bool ok = RAND_bytes(test, sizeof(test)) == 1;
    if (!ok)
        logf("voice: OpenSSL can't get random numbers");
    return ok;
}
#pragma GCC diagnostic pop
}  // namespace voice

extern "C" {
// No set-uid programs on the console.
int OPENSSL_issetugid(void) { return 0; }
int posix_memalign(void **out, size_t alignment, size_t size)
{
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)))
        return EINVAL;
    void *p = memalign(alignment, size);
    if (!p)
        return ENOMEM;
    *out = p;
    return 0;
}
}
#else
namespace voice {
bool initRandom() { return true; }
}  // namespace voice
#endif
