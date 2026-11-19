// SPDX-License-Identifier: MIT
#include "util/util.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#include <switch.h>

static FILE *g_logFile;
static std::mutex g_logMutex;

void logInit(const char *path)
{
    g_logFile = fopen(path, "w");
}

void logExit()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile)
        fclose(g_logFile);
    g_logFile = nullptr;
}

void logf(const char *fmt, ...)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile)
        return;
    fprintf(g_logFile, "[%8.3f] ", (double)nowMs() / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fputc('\n', g_logFile);
    fflush(g_logFile);
}

std::string base64Encode(const void *data, size_t size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *p = (const uint8_t *)data;
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < size; i += 3) {
        uint32_t v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
        out.push_back(table[(v >> 18) & 63]);
        out.push_back(table[(v >> 12) & 63]);
        out.push_back(table[(v >> 6) & 63]);
        out.push_back(table[v & 63]);
    }
    if (i < size) {
        uint32_t v = p[i] << 16;
        if (i + 1 < size)
            v |= p[i + 1] << 8;
        out.push_back(table[(v >> 18) & 63]);
        out.push_back(table[(v >> 12) & 63]);
        out.push_back(i + 1 < size ? table[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

uint32_t utf8Next(std::string_view s, size_t &i)
{
    uint8_t c = (uint8_t)s[i++];
    if (c < 0x80)
        return c;
    int extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        cp = c & 0x07;
    } else {
        return 0xFFFD;
    }
    for (int k = 0; k < extra; k++) {
        if (i >= s.size() || ((uint8_t)s[i] & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | ((uint8_t)s[i++] & 0x3F);
    }
    return cp;
}

void randomBytes(void *buf, size_t n)
{
    randomGet(buf, n);
}

std::string randomUuid()
{
    uint8_t b[16];
    randomBytes(b, sizeof(b));
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    char out[37];
    snprintf(out, sizeof(out),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12],
             b[13], b[14], b[15]);
    return out;
}

uint64_t nowMs()
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

std::string formatTime(uint64_t unix_seconds)
{
    TimeCalendarTime ct{}, today{};
    TimeCalendarAdditionalInfo info{};
    char buf[32];
    if (R_FAILED(timeToCalendarTimeWithMyRule(unix_seconds, &ct, &info))) {
        time_t t = (time_t)unix_seconds;
        struct tm *tm = gmtime(&t);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
        return buf;
    }
    u64 now = 0;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &now)))
        timeToCalendarTimeWithMyRule(now, &today, &info);
    if (ct.year == today.year && ct.month == today.month && ct.day == today.day)
        snprintf(buf, sizeof(buf), "%02d:%02d", ct.hour, ct.minute);
    else
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", ct.year, ct.month, ct.day,
                 ct.hour, ct.minute);
    return buf;
}

bool localTime(int64_t unix_seconds, LocalTime &out)
{
    TimeCalendarTime ct{};
    TimeCalendarAdditionalInfo info{};
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule((u64)unix_seconds, &ct, &info))) {
        out = {ct.year, ct.month, ct.day, ct.hour, ct.minute, ct.second, (int)info.wday};
        return true;
    }
    time_t t = (time_t)unix_seconds;
    struct tm *tm = gmtime(&t);
    if (!tm)
        return false;
    out = {tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_wday};
    return true;
}

int64_t unixNow()
{
    u64 now = 0;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_Default, &now)))
        return (int64_t)now;
    return (int64_t)time(nullptr);
}

std::string urlEncode(std::string_view s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}
