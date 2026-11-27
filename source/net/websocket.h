// SPDX-License-Identifier: MIT
//
// A minimal RFC 6455 WebSocket client over TLS (wss://).
#pragma once

#include <string>

#include "net/http.h"
#include "net/tls.h"

class WebSocket {
public:
    enum class Result { Message, Timeout, Closed, Error };

    struct Message {
        bool binary = false;
        std::string data;
    };

    // TLS to host:port (voice servers may use ports other than 443).
    bool connect(const std::string &host, const std::string &path, const HttpHeaders &headers,
                 std::string *error, int port = 443);
    void close(uint16_t code = 1000);
    bool isOpen() const { return m_socket.isOpen(); }

    bool sendText(const std::string &text);
    bool sendBinary(const std::string &data);
    // Waits up to timeout_ms for a complete message. Pings are answered here.
    Result receive(Message &out, uint32_t timeout_ms);
    // Close code the server sent, when receive returned Closed (0 if none).
    uint16_t closeCode() const { return m_closeCode; }

private:
    TlsSocket m_socket;
    std::string m_buf;     // received bytes not yet parsed
    std::string m_frag;    // fragmented message being assembled
    bool m_fragBinary = false;
    uint16_t m_closeCode = 0;

    bool sendFrame(uint8_t opcode, const void *data, size_t size);
    // Parses one frame from m_buf. Returns 1 if a frame was consumed, 0 if more
    // data is needed, -1 on protocol error.
    int parseFrame(uint8_t &opcode, bool &fin, std::string &payload);
};
