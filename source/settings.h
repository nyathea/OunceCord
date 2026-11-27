// SPDX-License-Identifier: MIT
//
// What the user picked in the settings pane, kept in settings.json.
// Platform-free.
#pragma once

#include <string>

#include "util/datefmt.h"

enum class Theme { Dark, Light, Amoled };

struct Settings {
    Theme theme = Theme::Dark;
    bool mouse = true;     // the Joy-Con 2 mouse mode
    bool touch = true;     // the touch screen
    bool qr_login = false; // experimental: log in by scanning a QR code
    bool videos = false;   // experimental: play videos in chat, send album clips
    bool clock24 = true;   // 24-hour times (else 12-hour with AM/PM)
    DateFormat date_format = DateFormat::YMD;
    // Kept between launches: the status picked in the user panel ("online",
    // "idle", "dnd", "invisible"; empty until one is picked, which keeps the
    // account's own) and the mute and deafen buttons.
    std::string status;
    bool self_mute = false;
    bool self_deaf = false;

    static const char *themeName(Theme t);  // "Dark", "Light", "AMOLED"
    static const char *dateFormatName(DateFormat f);  // "YYYY-MM-DD"...

    std::string toJson() const;
    // Unknown or missing fields keep their defaults.
    static Settings fromJson(const std::string &text);

    static Settings load();
    bool save() const;
};
