// SPDX-License-Identifier: MIT
#include "media/aac.h"

#include <aacdecoder_lib.h>

AacDecoder::~AacDecoder() { close(); }

bool AacDecoder::open(const std::vector<uint8_t> &asc)
{
    close();
    if (asc.empty())
        return false;
    HANDLE_AACDECODER h = aacDecoder_Open(TT_MP4_RAW, 1);
    if (!h)
        return false;
    UCHAR *conf[] = {const_cast<UCHAR *>(asc.data())};
    UINT len[] = {(UINT)asc.size()};
    if (aacDecoder_ConfigRaw(h, conf, len) != AAC_DEC_OK) {
        aacDecoder_Close(h);
        return false;
    }
    // Always stereo: mono is duplicated, surround is downmixed.
    aacDecoder_SetParam(h, AAC_PCM_MIN_OUTPUT_CHANNELS, 2);
    aacDecoder_SetParam(h, AAC_PCM_MAX_OUTPUT_CHANNELS, 2);
    m_handle = h;
    m_pcm.resize(2048 * 8);
    reset();
    return true;
}

void AacDecoder::close()
{
    if (m_handle)
        aacDecoder_Close((HANDLE_AACDECODER)m_handle);
    m_handle = nullptr;
}

void AacDecoder::reset()
{
    m_pos = 0;
    m_haveLast = false;
}

bool AacDecoder::decode(const uint8_t *data, size_t size, std::vector<int16_t> &out)
{
    HANDLE_AACDECODER h = (HANDLE_AACDECODER)m_handle;
    if (!h || !size)
        return false;
    UCHAR *in[] = {const_cast<UCHAR *>(data)};
    UINT in_size[] = {(UINT)size};
    UINT valid = (UINT)size;
    if (aacDecoder_Fill(h, in, in_size, &valid) != AAC_DEC_OK)
        return false;
    if (aacDecoder_DecodeFrame(h, (INT_PCM *)m_pcm.data(), (INT)m_pcm.size(), 0) != AAC_DEC_OK)
        return false;
    CStreamInfo *info = aacDecoder_GetStreamInfo(h);
    if (!info || info->numChannels <= 0 || info->sampleRate <= 0)
        return false;
    int ch = info->numChannels, frames = info->frameSize;
    if (frames <= 0 || (size_t)frames * ch > m_pcm.size())
        return false;
    auto sample = [&](int f, int c) { return m_pcm[(size_t)f * ch + (ch > 1 ? c : 0)]; };

    if (info->sampleRate == kOutRate) {
        for (int f = 0; f < frames; f++) {
            out.push_back(sample(f, 0));
            out.push_back(sample(f, 1));
        }
        m_last[0] = sample(frames - 1, 0);
        m_last[1] = sample(frames - 1, 1);
        m_haveLast = true;
        return true;
    }
    // Source frame -1 is the previous call's last frame, so interpolation
    // continues across frames without a click.
    double step = (double)info->sampleRate / kOutRate;
    if (!m_haveLast) {
        m_last[0] = sample(0, 0);
        m_last[1] = sample(0, 1);
        m_haveLast = true;
    }
    while (m_pos < frames - 1) {
        int i = (int)m_pos;  // m_pos >= -1
        double t = m_pos - (m_pos < 0 ? -1 : i);
        if (m_pos < 0)
            i = -1;
        for (int c = 0; c < 2; c++) {
            int a = i < 0 ? m_last[c] : sample(i, c);
            int b = sample(i + 1, c);
            out.push_back((int16_t)(a + (b - a) * t));
        }
        m_pos += step;
    }
    m_pos -= frames;
    m_last[0] = sample(frames - 1, 0);
    m_last[1] = sample(frames - 1, 1);
    return true;
}
