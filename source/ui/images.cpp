// SPDX-License-Identifier: MIT
#include "ui/images.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include <png.h>

#include "discord/cdn.h"
#include "discord/rest.h"
#include "net/http.h"
#include "util/util.h"

static constexpr const char *kCacheDir = "sdmc:/switch/OunceCord/cache";

// Decodes a PNG into RGBA8.
static bool decodePng(const std::string &data, std::vector<uint8_t> &out, int &w, int &h)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data.data(), data.size()))
        return false;
    image.format = PNG_FORMAT_RGBA;
    // Refuse absurd sizes; we only ask the CDN for small images.
    if (image.width == 0 || image.height == 0 || image.width > 1024 || image.height > 1024) {
        png_image_free(&image);
        return false;
    }
    out.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, out.data(), 0, nullptr)) {
        png_image_free(&image);
        return false;
    }
    w = (int)image.width;
    h = (int)image.height;
    return true;
}

static bool readFile(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && size < (8 << 20);
    if (ok) {
        out.resize(size);
        ok = fread(out.data(), 1, size, f) == (size_t)size;
    }
    fclose(f);
    return ok;
}

std::string ImageCache::diskPath(const std::string &path)
{
    // FNV-1a over the CDN path, which includes the content hash and size.
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : path) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char name[32];
    snprintf(name, sizeof(name), "/%016llx.png", (unsigned long long)h);
    return std::string(kCacheDir) + name;
}

bool ImageCache::init()
{
    mkdir(kCacheDir, 0777);
    // Keep the disk cache bounded: when it grows past the cap, start over.
    std::vector<std::string> files;
    if (DIR *dir = opendir(kCacheDir)) {
        while (struct dirent *e = readdir(dir))
            if (e->d_name[0] != '.')
                files.push_back(e->d_name);
        closedir(dir);
    }
    if (files.size() > kMaxDiskFiles) {
        logf("images: disk cache has %zu files, clearing", files.size());
        for (const auto &f : files)
            remove((std::string(kCacheDir) + "/" + f).c_str());
    }

    m_running = true;
    for (int i = 0; i < kWorkers; i++)
        m_threads.emplace_back([this] { worker(); });
    return true;
}

void ImageCache::exit()
{
    m_running = false;
    m_cv.notify_all();
    for (auto &t : m_threads)
        if (t.joinable())
            t.join();
    m_threads.clear();
    for (auto &e : m_entries)
        if (e.second.tex)
            glDeleteTextures(1, &e.second.tex);
    m_entries.clear();
    m_textureCount = 0;
}

GLuint ImageCache::get(const std::string &path)
{
    if (path.empty())
        return 0;
    uint64_t now = nowMs();
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(path);
    if (it == m_entries.end()) {
        Entry e;
        e.last_used = now;
        m_entries.emplace(path, std::move(e));
        m_cv.notify_one();
        return 0;
    }
    Entry &e = it->second;
    e.last_used = now;
    if (e.state == State::Ready)
        return e.tex;
    if (e.state == State::Failed && now - e.failed_at > kFailRetryMs) {
        e.state = State::Queued;
        m_cv.notify_one();
    }
    return 0;
}

void ImageCache::update()
{
    std::vector<std::pair<std::string, Entry *>> ready;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t now = nowMs();
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            Entry &e = it->second;
            // Queued work nobody looked at lately: forget it, so scrolling
            // past a thousand avatars does not download them all.
            if (e.state == State::Queued && now - e.last_used > kStaleMs) {
                it = m_entries.erase(it);
                continue;
            }
            if (e.state == State::Decoded && (int)ready.size() < kUploadsPerFrame)
                ready.emplace_back(it->first, &e);
            ++it;
        }
        // Upload under the lock; entries are only erased on this thread.
        for (auto &r : ready) {
            Entry &e = *r.second;
            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, e.width, e.height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, e.rgba.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            e.tex = tex;
            e.state = State::Ready;
            e.rgba.clear();
            e.rgba.shrink_to_fit();
            m_textureCount++;
        }
    }
    if (m_textureCount > kMaxTextures)
        evict();
}

void ImageCache::evict()
{
    // Drop the least recently used textures down to 80% of the cap.
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<uint64_t, std::string>> ready;
    for (auto &e : m_entries)
        if (e.second.state == State::Ready)
            ready.emplace_back(e.second.last_used, e.first);
    std::sort(ready.begin(), ready.end());
    size_t target = kMaxTextures * 4 / 5;
    for (size_t i = 0; i < ready.size() && m_textureCount > target; i++) {
        auto it = m_entries.find(ready[i].second);
        glDeleteTextures(1, &it->second.tex);
        m_entries.erase(it);
        m_textureCount--;
    }
}

bool ImageCache::takeJob(std::string &path, uint64_t now, uint64_t &wait_ms)
{
    wait_ms = 0;
    Entry *best = nullptr;
    const std::string *best_path = nullptr;
    for (auto &e : m_entries) {
        if (e.second.state != State::Queued || now - e.second.last_used > kStaleMs)
            continue;
        if (!best || e.second.last_used > best->last_used) {
            best = &e.second;
            best_path = &e.first;
        }
    }
    if (!best)
        return false;
    path = *best_path;
    best->state = State::Loading;
    return true;
}

void ImageCache::worker()
{
    HttpClient http(discord::cdn::kHost);
    const HttpHeaders headers = {
        {"User-Agent", discord::Identity::get().userAgent()},
        {"Accept", "image/png,image/*"},
    };
    while (m_running) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            uint64_t wait = 0;
            if (!takeJob(path, nowMs(), wait)) {
                m_cv.wait_for(lock, std::chrono::milliseconds(500));
                continue;
            }
        }

        std::string data, file = diskPath(path);
        bool from_disk = readFile(file, data);
        if (!from_disk) {
            // Pace downloads across workers, and honour a CDN 429.
            while (m_running) {
                std::unique_lock<std::mutex> lock(m_mutex);
                uint64_t now = nowMs();
                while (!m_recentDownloads.empty() && now - m_recentDownloads.front() >= 1000)
                    m_recentDownloads.pop_front();
                uint64_t ready_at = m_pausedUntil;
                if (m_recentDownloads.size() >= (size_t)kMaxDownloadsPerSecond)
                    ready_at = std::max(ready_at, m_recentDownloads.front() + 1000);
                if (ready_at <= now) {
                    m_recentDownloads.push_back(now);
                    break;
                }
                m_cv.wait_for(lock, std::chrono::milliseconds(std::min<uint64_t>(ready_at - now, 500)));
            }
            if (!m_running)
                break;

            HttpResponse res = http.request("GET", path, headers);
            if (res.status == 429) {
                double after = 5;
                if (const std::string *v = res.header("retry-after"))
                    after = std::max(1.0, strtod(v->c_str(), nullptr));
                logf("images: CDN 429, pausing %.1fs", after);
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pausedUntil = nowMs() + (uint64_t)(after * 1000);
                auto it = m_entries.find(path);
                if (it != m_entries.end())
                    it->second.state = State::Queued;  // try again after the pause
                continue;
            }
            if (res.status != 200) {
                if (res.status != 404)
                    logf("images: %s -> %d %s", path.c_str(), res.status, res.error.c_str());
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_entries.find(path);
                if (it != m_entries.end()) {
                    it->second.state = State::Failed;
                    // Network errors retry after 30s; HTTP errors after 5 min.
                    it->second.failed_at = nowMs() - (res.status == 0 ? kFailRetryMs - 30000 : 0);
                }
                continue;
            }
            data = std::move(res.body);
        }

        std::vector<uint8_t> rgba;
        int w = 0, h = 0;
        bool ok = decodePng(data, rgba, w, h);
        if (ok && !from_disk) {
            if (FILE *f = fopen(file.c_str(), "wb")) {
                fwrite(data.data(), 1, data.size(), f);
                fclose(f);
            }
        } else if (!ok && from_disk) {
            remove(file.c_str());  // corrupt cache file
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(path);
        if (it == m_entries.end())
            continue;
        if (ok) {
            it->second.rgba = std::move(rgba);
            it->second.width = w;
            it->second.height = h;
            it->second.state = State::Decoded;
        } else {
            logf("images: could not decode %s", path.c_str());
            it->second.state = State::Failed;
            it->second.failed_at = nowMs();
        }
    }
    http.close();
}
