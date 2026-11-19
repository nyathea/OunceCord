// SPDX-License-Identifier: MIT
#include "util/sysinfo.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <switch.h>

#include "util/util.h"

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

static SystemInfo detect()
{
    SystemInfo info;
    info.app = APP_VERSION;

    // The real system version and platform name come from set:sys. (On
    // Switch 2, hosversionGet() is pinned to 20.0.0.)
    SetSysFirmwareVersion fw{};
    bool have_fw = false;
    if (R_SUCCEEDED(setsysInitialize())) {
        have_fw = R_SUCCEEDED(setsysGetFirmwareVersion(&fw));
        setsysExit();
    }
    if (have_fw) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u.%u.%u", fw.major, fw.minor, fw.micro);
        info.hos = buf;
        std::string p(fw.platform, strnlen(fw.platform, sizeof(fw.platform)));
        std::string lower;
        for (char c : p)
            lower.push_back((char)tolower((unsigned char)c));
        if (lower.find("ounce") != std::string::npos)
            info.platform = "Ounce";
        else if (lower == "nx")
            info.platform = "NX";
        else
            info.platform = p;
    } else if (u32 v = hosversionGet()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u.%u.%u", HOSVER_MAJOR(v), HOSVER_MINOR(v), HOSVER_MICRO(v));
        info.hos = buf;
    }
    if (info.hos.empty())
        info.hos = "Unknown";
    if (info.platform.empty()) {
#if defined(__SWITCH2__)
        info.platform = "Ounce";  // what this build targets
#else
        info.platform = "NX";
#endif
    }

    // kaiHEN's loader marks itself the way Atmosphère's does, so check it first.
    if (hosversionIsKaiHEN()) {
        info.environment = "kaiHEN";
    } else if (hosversionIsAtmosphere()) {
        info.environment = "Atmosph\xC3\xA8re";
        // Its version, from exosphère's config item (65000).
        u64 ams = 0;
        if (R_SUCCEEDED(splInitialize())) {
            if (R_SUCCEEDED(splGetConfig((SplConfigItem)65000, &ams))) {
                char buf[32];
                snprintf(buf, sizeof(buf), " %u.%u.%u", (unsigned)((ams >> 56) & 0xFF), (unsigned)((ams >> 48) & 0xFF),
                         (unsigned)((ams >> 40) & 0xFF));
                info.environment += buf;
            }
            splExit();
        }
    } else {
        info.environment = "Unknown";
    }
    logf("system: HOS %s, platform %s (reported \"%.32s\"), app %s, environment %s", info.hos.c_str(),
         info.platform.c_str(), have_fw ? fw.platform : "", info.app.c_str(), info.environment.c_str());
    return info;
}

const SystemInfo &SystemInfo::get()
{
    static SystemInfo info = detect();
    return info;
}
