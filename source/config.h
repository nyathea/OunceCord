// SPDX-License-Identifier: MIT
//
// Files under sdmc:/switch/OunceCord/.
#pragma once

#include <string>

namespace config {

constexpr const char *kDir = "sdmc:/switch/OunceCord";
constexpr const char *kTokenPath = "sdmc:/switch/OunceCord/token.txt";
constexpr const char *kLogPath = "sdmc:/switch/OunceCord/log.txt";

void ensureDir();
// The saved token, trimmed, or empty. Accepts a token with or without
// surrounding quotes, as people often copy it from devtools that way.
std::string loadToken();
bool saveToken(const std::string &token);
void clearToken();
// Cleans up a pasted/typed token: strips whitespace and quotes.
std::string normalizeToken(std::string token);

}  // namespace config
