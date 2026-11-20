// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Logs to sdmc:/switch/OunceCord/log.txt (thread-safe). Never logs tokens.
void logInit(const char *path);
void logExit();
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

std::string base64Encode(const void *data, size_t size);
inline std::string base64Encode(std::string_view s) { return base64Encode(s.data(), s.size()); }

// Decodes one UTF-8 code point at s[i], advancing i. Invalid bytes yield U+FFFD.
uint32_t utf8Next(std::string_view s, size_t &i);

// Fills buf with n random bytes (csrng).
void randomBytes(void *buf, size_t n);
std::string randomUuid();

// Milliseconds since an arbitrary epoch (monotonic).
uint64_t nowMs();

// Discord epoch helpers.
inline uint64_t snowflakeUnixMs(uint64_t id) { return (id >> 22) + 1420070400000ULL; }
// Formats a unix timestamp (seconds) in the console's time zone,
// as "HH:MM" today or "YYYY-MM-DD HH:MM" otherwise.
#include "util/datefmt.h"

// "14:05" today, else "2025-03-01 14:05" (in the date order); with clock24 false, "2:05 PM".
std::string formatTime(uint64_t unix_seconds, bool clock24 = true, DateFormat date = DateFormat::YMD);
// The time of day: "14:05" or "2:05 PM" (with seconds: "14:05:09" / "2:05:09 PM").
std::string clockText(int hour, int minute, bool clock24, int second = -1);
// Wall-clock time in the console's time zone.
struct LocalTime {
    int year, month, day, hour, minute, second, weekday;  // month 1-12, weekday 0 = Sunday
};
bool localTime(int64_t unix_seconds, LocalTime &out);
int64_t unixNow();

// Percent-encodes a string for use in a URL query.
std::string urlEncode(std::string_view s);
