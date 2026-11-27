// SPDX-License-Identifier: MIT
#include "net/websocket.h"

#include <cstring>

#include "util/util.h"

enum : uint8_t {
    Op_Continuation = 0x0,
    Op_Text = 0x1,
    Op_Binary = 0x2,
    Op_Close = 0x8,
    Op_Ping = 0x9,
    Op_Pong = 0xA,
};

bool WebSocket::connect(const std::string &host, const std::string &path,
                        const HttpHeaders &headers, std::string *error, int port)
{
    close();
    m_buf.clear();
    m_frag.clear();
    m_closeCode = 0;
    if (!m_socket.connect(host, port, error))
        return false;

    uint8_t key_bytes[16];
    randomBytes(key_bytes, sizeof(key_bytes));
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + (port != 443 ? ":" + std::to_string(port) : std::string()) +
                      "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
                      base64Encode(key_bytes, sizeof(key_bytes)) + "\r\n";
    for (const auto &h : headers)
        req += h.first + ": " + h.second + "\r\n";
    req += "\r\n";
    if (!m_socket.writeAll(req.data(), req.size())) {
        if (error)
            *error = "WebSocket handshake send failed";
        m_socket.close();
        return false;
    }

    HttpResponse res;
    bool keep_alive;
    if (!httpReadResponse(m_socket, res, false, keep_alive, &m_buf) || res.status != 101) {
        if (error)
            *error = "WebSocket upgrade refused (HTTP " + std::to_string(res.status) + ")";
        logf("ws: upgrade failed, status %d %s", res.status, res.error.c_str());
        m_socket.close();
        return false;
    }
    return true;
}

void WebSocket::close(uint16_t code)
{
    if (m_socket.isOpen()) {
        uint8_t payload[2] = {(uint8_t)(code >> 8), (uint8_t)code};
        sendFrame(Op_Close, payload, sizeof(payload));
        m_socket.close();
    }
}

bool WebSocket::sendFrame(uint8_t opcode, const void *data, size_t size)
{
    std::string frame;
    frame.reserve(size + 14);
    frame.push_back((char)(0x80 | opcode));
    if (size < 126) {
        frame.push_back((char)(0x80 | size));
    } else if (size < 65536) {
        frame.push_back((char)(0x80 | 126));
        frame.push_back((char)(size >> 8));
        frame.push_back((char)size);
    } else {
        frame.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; i--)
            frame.push_back((char)((uint64_t)size >> (i * 8)));
    }
    uint8_t mask[4];
    randomBytes(mask, sizeof(mask));
    frame.append((const char *)mask, 4);
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++)
        frame.push_back((char)(p[i] ^ mask[i & 3]));
    return m_socket.writeAll(frame.data(), frame.size());
}

bool WebSocket::sendText(const std::string &text)
{
    return sendFrame(Op_Text, text.data(), text.size());
}

bool WebSocket::sendBinary(const std::string &data)
{
    return sendFrame(Op_Binary, data.data(), data.size());
}

int WebSocket::parseFrame(uint8_t &opcode, bool &fin, std::string &payload)
{
    if (m_buf.size() < 2)
        return 0;
    const uint8_t *p = (const uint8_t *)m_buf.data();
    fin = p[0] & 0x80;
    opcode = p[0] & 0x0F;
    bool masked = p[1] & 0x80;
    uint64_t len = p[1] & 0x7F;
    size_t pos = 2;
    if (len == 126) {
        if (m_buf.size() < 4)
            return 0;
        len = (p[2] << 8) | p[3];
        pos = 4;
    } else if (len == 127) {
        if (m_buf.size() < 10)
            return 0;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | p[2 + i];
        pos = 10;
    }
    if (len > (64u << 20))
        return -1;
    uint8_t mask[4] = {};
    if (masked) {
        if (m_buf.size() < pos + 4)
            return 0;
        memcpy(mask, p + pos, 4);
        pos += 4;
    }
    if (m_buf.size() < pos + len)
        return 0;
    payload.assign(m_buf, pos, len);
    if (masked)
        for (size_t i = 0; i < payload.size(); i++)
            payload[i] ^= mask[i & 3];
    m_buf.erase(0, pos + len);
    return 1;
}

WebSocket::Result WebSocket::receive(Message &out, uint32_t timeout_ms)
{
    if (!m_socket.isOpen())
        return Result::Closed;
    uint64_t deadline = nowMs() + timeout_ms;
    while (true) {
        uint8_t opcode;
        bool fin;
        std::string payload;
        int r = parseFrame(opcode, fin, payload);
        if (r < 0) {
            logf("ws: protocol error");
            m_socket.close();
            return Result::Error;
        }
        if (r > 0) {
            switch (opcode) {
            case Op_Ping:
                sendFrame(Op_Pong, payload.data(), payload.size());
                continue;
            case Op_Pong:
                continue;
            case Op_Close:
                m_closeCode = payload.size() >= 2
                                  ? (uint16_t)(((uint8_t)payload[0] << 8) | (uint8_t)payload[1])
                                  : 0;
                logf("ws: server closed, code %u", m_closeCode);
                sendFrame(Op_Close, payload.data(), payload.size() >= 2 ? 2 : 0);
                m_socket.close();
                return Result::Closed;
            case Op_Text:
            case Op_Binary:
                m_frag = std::move(payload);
                m_fragBinary = opcode == Op_Binary;
                break;
            case Op_Continuation:
                m_frag += payload;
                break;
            default:
                continue;
            }
            if (fin) {
                out.binary = m_fragBinary;
                out.data = std::move(m_frag);
                m_frag.clear();
                return Result::Message;
            }
            continue;
        }

        uint64_t now = nowMs();
        uint32_t wait = now >= deadline ? 0 : (uint32_t)(deadline - now);
        int ready = m_socket.waitReadable(wait);
        if (ready < 0) {
            m_socket.close();
            return Result::Error;
        }
        if (ready == 0)
            return Result::Timeout;
        char tmp[16384];
        long n = m_socket.read(tmp, sizeof(tmp));
        if (n < 0) {
            m_socket.close();
            return Result::Error;
        }
        if (n == 0) {
            m_socket.close();
            return Result::Closed;
        }
        m_buf.append(tmp, n);
    }
}
