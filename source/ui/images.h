// SPDX-License-Identifier: MIT
//
// Loads images from Discord's CDN and media proxies into GL textures.
//
// get() is called every frame for each image on screen. It returns the
// texture once ready, and otherwise queues the image. Worker threads read it
// from the SD-card cache or download it, and decode it; update() uploads
// decoded images on the UI thread (which owns the GL context).
//
// Being polite to the CDN: the newest requests go first (what is on screen),
// requests not asked for again within a couple of seconds are dropped (you
// scrolled past), downloads are paced, a 429 pauses all downloads for its
// Retry-After, and failures are not retried for a while. CDN image paths
// carry a content hash, so cached files never go stale.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <GLES3/gl3.h>

#include "util/gif.h"

class ImageCache {
public:
    static constexpr int kWorkers = 3;
    static constexpr size_t kMaxTextures = 400;
    static constexpr size_t kMaxTextureBytes = 160u << 20;  // GPU memory for textures
    static constexpr int kMaxImageSide = 2048;
    // Decoded frames kept per animated GIF; longer GIFs play their start.
    static constexpr size_t kMaxAnimationBytes = 32u << 20;
    static constexpr int kMaxDownloadsPerSecond = 15;
    static constexpr uint64_t kStaleMs = 2000;          // drop queued work not re-requested
    static constexpr uint64_t kFailRetryMs = 5 * 60 * 1000;
    static constexpr int kUploadsPerFrame = 6;
    static constexpr size_t kMaxDiskFiles = 5000;

    bool init();
    void exit();

    // Texture for a CDN path ("/avatars/..png?size=64") or a full image URL
    // on one of Discord's media hosts, or 0 while loading. failed is set
    // when the image could not be loaded (it is retried later). Animated
    // GIFs return the frame to show now, so call this every frame.
    GLuint get(const std::string &path, bool *failed = nullptr);
    // Uploads finished images. Call once per frame on the GL thread.
    void update();

private:
    enum class State { Queued, Loading, Decoded, Ready, Failed };
    struct Entry {
        State state = State::Queued;
        std::vector<GLuint> texs;  // one per frame; static images have one
        std::vector<int> delays;   // per frame, ms
        int total_ms = 0;
        uint64_t ready_at = 0;     // animation start
        uint64_t last_used = 0;
        uint64_t failed_at = 0;
        int width = 0, height = 0;
        size_t bytes = 0;               // GPU memory of the uploaded frames
        std::vector<gif::Frame> pending;  // decoded, waiting for upload
        size_t next_upload = 0;           // index into pending
    };

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<std::string, Entry> m_entries;
    std::deque<uint64_t> m_recentDownloads;  // for pacing
    uint64_t m_pausedUntil = 0;              // after a CDN 429
    std::vector<std::thread> m_threads;
    std::atomic<bool> m_running{false};
    size_t m_textureCount = 0;
    size_t m_textureBytes = 0;

    void worker();
    // Picks the most recently requested queued path. Called with the lock held.
    bool takeJob(std::string &path, uint64_t now, uint64_t &wait_ms);
    void evict();
    static std::string diskPath(const std::string &path);
};
