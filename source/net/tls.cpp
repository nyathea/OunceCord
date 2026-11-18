// SPDX-License-Identifier: MIT
#include "net/tls.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "util/util.h"

static bool g_socketReady, g_sslReady, g_nifmReady;

bool netInitialize()
{
    Result rc = nifmInitialize(NifmServiceType_User);
    g_nifmReady = R_SUCCEEDED(rc);
    if (!g_nifmReady)
        logf("nifmInitialize failed: 0x%x", rc);

    // A larger socket buffer than the default helps with the big READY payload.
    SocketInitConfig cfg = *socketGetDefaultInitConfig();
    cfg.tcp_rx_buf_size = 0x40000;
    cfg.tcp_rx_buf_max_size = 0x100000;
    cfg.sb_efficiency = 8;
    rc = socketInitialize(&cfg);
    if (R_FAILED(rc)) {
        logf("socketInitialize failed: 0x%x, retrying with defaults", rc);
        rc = socketInitializeDefault();
    }
    g_socketReady = R_SUCCEEDED(rc);
    if (!g_socketReady) {
        logf("socketInitializeDefault failed: 0x%x", rc);
        return false;
    }

    rc = sslInitialize(3);
    g_sslReady = R_SUCCEEDED(rc);
    if (!g_sslReady) {
        logf("sslInitialize failed: 0x%x", rc);
        return false;
    }
    return true;
}

void netExit()
{
    if (g_sslReady)
        sslExit();
    if (g_socketReady)
        socketExit();
    if (g_nifmReady)
        nifmExit();
    g_sslReady = g_socketReady = g_nifmReady = false;
}

bool netIsConnected()
{
    if (!g_nifmReady)
        return true;  // Unknown: let the connection attempt decide.
    NifmInternetConnectionStatus status;
    if (R_FAILED(nifmGetInternetConnectionStatus(nullptr, nullptr, &status)))
        return false;
    return status == NifmInternetConnectionStatus_Connected;
}

bool TlsSocket::connect(const std::string &host, uint16_t port, std::string *error)
{
    close();
    auto setError = [&](const char *what, long code) {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s (%s: 0x%lx)", what, host.c_str(), code);
        logf("tls: %s", buf);
        if (error)
            *error = buf;
    };

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    int gai = getaddrinfo(host.c_str(), port_str, &hints, &res);
    if (gai != 0 || !res) {
        setError("DNS lookup failed", gai);
        return false;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        setError("TCP connect failed", errno);
        return false;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    Result rc = sslCreateContext(&m_context, SslVersion_Auto);
    if (R_FAILED(rc)) {
        ::close(fd);
        setError("sslCreateContext failed", rc);
        return false;
    }
    m_hasContext = true;

    rc = sslContextCreateConnection(&m_context, &m_conn);
    if (R_FAILED(rc)) {
        ::close(fd);
        setError("sslContextCreateConnection failed", rc);
        close();
        return false;
    }

    // The ssl service takes the socket over; our descriptor is closed by it.
    if (socketSslConnectionSetSocketDescriptor(&m_conn, fd) == -1 && errno != ENOENT) {
        ::close(fd);
        setError("SetSocketDescriptor failed", errno);
        sslConnectionClose(&m_conn);
        close();
        return false;
    }

    rc = sslConnectionSetHostName(&m_conn, host.c_str(), host.size() + 1);
    if (R_SUCCEEDED(rc))
        rc = sslConnectionSetIoMode(&m_conn, SslIoMode_Blocking);
    if (R_SUCCEEDED(rc))
        rc = sslConnectionSetSessionCacheMode(&m_conn, SslSessionCacheMode_SessionId);
    if (R_FAILED(rc)) {
        setError("ssl connection setup failed", rc);
        sslConnectionClose(&m_conn);
        close();
        return false;
    }

    rc = sslConnectionDoHandshake(&m_conn, nullptr, nullptr, nullptr, 0);
    if (R_FAILED(rc)) {
        Result verify = sslConnectionGetVerifyCertError(&m_conn);
        char buf[96];
        snprintf(buf, sizeof(buf), "TLS handshake failed (verify 0x%x)", verify);
        setError(buf, rc);
        sslConnectionClose(&m_conn);
        close();
        return false;
    }

    m_open = true;
    return true;
}

void TlsSocket::close()
{
    if (m_open) {
        sslConnectionClose(&m_conn);
        m_open = false;
    }
    if (m_hasContext) {
        sslContextClose(&m_context);
        m_hasContext = false;
    }
}

bool TlsSocket::writeAll(const void *data, size_t size)
{
    if (!m_open)
        return false;
    const uint8_t *p = (const uint8_t *)data;
    while (size) {
        u32 written = 0;
        u32 chunk = size > 0x10000 ? 0x10000 : (u32)size;
        Result rc = sslConnectionWrite(&m_conn, p, chunk, &written);
        if (R_FAILED(rc) || written == 0) {
            logf("tls: write failed 0x%x", rc);
            return false;
        }
        p += written;
        size -= written;
    }
    return true;
}

long TlsSocket::read(void *data, size_t size)
{
    if (!m_open)
        return -1;
    u32 got = 0;
    u32 chunk = size > 0x10000 ? 0x10000 : (u32)size;
    Result rc = sslConnectionRead(&m_conn, data, chunk, &got);
    if (R_FAILED(rc)) {
        logf("tls: read failed 0x%x", rc);
        return -1;
    }
    return got;
}

int TlsSocket::waitReadable(uint32_t timeout_ms)
{
    if (!m_open)
        return -1;
    s32 pending = 0;
    if (R_SUCCEEDED(sslConnectionPending(&m_conn, &pending)) && pending > 0)
        return 1;
    u32 events = 0;
    Result rc = sslConnectionPoll(&m_conn, SslPollEvent_Read | SslPollEvent_Except, &events,
                                  timeout_ms);
    if (R_FAILED(rc)) {
        // A timeout is reported as an error result by some firmware versions.
        if (events == 0)
            return 0;
        return -1;
    }
    if (events & SslPollEvent_Except)
        return -1;
    return (events & SslPollEvent_Read) ? 1 : 0;
}
