// SPDX-License-Identifier: MIT
//
// AAC decoding (fdk-aac) to 48 kHz stereo 16-bit PCM, the format the
// console's audio output takes. Platform-free.
#pragma once

#include <cstdint>
#include <vector>

class AacDecoder {
public:
    static constexpr int kOutRate = 48000;

    AacDecoder() = default;
    ~AacDecoder();
    AacDecoder(const AacDecoder &) = delete;
    AacDecoder &operator=(const AacDecoder &) = delete;

    // asc: the AudioSpecificConfig from the MP4's esds.
    bool open(const std::vector<uint8_t> &asc);
    void close();
    // Decodes one AAC access unit and appends interleaved stereo samples
    // (L, R, L, R...) at kOutRate to out. False if the frame was bad.
    bool decode(const uint8_t *data, size_t size, std::vector<int16_t> &out);
    // Forgets resampler history (after a seek).
    void reset();

private:
    void *m_handle = nullptr;  // HANDLE_AACDECODER
    std::vector<int16_t> m_pcm;
    // Linear resampler: position in source frames, and the last source frame.
    double m_pos = 0;
    int16_t m_last[2] = {0, 0};
    bool m_haveLast = false;
};
