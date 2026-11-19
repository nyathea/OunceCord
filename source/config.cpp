// SPDX-License-Identifier: MIT
#include "config.h"

#include <cstdio>
#include <sys/stat.h>

namespace config {

void ensureDir()
{
    mkdir("sdmc:/switch", 0777);
    mkdir(kDir, 0777);
}

std::string normalizeToken(std::string t)
{
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!t.empty() && (isSpace(t.back()) || t.back() == '"' || t.back() == '\''))
        t.pop_back();
    size_t start = 0;
    while (start < t.size() && (isSpace(t[start]) || t[start] == '"' || t[start] == '\''))
        start++;
    t.erase(0, start);
    // Tokens contain no whitespace; drop any that slipped in mid-string.
    std::string out;
    for (char c : t)
        if (!isSpace(c))
            out.push_back(c);
    return out;
}

std::string loadToken()
{
    FILE *f = fopen(kTokenPath, "r");
    if (!f)
        return {};
    char buf[512] = {};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    return normalizeToken(std::string(buf, n));
}

bool saveToken(const std::string &token)
{
    ensureDir();
    FILE *f = fopen(kTokenPath, "w");
    if (!f)
        return false;
    bool ok = fwrite(token.data(), 1, token.size(), f) == token.size();
    fclose(f);
    return ok;
}

bool readText(const char *path, std::string &out, size_t max_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = size >= 0 && (size_t)size <= max_size;
    if (ok) {
        out.resize(size);
        ok = fread(out.data(), 1, size, f) == (size_t)size;
    }
    fclose(f);
    return ok;
}

bool writeText(const char *path, const std::string &data)
{
    ensureDir();
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

void clearToken()
{
    remove(kTokenPath);
}

}  // namespace config
