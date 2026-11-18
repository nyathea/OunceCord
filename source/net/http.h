// SPDX-License-Identifier: MIT
//
// HTTPS/1.1 client for one host, keeping its connection alive between
// requests. Not thread-safe: use one client per thread.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "net/tls.h"

struct HttpResponse {
    int status = 0;  // 0 when the request did not complete
    std::vector<std::pair<std::string, std::string>> headers;  // lower-cased names
    std::string body;
    std::string error;  // transport error, when status == 0

    const std::string *header(const char *name) const;
};

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

class HttpClient {
public:
    explicit HttpClient(std::string host) : m_host(std::move(host)) {}

    HttpResponse request(const std::string &method, const std::string &path,
                         const HttpHeaders &headers, const std::string &body = {});
    void close() { m_socket.close(); }

private:
    std::string m_host;
    TlsSocket m_socket;

    bool send(const std::string &method, const std::string &path, const HttpHeaders &headers,
              const std::string &body);
};

// Reads the response to an HTTP request from a socket: status line, headers,
// and (when read_body) the body. Shared with the WebSocket handshake. `extra`
// returns bytes read past the headers when read_body is false.
bool httpReadResponse(TlsSocket &socket, HttpResponse &res, bool read_body, bool &keep_alive,
                      std::string *extra);
