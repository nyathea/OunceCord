// SPDX-License-Identifier: MIT
//
// Plays an MP4: H.264 video on the console's hardware decoder (libounce's
// VideoDecoder), AAC sound through audout. A video thread feeds the decoder
// and keeps a few decoded frames ready; an audio thread decodes and queues
// sound; update(), on the UI thread, uploads the frame that is due to GL
// textures for Renderer::drawNv12.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <GLES3/gl3.h>

#include "media/mp4.h"
#include "media/source.h"

class VideoPlayer {
public:
    ~VideoPlayer();

    // Starts playing from the beginning. False (with a reason) if it can't.
    // sound: false plays without audio (a voice call owns the speaker).
    bool open(std::shared_ptr<MediaSource> source, std::string &error, bool sound = true);
    bool hasSound() const { return m_audioOpen; }
    void close();
    bool isOpen() const { return m_open; }

    // UI thread, once per frame: advances to the frame due now.
    void update();
    bool hasFrame() const { return m_yTex != 0 && m_frameW > 0; }
    GLuint yTexture() const { return m_yTex; }
    GLuint uvTexture() const { return m_uvTex; }
    int width() const { return m_frameW; }
    int height() const { return m_frameH; }
    bool bt709() const { return m_frameH >= 720; }
    bool hasVideo() const { return m_file.video >= 0; }

    void setPaused(bool paused);
    bool paused() const { return m_paused; }
    // Jumps to a time (from the keyframe at or before it).
    void seek(int64_t us);
    int64_t position() const;  // microseconds
    int64_t duration() const { return m_file.duration_us; }
    bool ended() const;
    // Set when playback stopped on an error.
    std::string error() const;

private:
    struct Frame {
        std::vector<uint8_t> y, uv;
        int w = 0, h = 0;
        int64_t pts_us = 0;
    };
    static constexpr size_t kMaxFrames = 4;

    bool m_open = false;
    std::shared_ptr<MediaSource> m_source;
    mp4::File m_file;
    std::string m_codecConfig;  // H.264 SPS/PPS, Annex-B
    int m_nalLength = 4;
    void *m_decoder = nullptr;  // VideoDecoder
    bool m_audioOpen = false;

    // Playback clock: m_clockPos at m_clockWallMs, running unless paused or
    // waiting for the first frame after a start/seek.
    mutable std::mutex m_clockMutex;
    int64_t m_clockPos = 0;
    uint64_t m_clockWallMs = 0;
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_running{false};  // clock started (first frame shown)
    int64_t m_startUs = 0;               // where the current run began

    std::thread m_videoThread, m_audioThread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_videoDone{false};
    mutable std::mutex m_frameMutex;
    std::deque<Frame> m_ready;  // decoded, in presentation order
    std::vector<Frame> m_spare;
    std::string m_error;

    GLuint m_yTex = 0, m_uvTex = 0;
    int m_frameW = 0, m_frameH = 0;
    int m_texW = 0, m_texH = 0;

    void startThreads(int64_t from_us);
    void stopThreads();
    void videoLoop(int64_t from_us);
    void audioLoop(int64_t from_us);
    void fail(const std::string &why);
    void upload(const Frame &f);
};
