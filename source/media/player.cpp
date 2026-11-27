// SPDX-License-Identifier: MIT
#include "media/player.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <malloc.h>
#include <switch.h>
#include <switch2/video/decoder.h>

#include "media/aac.h"
#include "util/util.h"

namespace {

constexpr int kAudioBuffers = 4;
constexpr size_t kAudioFrames = 2048;  // per buffer: about 43 ms
constexpr size_t kAudioBufferBytes = (kAudioFrames * 4 + 0xfff) & ~(size_t)0xfff;

VideoDecoder *dec(void *p) { return (VideoDecoder *)p; }

std::string hexString(Result rc)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%x", (unsigned)rc);
    return buf;
}

}  // namespace

VideoPlayer::~VideoPlayer() { close(); }

bool VideoPlayer::open(std::shared_ptr<MediaSource> source, std::string &error, bool sound)
{
    close();
    m_source = std::move(source);
    MediaSource *src = m_source.get();
    if (!mp4::parse([src](uint64_t off, void *out, size_t n) { return src->read(off, out, n); }, src->size(), m_file,
                    error)) {
        m_source.reset();
        return false;
    }
    if (m_file.video >= 0) {
        const mp4::Track &v = m_file.tracks[m_file.video];
        if (v.codec != "avc1" && v.codec != "avc3") {
            error = "This video uses " + v.codec + ", which can't be played yet (only H.264).";
            m_source.reset();
            return false;
        }
        if (!mp4::avccToAnnexB(v.avcc, m_codecConfig, m_nalLength)) {
            error = "The video's H.264 settings are missing or damaged.";
            m_source.reset();
            return false;
        }
        uint32_t max_sample = 0;
        for (const auto &s : v.samples)
            max_sample = std::max(max_sample, s.size);
        VideoDecoderConfig config{};
        config.codec = ViddecCodec_H264;
        config.width = (uint32_t)((v.width + 15) & ~15);
        config.height = (uint32_t)((v.height + 15) & ~15);
        config.codec_config = m_codecConfig.data();
        config.codec_config_size = m_codecConfig.size();
        // Length prefixes shorter than start codes grow the sample a little.
        config.max_sample_size = (uint64_t)max_sample * 2 + 4096;
        auto *d = new VideoDecoder{};
        logf("player: starting the decoder: %s %dx%d, %zu samples, config %zu bytes", v.codec.c_str(), v.width,
             v.height, v.samples.size(), m_codecConfig.size());
        Result rc = videoDecoderCreate(d, &config);
        logf("player: decoder 0x%x", rc);
        if (R_FAILED(rc)) {
            delete d;
            logf("player: videoDecoderCreate %dx%d failed 0x%x", v.width, v.height, rc);
            error = "The video decoder couldn't start (" + std::to_string(v.width) + "x" + std::to_string(v.height) +
                    ", error 0x" + hexString(rc) + ").";
            m_source.reset();
            return false;
        }
        m_decoder = d;
    }
    if (m_file.audio >= 0 && !sound) {
        logf("player: no sound: in a voice call");
    } else if (m_file.audio >= 0) {
        const mp4::Track &a = m_file.tracks[m_file.audio];
        if (a.object_type == 0x40 && !a.asc.empty()) {
            logf("player: starting audio out");
            Result rc = audoutInitialize();
            if (R_SUCCEEDED(rc))
                rc = audoutStartAudioOut();
            m_audioOpen = R_SUCCEEDED(rc);
            if (!m_audioOpen)
                logf("player: audio out failed 0x%x", rc);
        } else {
            logf("player: no sound: audio is %s (object type 0x%x)", a.codec.c_str(), a.object_type);
        }
    }
    logf("player: %s, %.1fs, video %s, audio %s", hasVideo() ? "video" : "audio only", m_file.duration_us / 1e6,
         hasVideo() ? m_file.tracks[m_file.video].codec.c_str() : "-", m_audioOpen ? "AAC" : "none");
    m_open = true;
    m_paused = false;
    m_error.clear();
    startThreads(0);
    return true;
}

void VideoPlayer::close()
{
    if (!m_open && !m_decoder && !m_source)
        return;
    stopThreads();
    if (m_decoder) {
        videoDecoderClose(dec(m_decoder));
        delete dec(m_decoder);
        m_decoder = nullptr;
    }
    if (m_audioOpen) {
        audoutStopAudioOut();
        audoutExit();
        m_audioOpen = false;
    }
    if (m_yTex)
        glDeleteTextures(1, &m_yTex);
    if (m_uvTex)
        glDeleteTextures(1, &m_uvTex);
    m_yTex = m_uvTex = 0;
    m_frameW = m_frameH = m_texW = m_texH = 0;
    m_spare.clear();
    m_file = mp4::File{};
    m_source.reset();
    m_open = false;
}

void VideoPlayer::startThreads(int64_t from_us)
{
    m_stop = false;
    m_videoDone = m_decoder == nullptr;
    m_running = false;
    m_startUs = from_us;
    {
        std::lock_guard<std::mutex> lock(m_clockMutex);
        m_clockPos = from_us;
        m_clockWallMs = nowMs();
    }
    if (m_decoder)
        m_videoThread = std::thread(&VideoPlayer::videoLoop, this, from_us);
    else
        m_running = true;  // sound only: start the clock now
    if (m_audioOpen)
        m_audioThread = std::thread(&VideoPlayer::audioLoop, this, from_us);
}

void VideoPlayer::stopThreads()
{
    m_stop = true;
    if (m_videoThread.joinable())
        m_videoThread.join();
    if (m_audioThread.joinable())
        m_audioThread.join();
    std::lock_guard<std::mutex> lock(m_frameMutex);
    for (auto &f : m_ready)
        m_spare.push_back(std::move(f));
    m_ready.clear();
}

void VideoPlayer::fail(const std::string &why)
{
    logf("player: %s", why.c_str());
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_error.empty())
        m_error = why;
}

std::string VideoPlayer::error() const
{
    std::lock_guard<std::mutex> lock(m_frameMutex);
    return m_error;
}

void VideoPlayer::videoLoop(int64_t from_us)
{
    VideoDecoder *d = dec(m_decoder);
    const mp4::Track &t = m_file.tracks[m_file.video];
    logf("player: video thread from %.2fs", from_us / 1e6);
    bool logged_frame = false;
    // Start at the last keyframe at or before the target.
    size_t first = 0;
    for (size_t i = 0; i < t.samples.size(); i++)
        if (t.samples[i].key && t.samples[i].pts_us <= from_us)
            first = i;

    // Moves decoded frames into m_ready while there is room. True at the end
    // of the stream.
    auto drain = [&]() {
        while (!m_stop) {
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                if (m_ready.size() >= kMaxFrames)
                    return false;
            }
            VideoFrame vf;
            if (R_FAILED(videoDecoderGetFrame(d, &vf, 0)))
                return false;
            bool eos = vf.end_of_stream;
            int64_t pts = vf.timestamp_ns / 1000;
            if (!logged_frame) {
                logged_frame = true;
                logf("player: first frame %ux%u at %.3fs, %s, pitches %u/%u", vf.width, vf.height, pts / 1e6,
                     vf.planes[0] ? "pitch-linear" : "block-linear", vf.pitches[0], vf.pitches[1]);
            }
            // Frames before a seek target are decoded but not shown.
            if (pts + 1000 >= from_us && vf.width && vf.height) {
                Frame f;
                {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    if (!m_spare.empty()) {
                        f = std::move(m_spare.back());
                        m_spare.pop_back();
                    }
                }
                f.w = (int)vf.width & ~1;
                f.h = (int)vf.height & ~1;
                f.pts_us = pts;
                f.y.resize((size_t)f.w * f.h);
                f.uv.resize((size_t)f.w * f.h / 2);
                if (vf.planes[0] && vf.planes[1]) {
                    for (int r = 0; r < f.h; r++)
                        memcpy(&f.y[(size_t)r * f.w], vf.planes[0] + (size_t)r * vf.pitches[0], f.w);
                    for (int r = 0; r < f.h / 2; r++)
                        memcpy(&f.uv[(size_t)r * f.w], vf.planes[1] + (size_t)r * vf.pitches[1], f.w);
                } else {
                    videoDecoderCopyFrame(d, &vf, f.y.data(), f.w, f.uv.data(), f.w);
                }
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_ready.push_back(std::move(f));
            }
            videoDecoderReleaseFrame(d, &vf);
            if (eos)
                return true;
        }
        return false;
    };

    std::vector<uint8_t> raw;
    std::string annexb;
    bool eos = false;
    for (size_t i = first; i < t.samples.size() && !m_stop && !eos; i++) {
        const mp4::Sample &s = t.samples[i];
        raw.resize(s.size);
        if (!m_source->read(s.offset, raw.data(), raw.size()) ||
            !mp4::sampleToAnnexB(raw.data(), raw.size(), m_nalLength, annexb)) {
            fail("The video file is damaged.");
            break;
        }
        bool last = i + 1 == t.samples.size();
        while (!m_stop) {
            eos = drain() || eos;
            Result rc = videoDecoderSubmit(d, annexb.data(), annexb.size(), s.pts_us * 1000, last, 10'000'000ULL);
            if (R_SUCCEEDED(rc))
                break;
            if (rc != KERNELRESULT(TimedOut)) {
                fail("The video decoder stopped (error 0x" + hexString(rc) + ").");
                m_stop = true;
            }
        }
    }
    // Collect what is still in the decoder.
    uint64_t give_up = nowMs() + 3000;
    while (!m_stop && !eos && nowMs() < give_up) {
        if (drain())
            break;
        svcSleepThread(2'000'000);
    }
    m_videoDone = true;
}

void VideoPlayer::audioLoop(int64_t from_us)
{
    const mp4::Track &t = m_file.tracks[m_file.audio];
    AacDecoder aac;
    if (!aac.open(t.asc)) {
        logf("player: AAC decoder failed to open");
        return;
    }
    size_t idx = 0;
    while (idx < t.samples.size() && t.samples[idx].pts_us < from_us)
        idx++;

    struct Buf {
        AudioOutBuffer out{};
        void *mem = nullptr;
    };
    Buf bufs[kAudioBuffers];
    std::vector<AudioOutBuffer *> free_list;
    for (auto &b : bufs) {
        b.mem = memalign(0x1000, kAudioBufferBytes);
        if (!b.mem)
            continue;
        b.out.buffer = b.mem;
        b.out.buffer_size = kAudioBufferBytes;
        free_list.push_back(&b.out);
    }
    int queued = 0;
    std::vector<int16_t> pcm, raw16;
    std::vector<uint8_t> raw;

    logf("player: audio thread from %.2fs", from_us / 1e6);
    // Wait for the picture before starting the sound.
    while (!m_stop && !m_running)
        svcSleepThread(2'000'000);

    while (!m_stop) {
        if (m_paused) {
            svcSleepThread(10'000'000);
            continue;
        }
        while (pcm.size() < kAudioFrames * 2 && idx < t.samples.size()) {
            const mp4::Sample &s = t.samples[idx++];
            raw.resize(s.size);
            if (m_source->read(s.offset, raw.data(), raw.size()))
                aac.decode(raw.data(), raw.size(), pcm);
        }
        if (pcm.empty() && idx >= t.samples.size())
            break;
        while (free_list.empty() && !m_stop) {
            AudioOutBuffer *released = nullptr;
            u32 count = 0;
            if (R_SUCCEEDED(audoutWaitPlayFinish(&released, &count, 20'000'000ULL)) && released && count) {
                free_list.push_back(released);
                queued--;
            }
        }
        if (m_stop)
            break;
        AudioOutBuffer *b = free_list.back();
        free_list.pop_back();
        size_t n = std::min(pcm.size(), kAudioFrames * 2);
        memcpy(b->buffer, pcm.data(), n * 2);
        b->data_size = n * 2;
        b->data_offset = 0;
        pcm.erase(pcm.begin(), pcm.begin() + n);
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(b)))
            queued++;
        else
            free_list.push_back(b);
    }
    // Let queued sound finish (or drop it when stopping), then free buffers.
    uint64_t give_up = nowMs() + 500;
    while (queued > 0 && nowMs() < give_up) {
        AudioOutBuffer *released = nullptr;
        u32 count = 0;
        if (R_SUCCEEDED(audoutWaitPlayFinish(&released, &count, 20'000'000ULL)) && released && count)
            queued--;
    }
    if (queued > 0) {
        // Still held by the service: stopping releases them.
        audoutStopAudioOut();
        audoutFlushAudioOutBuffers(nullptr);
        audoutStartAudioOut();
    }
    for (auto &b : bufs)
        free(b.mem);
}

void VideoPlayer::upload(const Frame &f)
{
    if (!m_yTex) {
        glGenTextures(1, &m_yTex);
        glGenTextures(1, &m_uvTex);
        for (GLuint tex : {m_yTex, m_uvTex}) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, m_yTex);
    if (f.w != m_texW || f.h != m_texH)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, f.w, f.h, 0, GL_RED, GL_UNSIGNED_BYTE, f.y.data());
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.w, f.h, GL_RED, GL_UNSIGNED_BYTE, f.y.data());
    glBindTexture(GL_TEXTURE_2D, m_uvTex);
    if (f.w != m_texW || f.h != m_texH)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, f.w / 2, f.h / 2, 0, GL_RG, GL_UNSIGNED_BYTE, f.uv.data());
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, f.w / 2, f.h / 2, GL_RG, GL_UNSIGNED_BYTE, f.uv.data());
    m_texW = f.w;
    m_texH = f.h;
    m_frameW = f.w;
    m_frameH = f.h;
}

void VideoPlayer::update()
{
    if (!m_open || !m_decoder)
        return;
    Frame show;
    bool have = false;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (!m_ready.empty()) {
            if (!m_running) {
                // First frame after a start or seek: show it and start the clock.
                show = std::move(m_ready.front());
                m_ready.pop_front();
                have = true;
            } else {
                int64_t now = position();
                // Skip frames that are already late, keep the newest due one.
                while (m_ready.size() >= 2 && m_ready[1].pts_us <= now) {
                    m_spare.push_back(std::move(m_ready.front()));
                    m_ready.pop_front();
                }
                if (m_ready.front().pts_us <= now) {
                    show = std::move(m_ready.front());
                    m_ready.pop_front();
                    have = true;
                }
            }
        }
    }
    if (!have)
        return;
    upload(show);
    if (!m_running) {
        std::lock_guard<std::mutex> lock(m_clockMutex);
        m_clockPos = std::max(m_startUs, show.pts_us);
        m_clockWallMs = nowMs();
        m_running = true;
    }
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_spare.push_back(std::move(show));
}

int64_t VideoPlayer::position() const
{
    std::lock_guard<std::mutex> lock(m_clockMutex);
    if (m_paused || !m_running)
        return m_clockPos;
    int64_t p = m_clockPos + (int64_t)(nowMs() - m_clockWallMs) * 1000;
    return std::min(p, m_file.duration_us);
}

bool VideoPlayer::ended() const
{
    if (!m_open || !m_running)
        return false;
    return position() >= m_file.duration_us && m_videoDone;
}

void VideoPlayer::setPaused(bool paused)
{
    if (paused == m_paused || !m_open)
        return;
    int64_t pos = position();
    std::lock_guard<std::mutex> lock(m_clockMutex);
    m_clockPos = pos;
    m_clockWallMs = nowMs();
    m_paused = paused;
}

void VideoPlayer::seek(int64_t us)
{
    if (!m_open)
        return;
    us = std::clamp<int64_t>(us, 0, std::max<int64_t>(0, m_file.duration_us - 100000));
    stopThreads();
    if (m_decoder)
        videoDecoderFlush(dec(m_decoder));
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_error.clear();
    }
    startThreads(us);
}
