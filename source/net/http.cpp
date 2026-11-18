// SPDX-License-Identifier: MIT
#include "net/http.h"

#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "util/util.h"

const std::string *HttpResponse::header(const char *name) const
{
    for (const auto &h : headers)
        if (strcasecmp(h.first.c_str(), name) == 0)
            return &h.second;
    return nullptr;
}

static std::string toLower(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = c - 'A' + 'a';
    return s;
}

static std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        b--;
    return s.substr(a, b - a);
}

namespace {
// Buffered reader over a TlsSocket.
class Reader {
public:
    Reader(TlsSocket &s, std::string initial) : m_s(s), m_buf(std::move(initial)) {}

    bool fill()
    {
        char tmp[16384];
        long n = m_s.read(tmp, sizeof(tmp));
        if (n <= 0)
            return false;
        m_buf.append(tmp, n);
        return true;
    }

    bool line(std::string &out)
    {
        while (true) {
            size_t nl = m_buf.find('\n', m_pos);
            if (nl != std::string::npos) {
                out.assign(m_buf, m_pos, nl - m_pos);
                if (!out.empty() && out.back() == '\r')
                    out.pop_back();
                m_pos = nl + 1;
                return true;
            }
            compact();
            if (!fill())
                return false;
        }
    }

    bool bytes(size_t n, std::string &out)
    {
        while (m_buf.size() - m_pos < n) {
            compact();
            if (!fill())
                return false;
        }
        out.append(m_buf, m_pos, n);
        m_pos += n;
        return true;
    }

    bool rest(std::string &out)
    {
        out.append(m_buf, m_pos, std::string::npos);
        m_pos = m_buf.size();
        while (true) {
            char tmp[16384];
            long n = m_s.read(tmp, sizeof(tmp));
            if (n < 0)
                return false;
            if (n == 0)
                return true;
            out.append(tmp, n);
        }
    }

    std::string leftover() const { return m_buf.substr(m_pos); }

private:
    TlsSocket &m_s;
    std::string m_buf;
    size_t m_pos = 0;

    void compact()
    {
        if (m_pos > 0) {
            m_buf.erase(0, m_pos);
            m_pos = 0;
        }
    }
};
}  // namespace

static bool readHead(Reader &r, HttpResponse &res, bool &keep_alive)
{
    std::string line;
    do {
        if (!r.line(line)) {
            res.error = "connection closed before response";
            return false;
        }
    } while (line.empty());
    // "HTTP/1.1 200 OK"
    size_t sp = line.find(' ');
    if (line.compare(0, 5, "HTTP/") != 0 || sp == std::string::npos) {
        res.error = "bad status line";
        return false;
    }
    res.status = atoi(line.c_str() + sp + 1);
    keep_alive = line.compare(0, 8, "HTTP/1.1") == 0;
    while (true) {
        if (!r.line(line)) {
            res.error = "connection closed in headers";
            res.status = 0;
            return false;
        }
        if (line.empty())
            break;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = toLower(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (name == "connection")
            keep_alive = toLower(value) != "close";
        res.headers.emplace_back(std::move(name), std::move(value));
    }
    return true;
}

static bool readBody(Reader &r, HttpResponse &res, bool &keep_alive)
{
    if (res.status == 204 || res.status == 304 || (res.status >= 100 && res.status < 200))
        return true;
    const std::string *te = res.header("transfer-encoding");
    if (te && toLower(*te).find("chunked") != std::string::npos) {
        std::string line;
        while (true) {
            if (!r.line(line))
                return false;
            size_t size = strtoul(line.c_str(), nullptr, 16);
            if (size == 0) {
                // Trailers until an empty line.
                while (r.line(line) && !line.empty()) {
                }
                return true;
            }
            if (!r.bytes(size, res.body))
                return false;
            if (!r.line(line))  // CRLF after the chunk
                return false;
        }
    }
    const std::string *cl = res.header("content-length");
    if (cl)
        return r.bytes(strtoull(cl->c_str(), nullptr, 10), res.body);
    // Read to EOF.
    keep_alive = false;
    return r.rest(res.body);
}

bool httpReadResponse(TlsSocket &socket, HttpResponse &res, bool read_body, bool &keep_alive,
                      std::string *extra)
{
    Reader r(socket, {});
    if (!readHead(r, res, keep_alive))
        return false;
    if (!read_body) {
        if (extra)
            *extra = r.leftover();
        return true;
    }
    if (!readBody(r, res, keep_alive)) {
        res.error = "connection closed in body";
        res.status = 0;
        return false;
    }
    return true;
}

bool HttpClient::send(const std::string &method, const std::string &path,
                      const HttpHeaders &headers, const std::string &body)
{
    std::string req;
    req.reserve(1024 + body.size());
    req += method;
    req += ' ';
    req += path;
    req += " HTTP/1.1\r\nHost: ";
    req += m_host;
    req += "\r\nConnection: keep-alive\r\n";
    for (const auto &h : headers) {
        req += h.first;
        req += ": ";
        req += h.second;
        req += "\r\n";
    }
    if (!body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        req += "Content-Length: ";
        req += std::to_string(body.size());
        req += "\r\n";
    }
    req += "\r\n";
    req += body;
    return m_socket.writeAll(req.data(), req.size());
}

HttpResponse HttpClient::request(const std::string &method, const std::string &path,
                                 const HttpHeaders &headers, const std::string &body)
{
    HttpResponse res;
    // A kept-alive connection may have been closed by the server; retry once
    // on a fresh connection when the reused one fails before any response.
    for (int attempt = 0; attempt < 2; attempt++) {
        bool reused = m_socket.isOpen();
        if (!reused && !m_socket.connect(m_host, 443, &res.error))
            return res;
        res = HttpResponse();
        if (!send(method, path, headers, body)) {
            m_socket.close();
            res.error = "send failed";
            if (reused)
                continue;
            return res;
        }
        bool keep_alive = true;
        bool ok = httpReadResponse(m_socket, res, method != "HEAD", keep_alive, nullptr);
        if (!ok) {
            m_socket.close();
            if (reused && res.status == 0)
                continue;
            return res;
        }
        if (!keep_alive)
            m_socket.close();
        return res;
    }
    return res;
}
