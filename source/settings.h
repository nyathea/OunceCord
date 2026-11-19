// SPDX-License-Identifier: MIT
//
// What the user picked in the settings pane, kept in settings.json.
// Platform-free.
#pragma once

#include <string>

enum class Theme { Dark, Light, Amoled };

struct Settings {
    Theme theme = Theme::Dark;
    bool mouse = true;     // the Joy-Con 2 mouse mode
    bool touch = true;     // the touch screen
    bool qr_login = false; // experimental: log in by scanning a QR code
    bool videos = false;   // experimental: play videos in chat, send album clips

    static const char *themeName(Theme t);  // "Dark", "Light", "AMOLED"

    std::string toJson() const;
    // Unknown or missing fields keep their defaults.
    static Settings fromJson(const std::string &text);

    static Settings load();
    bool save() const;
};
