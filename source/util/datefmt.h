// SPDX-License-Identifier: MIT
//
// Date order for numeric and written-out dates (a setting). Platform-free.
#pragma once

#include <cstdio>
#include <string>

enum class DateFormat { YMD, MDY, DMY };

// "2025-03-01", "03-01-2025" or "01-03-2025".
inline std::string numericDate(int y, int m, int d, DateFormat f)
{
    char buf[24];
    switch (f) {
    case DateFormat::MDY: snprintf(buf, sizeof(buf), "%02d-%02d-%04d", m, d, y); break;
    case DateFormat::DMY: snprintf(buf, sizeof(buf), "%02d-%02d-%04d", d, m, y); break;
    default: snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d); break;
    }
    return buf;
}

// "March 1, 2025" (month first) or "1 March 2025"; short_month: "Mar".
inline std::string longDate(int y, int m, int d, DateFormat f, bool short_month = false)
{
    static const char *kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};
    std::string month = kMonths[(m - 1 + 12) % 12];
    if (short_month)
        month = month.substr(0, 3);
    char buf[48];
    if (f == DateFormat::MDY)
        snprintf(buf, sizeof(buf), "%s %d, %d", month.c_str(), d, y);
    else
        snprintf(buf, sizeof(buf), "%d %s %d", d, month.c_str(), y);
    return buf;
}
