// SPDX-License-Identifier: MIT
#include "voice/audio.h"

#include <algorithm>
#include <cstring>

#if defined(__SWITCH__)
#include <malloc.h>
#include <switch.h>
#endif

#include "util/util.h"

namespace voice {

#if defined(__SWITCH__)

namespace {
constexpr int kInBuffers = 4;
constexpr size_t kInBytes = 0x1000;  // 1024 stereo samples, about 21 ms
constexpr int kOutBuffers = 6;
constexpr size_t kOutBytes = 0x1000;
constexpr int kOutQueued = 3;        // frames kept queued: about 60 ms of latency
}  // namespace

struct AudioIO::Impl {
    AudioInBuffer in[kInBuffers] = {};
    AudioOutBuffer out[kOutBuffers] = {};
    void *inMem[kInBuffers] = {};
    void *outMem[kOutBuffers] = {};
    bool outQueued[kOutBuffers] = {};
};

bool AudioIO::open()
{
    if (m_open)
        return true;
    m = new Impl;
    Result rc = audinInitialize();
    if (R_SUCCEEDED(rc)) {
        m_inOpen = true;
        char names[8][0x100] = {};
        u32 count = 0;
        if (R_SUCCEEDED(audinListAudioIns(&names[0][0], 8, &count)))
            for (u32 i = 0; i < count && i < 8; i++)
                logf("voice: audio input %u: %s", (unsigned)i, names[i]);
        if (audinGetSampleRate() != 48000 || audinGetChannelCount() != 2 || audinGetPcmFormat() != PcmFormat_Int16) {
            logf("voice: microphone format %u Hz x%u (format %d) isn't supported", (unsigned)audinGetSampleRate(),
                 (unsigned)audinGetChannelCount(), (int)audinGetPcmFormat());
            audinExit();
            m_inOpen = false;
        }
    } else {
        logf("voice: audinInitialize failed 0x%x; listening only", rc);
    }
    if (m_inOpen) {
        for (int i = 0; i < kInBuffers; i++) {
            m->inMem[i] = memalign(0x1000, kInBytes);
            if (!m->inMem[i]) {
                close();
                return false;
            }
            memset(m->inMem[i], 0, kInBytes);
            m->in[i].buffer = m->inMem[i];
            m->in[i].buffer_size = kInBytes;
            m->in[i].data_size = kInBytes;
            audinAppendAudioInBuffer(&m->in[i]);
        }
    }

    rc = audoutInitialize();
    if (R_FAILED(rc)) {
        logf("voice: audoutInitialize failed 0x%x", rc);
        close();
        return false;
    }
    m_outOpen = true;
    if (audoutGetSampleRate() != 48000 || audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16) {
        logf("voice: speaker format isn't 48 kHz 16-bit stereo");
        close();
        return false;
    }
    for (int i = 0; i < kOutBuffers; i++) {
        m->outMem[i] = memalign(0x1000, kOutBytes);
        if (!m->outMem[i]) {
            close();
            return false;
        }
        memset(m->outMem[i], 0, kOutBytes);
        m->out[i].buffer = m->outMem[i];
        m->out[i].buffer_size = kOutBytes;
        m->out[i].data_size = kFrameSamples * 4;
    }
    if (R_FAILED(audoutStartAudioOut())) {
        close();
        return false;
    }
    m_open = true;
    return true;
}

void AudioIO::close()
{
    if (m_inOpen) {
        if (m_capturing)
            audinStopAudioIn();
        audinExit();
        m_inOpen = false;
    }
    m_capturing = false;
    if (m_outOpen) {
        audoutStopAudioOut();
        audoutExit();
        m_outOpen = false;
    }
    if (m) {
        for (void *p : m->inMem)
            free(p);
        for (void *p : m->outMem)
            free(p);
        delete m;
        m = nullptr;
    }
    m_mono.clear();
    m_open = false;
}

void AudioIO::setCapture(bool on)
{
    if (!m_inOpen || on == m_capturing)
        return;
    Result rc = on ? audinStartAudioIn() : audinStopAudioIn();
    if (R_SUCCEEDED(rc))
        m_capturing = on;
    if (!on)
        m_mono.clear();
}

bool AudioIO::readFrame(int16_t mono[kFrameSamples])
{
    if (!m_inOpen)
        return false;
    for (int k = 0; k < kInBuffers; k++) {
        AudioInBuffer *released = nullptr;
        u32 count = 0;
        if (R_FAILED(audinGetReleasedAudioInBuffer(&released, &count)) || !count || !released)
            break;
        if (m_capturing) {
            const int16_t *pcm = (const int16_t *)released->buffer;
            size_t frames = std::min<size_t>(released->data_size, kInBytes) / 4;
            m_capturedFrames += frames;
            // The microphone may fill only one of the two channels: take
            // the louder one rather than averaging it down with silence.
            int64_t e[2] = {};
            for (size_t i = 0; i < frames; i++) {
                e[0] += (int64_t)pcm[i * 2] * pcm[i * 2];
                e[1] += (int64_t)pcm[i * 2 + 1] * pcm[i * 2 + 1];
            }
            int ch = e[1] > e[0] ? 1 : 0;
            for (size_t i = 0; i < frames; i++)
                m_mono.push_back(pcm[i * 2 + ch]);
        }
        released->data_size = kInBytes;
        audinAppendAudioInBuffer(released);
    }
    // Don't let a backlog build up (e.g. after a stall): keep the newest 100 ms.
    if (m_mono.size() > (size_t)kFrameSamples * 5)
        m_mono.erase(m_mono.begin(), m_mono.end() - kFrameSamples * 5);
    if (m_mono.size() < (size_t)kFrameSamples)
        return false;
    memcpy(mono, m_mono.data(), kFrameSamples * sizeof(int16_t));
    m_mono.erase(m_mono.begin(), m_mono.begin() + kFrameSamples);
    return true;
}

bool AudioIO::wantsFrame()
{
    if (!m_outOpen)
        return false;
    for (int k = 0; k < kOutBuffers; k++) {
        AudioOutBuffer *released = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)) || !count || !released)
            break;
        for (int i = 0; i < kOutBuffers; i++)
            if (released == &m->out[i])
                m->outQueued[i] = false;
    }
    int queued = 0;
    for (bool q : m->outQueued)
        queued += q;
    return queued < kOutQueued;
}

void AudioIO::playFrame(const int16_t stereo[kFrameSamples * 2])
{
    if (!m_outOpen)
        return;
    for (int i = 0; i < kOutBuffers; i++) {
        if (m->outQueued[i])
            continue;
        memcpy(m->outMem[i], stereo, kFrameSamples * 4);
        m->out[i].data_size = kFrameSamples * 4;
        if (R_SUCCEEDED(audoutAppendAudioOutBuffer(&m->out[i])))
            m->outQueued[i] = true;
        return;
    }
}

#else  // not on the console: no audio devices (host tests)

struct AudioIO::Impl {};
bool AudioIO::open() { return false; }
void AudioIO::close() { m_open = false; }
void AudioIO::setCapture(bool on) { m_capturing = on; }
bool AudioIO::readFrame(int16_t *) { return false; }
bool AudioIO::wantsFrame() { return false; }
void AudioIO::playFrame(const int16_t *) {}

#endif

}  // namespace voice
