// SPDX-License-Identifier: MIT
//
// A small JSON DOM: parse, query and serialize. Built for Discord payloads,
// so it is forgiving on reads (missing keys yield a null value) and numbers
// keep their source text, which matters for 64-bit snowflakes.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json {

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() = default;
    static Value boolean(bool b);
    static Value number(double d);
    static Value number(int64_t i);
    static Value string(std::string s);
    static Value array();
    static Value object();

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    // Object lookup. Returns a shared null value when absent or not an object.
    const Value &operator[](std::string_view key) const;
    // Array index. Returns a shared null value when out of range.
    const Value &operator[](size_t index) const;
    bool has(std::string_view key) const;

    size_t size() const;
    const std::vector<Value> &items() const { return m_array; }
    const std::vector<std::pair<std::string, Value>> &members() const { return m_object; }

    // Conversions with fallbacks.
    bool asBool(bool fallback = false) const;
    double asDouble(double fallback = 0) const;
    int64_t asInt(int64_t fallback = 0) const;
    // Snowflakes arrive as strings; numbers are accepted as well.
    uint64_t asId() const;
    const std::string &asString() const;  // empty string unless String
    std::string str(std::string_view fallback = "") const;

    // Builders.
    Value &push(Value v);
    Value &set(std::string key, Value v);

    std::string dump() const;

private:
    friend class Parser;
    Type m_type = Type::Null;
    bool m_bool = false;
    std::string m_text;  // string contents, or number source text
    std::vector<Value> m_array;
    std::vector<std::pair<std::string, Value>> m_object;

    void dumpTo(std::string &out) const;
};

// Parses a document. On failure returns false and sets error (if given).
bool parse(std::string_view text, Value &out, std::string *error = nullptr);

// Escapes a string as a JSON string literal, quotes included.
void escapeTo(std::string &out, std::string_view s);

}  // namespace json
