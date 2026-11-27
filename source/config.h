// SPDX-License-Identifier: MIT
//
// Files under sdmc:/switch/OunceCord/.
#pragma once

#include <string>

namespace config {

constexpr const char *kDir = "sdmc:/switch/OunceCord";
constexpr const char *kTokenPath = "sdmc:/switch/OunceCord/token.txt";
constexpr const char *kLogPath = "sdmc:/switch/OunceCord/log.txt";
// Discord's tracking fingerprint for this install, sent with login requests.
constexpr const char *kFingerprintPath = "sdmc:/switch/OunceCord/fingerprint.txt";
// Cookies discord.com set (like a browser keeps them), one name=value per line.
constexpr const char *kCookiesPath = "sdmc:/switch/OunceCord/cookies.txt";
// Optional overrides for the client identity (build numbers, versions).
constexpr const char *kIdentityPath = "sdmc:/switch/OunceCord/identity.json";
// Pictures saved from chat, which can also be sent from the picker.
constexpr const char *kSavedDir = "sdmc:/switch/OunceCord/Saved";
// Settings chosen in the app.
constexpr const char *kSettingsPath = "sdmc:/switch/OunceCord/settings.json";
// The server and channel open when the app last ran, reopened on launch.
constexpr const char *kLastPlacePath = "sdmc:/switch/OunceCord/last.json";

void ensureDir();
// The saved token, trimmed, or empty. Accepts a token with or without
// surrounding quotes, as people often copy it from devtools that way.
std::string loadToken();
bool saveToken(const std::string &token);
void clearToken();
// Whole-file helpers. readText fails on files over max_size.
bool readText(const char *path, std::string &out, size_t max_size = 64 * 1024);
bool writeText(const char *path, const std::string &data);

// Cleans up a pasted/typed token: strips whitespace and quotes.
std::string normalizeToken(std::string token);

}  // namespace config
