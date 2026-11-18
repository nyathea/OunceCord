// SPDX-License-Identifier: MIT
#include "util/json.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

static const Value g_null;
static const std::string g_emptyString;

Value Value::boolean(bool b)
{
    Value v;
    v.m_type = Type::Bool;
    v.m_bool = b;
    return v;
}

Value Value::number(double d)
{
    Value v;
    v.m_type = Type::Number;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", d);
    v.m_text = buf;
    return v;
}

Value Value::number(int64_t i)
{
    Value v;
    v.m_type = Type::Number;
    v.m_text = std::to_string(i);
    return v;
}

Value Value::string(std::string s)
{
    Value v;
    v.m_type = Type::String;
    v.m_text = std::move(s);
    return v;
}

Value Value::array()
{
    Value v;
    v.m_type = Type::Array;
    return v;
}

Value Value::object()
{
    Value v;
    v.m_type = Type::Object;
    return v;
}

const Value &Value::operator[](std::string_view key) const
{
    if (m_type != Type::Object)
        return g_null;
    for (const auto &m : m_object)
        if (m.first == key)
            return m.second;
    return g_null;
}

const Value &Value::operator[](size_t index) const
{
    if (m_type != Type::Array || index >= m_array.size())
        return g_null;
    return m_array[index];
}

bool Value::has(std::string_view key) const
{
    if (m_type != Type::Object)
        return false;
    for (const auto &m : m_object)
        if (m.first == key)
            return true;
    return false;
}

size_t Value::size() const
{
    if (m_type == Type::Array)
        return m_array.size();
    if (m_type == Type::Object)
        return m_object.size();
    return 0;
}

bool Value::asBool(bool fallback) const
{
    return m_type == Type::Bool ? m_bool : fallback;
}

double Value::asDouble(double fallback) const
{
    if (m_type == Type::Number || m_type == Type::String)
        return m_text.empty() ? fallback : strtod(m_text.c_str(), nullptr);
    return fallback;
}

int64_t Value::asInt(int64_t fallback) const
{
    if (m_type == Type::Number || m_type == Type::String) {
        if (m_text.empty())
            return fallback;
        return strtoll(m_text.c_str(), nullptr, 10);
    }
    if (m_type == Type::Bool)
        return m_bool;
    return fallback;
}

uint64_t Value::asId() const
{
    if (m_type == Type::Number || m_type == Type::String)
        return strtoull(m_text.c_str(), nullptr, 10);
    return 0;
}

const std::string &Value::asString() const
{
    return m_type == Type::String ? m_text : g_emptyString;
}

std::string Value::str(std::string_view fallback) const
{
    if (m_type == Type::String)
        return m_text;
    return std::string(fallback);
}

Value &Value::push(Value v)
{
    if (m_type != Type::Array) {
        *this = array();
    }
    m_array.push_back(std::move(v));
    return m_array.back();
}

Value &Value::set(std::string key, Value v)
{
    if (m_type != Type::Object)
        *this = object();
    for (auto &m : m_object) {
        if (m.first == key) {
            m.second = std::move(v);
            return m.second;
        }
    }
    m_object.emplace_back(std::move(key), std::move(v));
    return m_object.back().second;
}

void escapeTo(std::string &out, std::string_view s)
{
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back((char)c);
            }
        }
    }
    out.push_back('"');
}

void Value::dumpTo(std::string &out) const
{
    switch (m_type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += m_bool ? "true" : "false"; break;
    case Type::Number: out += m_text; break;
    case Type::String: escapeTo(out, m_text); break;
    case Type::Array:
        out.push_back('[');
        for (size_t i = 0; i < m_array.size(); i++) {
            if (i)
                out.push_back(',');
            m_array[i].dumpTo(out);
        }
        out.push_back(']');
        break;
    case Type::Object:
        out.push_back('{');
        for (size_t i = 0; i < m_object.size(); i++) {
            if (i)
                out.push_back(',');
            escapeTo(out, m_object[i].first);
            out.push_back(':');
            m_object[i].second.dumpTo(out);
        }
        out.push_back('}');
        break;
    }
}

std::string Value::dump() const
{
    std::string out;
    dumpTo(out);
    return out;
}

// --- Parser ---------------------------------------------------------------

class Parser {
public:
    explicit Parser(std::string_view t) : m_p(t.data()), m_end(t.data() + t.size()) {}

    bool parseDocument(Value &out, std::string *error)
    {
        skipWs();
        if (!parseValue(out, 0)) {
            if (error)
                *error = m_error.empty() ? "parse error" : m_error;
            return false;
        }
        return true;
    }

private:
    const char *m_p;
    const char *m_end;
    std::string m_error;

    static constexpr int kMaxDepth = 128;

    bool fail(const char *msg)
    {
        if (m_error.empty())
            m_error = msg;
        return false;
    }

    void skipWs()
    {
        while (m_p < m_end && (*m_p == ' ' || *m_p == '\n' || *m_p == '\r' || *m_p == '\t'))
            m_p++;
    }

    bool literal(const char *word)
    {
        size_t n = strlen(word);
        if ((size_t)(m_end - m_p) < n || memcmp(m_p, word, n) != 0)
            return fail("bad literal");
        m_p += n;
        return true;
    }

    static void appendUtf8(std::string &out, uint32_t cp)
    {
        if (cp < 0x80) {
            out.push_back((char)cp);
        } else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool hex4(uint32_t &out)
    {
        if (m_end - m_p < 4)
            return fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; i++) {
            char c = *m_p++;
            out <<= 4;
            if (c >= '0' && c <= '9') out |= c - '0';
            else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool parseString(std::string &out)
    {
        m_p++;  // opening quote
        const char *run = m_p;
        while (true) {
            if (m_p >= m_end)
                return fail("unterminated string");
            char c = *m_p;
            if (c == '"') {
                out.append(run, m_p - run);
                m_p++;
                return true;
            }
            if (c != '\\') {
                m_p++;
                continue;
            }
            out.append(run, m_p - run);
            m_p++;
            if (m_p >= m_end)
                return fail("bad escape");
            char e = *m_p++;
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t cp;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && m_end - m_p >= 6 && m_p[0] == '\\' && m_p[1] == 'u') {
                    m_p += 2;
                    uint32_t lo;
                    if (!hex4(lo))
                        return false;
                    if (lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    else
                        cp = 0xFFFD;
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    cp = 0xFFFD;
                }
                appendUtf8(out, cp);
                break;
            }
            default: return fail("bad escape");
            }
            run = m_p;
        }
    }

    bool parseValue(Value &out, int depth)
    {
        if (depth > kMaxDepth)
            return fail("nesting too deep");
        if (m_p >= m_end)
            return fail("unexpected end");
        switch (*m_p) {
        case 'n':
            out.m_type = Type::Null;
            return literal("null");
        case 't':
            out.m_type = Type::Bool;
            out.m_bool = true;
            return literal("true");
        case 'f':
            out.m_type = Type::Bool;
            out.m_bool = false;
            return literal("false");
        case '"':
            out.m_type = Type::String;
            return parseString(out.m_text);
        case '[': {
            out.m_type = Type::Array;
            m_p++;
            skipWs();
            if (m_p < m_end && *m_p == ']') {
                m_p++;
                return true;
            }
            while (true) {
                out.m_array.emplace_back();
                skipWs();
                if (!parseValue(out.m_array.back(), depth + 1))
                    return false;
                skipWs();
                if (m_p >= m_end)
                    return fail("unterminated array");
                if (*m_p == ',') {
                    m_p++;
                    continue;
                }
                if (*m_p == ']') {
                    m_p++;
                    return true;
                }
                return fail("expected , or ]");
            }
        }
        case '{': {
            out.m_type = Type::Object;
            m_p++;
            skipWs();
            if (m_p < m_end && *m_p == '}') {
                m_p++;
                return true;
            }
            while (true) {
                skipWs();
                if (m_p >= m_end || *m_p != '"')
                    return fail("expected key");
                out.m_object.emplace_back();
                if (!parseString(out.m_object.back().first))
                    return false;
                skipWs();
                if (m_p >= m_end || *m_p != ':')
                    return fail("expected :");
                m_p++;
                skipWs();
                if (!parseValue(out.m_object.back().second, depth + 1))
                    return false;
                skipWs();
                if (m_p >= m_end)
                    return fail("unterminated object");
                if (*m_p == ',') {
                    m_p++;
                    continue;
                }
                if (*m_p == '}') {
                    m_p++;
                    return true;
                }
                return fail("expected , or }");
            }
        }
        default: {
            const char *start = m_p;
            if (*m_p == '-')
                m_p++;
            while (m_p < m_end && ((*m_p >= '0' && *m_p <= '9') || *m_p == '.' || *m_p == 'e' ||
                                   *m_p == 'E' || *m_p == '+' || *m_p == '-'))
                m_p++;
            if (m_p == start || (m_p == start + 1 && *start == '-'))
                return fail("unexpected character");
            out.m_type = Type::Number;
            out.m_text.assign(start, m_p - start);
            return true;
        }
        }
    }
};

bool parse(std::string_view text, Value &out, std::string *error)
{
    out = Value();
    Parser p(text);
    return p.parseDocument(out, error);
}

}  // namespace json
