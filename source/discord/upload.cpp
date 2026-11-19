// SPDX-License-Identifier: MIT
#include "discord/upload.h"

namespace discord {

std::string multipartBoundary(const unsigned char random[16])
{
    static const char hex[] = "0123456789abcdef";
    std::string b = "----OunceCord";
    for (int i = 0; i < 16; i++) {
        b.push_back(hex[random[i] >> 4]);
        b.push_back(hex[random[i] & 15]);
    }
    return b;
}

std::string safeFilename(const std::string &name)
{
    std::string out;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                  c == '_' || c == '.';
        out.push_back(ok ? c : '_');
    }
    if (out.empty() || out[0] == '.')
        out = "file" + out;
    if (out.size() > 100)
        out = out.substr(out.size() - 100);
    return out;
}

std::string multipartBody(const std::string &boundary, const std::string &payload_json, const Upload &upload)
{
    std::string b;
    b.reserve(upload.data.size() + payload_json.size() + 512);
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"payload_json\"\r\n";
    b += "Content-Type: application/json\r\n\r\n";
    b += payload_json;
    b += "\r\n--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"files[0]\"; filename=\"" + safeFilename(upload.filename) + "\"\r\n";
    b += "Content-Type: " + upload.content_type + "\r\n\r\n";
    b += upload.data;
    b += "\r\n--" + boundary + "--\r\n";
    return b;
}

}  // namespace discord
