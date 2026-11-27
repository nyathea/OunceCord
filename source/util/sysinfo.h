// SPDX-License-Identifier: MIT
//
// What the app is running on, for the settings pane: the system version,
// the platform (Ounce = Switch 2, NX = Switch 1), the app version and the
// homebrew environment (kaiHEN or Atmosphère).
#pragma once

#include <string>

struct SystemInfo {
    std::string hos;          // "20.1.5", or "Unknown"
    std::string platform;     // "Ounce", "NX", or what the system reports
    std::string app;          // "0.1.0"
    std::string build_date;   // "2026-11-23 17:05", when this copy was built
    std::string commit;       // git commit it was built from, "8425187 (modified)" with uncommitted changes
    std::string environment;  // "kaiHEN", "Atmosphère 1.9.0", or "Unknown"

    // Reads it once; later calls return the same.
    static const SystemInfo &get();
};
