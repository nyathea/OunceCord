// SPDX-License-Identifier: MIT
//
// A TLS client connection through Horizon's ssl service. The system verifies
// the server certificate against its trusted CA store and the host name.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <switch.h>

// Services the network code needs: nifm, sockets and ssl. Call once.
bool netInitialize();
void netExit();
// Whether the console currently has an internet connection.
bool netIsConnected();

class TlsSocket {
public:
    TlsSocket() = default;
    ~TlsSocket() { close(); }
    TlsSocket(const TlsSocket &) = delete;
    TlsSocket &operator=(const TlsSocket &) = delete;

    bool connect(const std::string &host, uint16_t port, std::string *error);
    void close();
    bool isOpen() const { return m_open; }

    // Writes all bytes. Returns false on error.
    bool writeAll(const void *data, size_t size);
    // Reads up to size bytes, blocking. Returns bytes read, 0 on EOF, -1 on error.
    long read(void *data, size_t size);
    // Waits up to timeout_ms for readable data. 1 = readable, 0 = timeout, -1 = error.
    int waitReadable(uint32_t timeout_ms);

private:
    bool m_open = false;
    bool m_hasContext = false;
    SslContext m_context{};
    SslConnection m_conn{};
};
