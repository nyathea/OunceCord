// SPDX-License-Identifier: MIT
#include "ui/images.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

#include <map>
#include <memory>

#include <png.h>
#include <turbojpeg.h>

#include "discord/cdn.h"
#include "discord/rest.h"
#include "net/http.h"
#include "util/imageutil.h"
#include "util/util.h"

static constexpr const char *kCacheDir = "sdmc:/switch/OunceCord/cache";

static bool decodeJpeg(const std::string &data, std::vector<uint8_t> &out, int &w, int &h)
{
    tjhandle tj = tjInitDecompress();
    if (!tj)
        return false;
    int subsamp = 0, colorspace = 0;
    bool ok = tjDecompressHeader3(tj, (const unsigned char *)data.data(), (unsigned long)data.size(), &w, &h,
                                  &subsamp, &colorspace) == 0 &&
              w > 0 && h > 0 && w <= ImageCache::kMaxImageSide && h <= ImageCache::kMaxImageSide;
    if (ok) {
        out.resize((size_t)w * h * 4);
        ok = tjDecompress2(tj, (const unsigned char *)data.data(), (unsigned long)data.size(), out.data(), w, 0, h,
                           TJPF_RGBA, TJFLAG_ACCURATEDCT) == 0;
    }
    tjDestroy(tj);
    return ok;
}

// A big JPEG decoded straight to a smaller size (libjpeg-turbo scales by
// 1/2, 1/4 or 1/8 while decoding), for thumbnails.
static bool decodeJpegSmall(const std::string &data, std::vector<uint8_t> &out, int &w, int &h, int max_side)
{
    tjhandle tj = tjInitDecompress();
    if (!tj)
        return false;
    int subsamp = 0, colorspace = 0, fw = 0, fh = 0;
    bool ok = tjDecompressHeader3(tj, (const unsigned char *)data.data(), (unsigned long)data.size(), &fw, &fh,
                                  &subsamp, &colorspace) == 0 &&
              fw > 0 && fh > 0 && fw <= 16384 && fh <= 16384;
    if (ok) {
        int scale = 1;
        while (scale < 8 && (fw / scale > max_side || fh / scale > max_side))
            scale *= 2;
        w = TJSCALED(fw, (tjscalingfactor{1, scale}));
        h = TJSCALED(fh, (tjscalingfactor{1, scale}));
        out.resize((size_t)w * h * 4);
        ok = tjDecompress2(tj, (const unsigned char *)data.data(), (unsigned long)data.size(), out.data(), w, 0, h,
                           TJPF_RGBA, TJFLAG_FASTDCT) == 0;
    }
    tjDestroy(tj);
    return ok;
}

// Decodes a PNG into RGBA8.
static bool decodePng(const std::string &data, std::vector<uint8_t> &out, int &w, int &h)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data.data(), data.size()))
        return false;
    image.format = PNG_FORMAT_RGBA;
    // Refuse absurd sizes; we always ask for a bounded size.
    if (image.width == 0 || image.height == 0 || image.width > (unsigned)ImageCache::kMaxImageSide ||
        image.height > (unsigned)ImageCache::kMaxImageSide) {
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

// PNG or JPEG, by content. Anything else (GIF, WebP) fails.
static bool decodeImage(const std::string &data, std::vector<uint8_t> &out, int &w, int &h)
{
    if (data.size() > 8 && memcmp(data.data(), "\x89PNG", 4) == 0)
        return decodePng(data, out, w, h);
    if (data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8)
        return decodeJpeg(data, out, w, h);
    return false;
}

// Decodes a still (PNG, JPEG) or animated (GIF) image into frames.
static bool decodeAny(const std::string &data, std::vector<gif::Frame> &frames, int &w, int &h)
{
    frames.clear();
    if (data.size() > 6 && memcmp(data.data(), "GIF8", 4) == 0) {
        gif::Image img;
        if (!gif::decode(data, img, ImageCache::kMaxAnimationBytes, ImageCache::kMaxImageSide))
            return false;
        w = img.width;
        h = img.height;
        frames = std::move(img.frames);
        return true;
    }
    gif::Frame f;
    if (!decodeImage(data, f.rgba, w, h))
        return false;
    f.delay_ms = 0;
    frames.push_back(std::move(f));
    return true;
}

// What a file really is, for the log ("webp", "gif", ...).
static std::string sniffFormat(const std::string &d)
{
    auto starts = [&d](const char *magic, size_t n, size_t at = 0) {
        return d.size() >= at + n && memcmp(d.data() + at, magic, n) == 0;
    };
    if (starts("\x89PNG", 4))
        return "png";
    if (starts("\xFF\xD8", 2))
        return "jpeg";
    if (starts("RIFF", 4) && starts("WEBP", 4, 8))
        return "webp";
    if (starts("GIF8", 4))
        return "gif";
    if (starts("ftypavif", 8, 4) || starts("ftypavis", 8, 4))
        return "avif";
    if (starts("ftyp", 4, 4))
        return "heif/mp4";
    if (starts("<", 1))
        return "html";
    return "unknown";
}

static bool readFile(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && size < (16 << 20);
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
    // Full URLs are hashed without their rotating signature parameters.
    std::string key = path[0] == '/' ? path : discord::cdn::imageCacheKey(path);
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char name[32];
    snprintf(name, sizeof(name), "/%016llx.%s", (unsigned long long)h, path[0] == '/' ? "png" : "img");
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
        if (!e.second.texs.empty())
            glDeleteTextures((GLsizei)e.second.texs.size(), e.second.texs.data());
    m_entries.clear();
    m_textureCount = 0;
    m_textureBytes = 0;
}

GLuint ImageCache::get(const std::string &path, bool *failed)
{
    if (failed)
        *failed = false;
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
    if (!e.texs.empty()) {
        // Animations play once every frame is uploaded; until then, and for
        // still images, show the first frame.
        if (e.state != State::Ready || e.texs.size() == 1 || e.total_ms <= 0)
            return e.texs[0];
        int t = (int)((now - e.ready_at) % (uint64_t)e.total_ms);
        for (size_t i = 0; i < e.texs.size(); i++) {
            if (t < e.delays[i])
                return e.texs[i];
            t -= e.delays[i];
        }
        return e.texs.back();
    }
    if (failed)
        *failed = e.state == State::Failed;
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
        // Big pictures and long animations take a while to upload, so the
        // work is spread over frames (about 4 MB per frame).
        size_t uploaded = 0;
        for (auto &r : ready) {
            Entry &e = *r.second;
            bool animated = e.pending.size() > 1;
            while (e.next_upload < e.pending.size() && uploaded <= (4u << 20)) {
                gif::Frame &f = e.pending[e.next_upload++];
                uploaded += f.rgba.size();
                GLuint tex;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, e.width, e.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             f.rgba.data());
                // Mipmaps for stills (they're often drawn smaller); animation
                // frames skip them to save memory and upload time.
                if (!animated)
                    glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, animated ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (e.texs.empty())
                    m_textureCount++;
                e.texs.push_back(tex);
                e.delays.push_back(f.delay_ms);
                size_t bytes = (size_t)e.width * e.height * 4;
                if (!animated)
                    bytes = bytes * 4 / 3;  // mipmaps
                e.bytes += bytes;
                m_textureBytes += bytes;
                std::vector<uint8_t>().swap(f.rgba);
            }
            if (e.next_upload >= e.pending.size()) {
                e.state = State::Ready;
                e.total_ms = 0;
                for (int d : e.delays)
                    e.total_ms += d;
                e.ready_at = now;
                std::vector<gif::Frame>().swap(e.pending);
                e.next_upload = 0;
            }
            if (uploaded > (4u << 20))
                break;
        }
    }
    if (m_textureCount > kMaxTextures || m_textureBytes > kMaxTextureBytes)
        evict();
}

void ImageCache::evict()
{
    // Drop the least recently used textures down to 80% of the caps,
    // sparing anything drawn in the last second (it is on screen).
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t now = nowMs();
    std::vector<std::pair<uint64_t, std::string>> ready;
    for (auto &e : m_entries)
        if (!e.second.texs.empty())
            ready.emplace_back(e.second.last_used, e.first);
    std::sort(ready.begin(), ready.end());
    size_t target = kMaxTextures * 4 / 5, target_bytes = kMaxTextureBytes * 4 / 5;
    for (size_t i = 0; i < ready.size() && (m_textureCount > target || m_textureBytes > target_bytes); i++) {
        if (now - ready[i].first < 1000)
            break;
        auto it = m_entries.find(ready[i].second);
        glDeleteTextures((GLsizei)it->second.texs.size(), it->second.texs.data());
        m_textureBytes -= it->second.bytes;
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
    // One keep-alive connection per image host.
    std::map<std::string, std::unique_ptr<HttpClient>> clients;
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

        if (path.compare(0, 6, "local:") == 0) {
            std::string data;
            std::vector<gif::Frame> frames;
            int w = 0, h = 0;
            bool ok = m_localLoader && m_localLoader(path, data) && decodeAny(data, frames, w, h);
            if (!ok && !data.empty() && imageutil::sniffExtension(data) == "jpg") {
                gif::Frame f;
                f.delay_ms = 0;
                ok = decodeJpegSmall(data, f.rgba, w, h, kLocalThumbSide * 2);
                frames.clear();
                if (ok)
                    frames.push_back(std::move(f));
            }
            if (ok) {
                // Local images are thumbnails: a still, kept small.
                frames.resize(1);
                frames[0].delay_ms = 0;
                imageutil::shrink(frames[0].rgba, w, h, kLocalThumbSide);
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_entries.find(path);
            if (it == m_entries.end())
                continue;
            if (ok) {
                it->second.pending = std::move(frames);
                it->second.next_upload = 0;
                it->second.width = w;
                it->second.height = h;
                it->second.state = State::Decoded;
            } else {
                it->second.state = State::Failed;
                it->second.failed_at = nowMs();
            }
            continue;
        }
        std::string host = discord::cdn::kHost, req_path = path;
        if (path[0] != '/' && !discord::cdn::splitImageUrl(path, host, req_path)) {
            logf("images: refusing %s (not a Discord image host)", discord::cdn::imageCacheKey(path).c_str());
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_entries.find(path);
            if (it != m_entries.end()) {
                it->second.state = State::Failed;
                it->second.failed_at = nowMs();
            }
            continue;
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

            std::unique_ptr<HttpClient> &http = clients[host];
            if (!http)
                http = std::make_unique<HttpClient>(host);
            HttpResponse res = http->request("GET", req_path, headers);
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
                    logf("images: %s -> %d %s", discord::cdn::imageCacheKey(path).c_str(), res.status,
                         res.error.c_str());
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

        std::vector<gif::Frame> frames;
        int w = 0, h = 0;
        bool ok = decodeAny(data, frames, w, h);
        // The file may not be what its name says (a WebP saved as .png is
        // common), and the proxy then passes the original bytes through.
        // Ask the proxy to convert it to PNG instead, once.
        if (!ok && !from_disk && path[0] != '/' && req_path.find("format=") == std::string::npos) {
            std::string what = sniffFormat(data);
            std::string alt = req_path + (req_path.find('?') == std::string::npos ? "?" : "&") + "format=png";
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_recentDownloads.push_back(nowMs());
            }
            std::unique_ptr<HttpClient> &http = clients[host];
            HttpResponse res = http->request("GET", alt, headers);
            if (res.status == 200) {
                data = std::move(res.body);
                ok = decodeAny(data, frames, w, h);
            }
            logf("images: %s was %s, as PNG: %s", discord::cdn::imageCacheKey(path).c_str(), what.c_str(),
                 ok ? "ok" : res.status == 200 ? sniffFormat(data).c_str() : std::to_string(res.status).c_str());
        }
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
            it->second.pending = std::move(frames);
            it->second.next_upload = 0;
            it->second.width = w;
            it->second.height = h;
            it->second.state = State::Decoded;
        } else {
            logf("images: could not decode %s (%s, %zu bytes)", discord::cdn::imageCacheKey(path).c_str(),
                 sniffFormat(data).c_str(), data.size());
            it->second.state = State::Failed;
            it->second.failed_at = nowMs();
        }
    }
    for (auto &c : clients)
        c.second->close();
}
