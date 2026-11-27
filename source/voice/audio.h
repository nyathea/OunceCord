// SPDX-License-Identifier: MIT
//
// The console's microphone and speakers for voice chat: audin (the default
// input device, the built-in mic or a headset's) and audout, both at 48 kHz
// 16-bit stereo, in 20 ms frames (960 samples per channel) as Opus wants.
// Polled from the voice thread; nothing here blocks.
//
// The buffer handling follows DiscordNX2's (MIT): a buffer belongs to the
// service until it is released, so only released buffers are read or reused.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace voice {

constexpr int kFrameSamples = 960;  // per channel, 20 ms at 48 kHz

class AudioIO {
public:
    ~AudioIO() { close(); }
    bool open();
    void close();
    bool isOpen() const { return m_open; }
    bool hasMicrophone() const { return m_inOpen; }
    bool capturing() const { return m_capturing; }
    // Samples (per channel) the microphone has delivered, for the log.
    uint64_t capturedFrames() const { return m_capturedFrames; }

    // Stops or starts the microphone (muted: not capturing at all).
    void setCapture(bool on);
    // The next 20 ms of microphone audio, as mono (the louder channel). False when there
    // isn't a whole frame yet.
    bool readFrame(int16_t mono[kFrameSamples]);
    // Whether the speakers want the next 20 ms frame now.
    bool wantsFrame();
    // Queues 20 ms of interleaved stereo audio.
    void playFrame(const int16_t stereo[kFrameSamples * 2]);

private:
    bool m_open = false;
    bool m_inOpen = false, m_outOpen = false;
    bool m_capturing = false;
    struct Impl;
    Impl *m = nullptr;
    std::vector<int16_t> m_mono;  // captured samples not yet handed out
    uint64_t m_capturedFrames = 0;
};

}  // namespace voice
