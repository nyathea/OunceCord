// SPDX-License-Identifier: MIT
//
// A small MP4/MOV (ISO base media) demuxer: finds the first video and audio
// tracks and builds their sample tables, so samples can be read by offset.
// Non-fragmented files only (what phones, the Switch album and most
// uploads produce). Platform-free.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mp4 {

struct Sample {
    uint64_t offset = 0;
    uint32_t size = 0;
    int64_t pts_us = 0;  // presentation time, edit list applied
    int64_t dts_us = 0;  // decode time
    bool key = false;
};

struct Track {
    enum class Kind { Other, Video, Audio };
    Kind kind = Kind::Other;
    std::string codec;  // sample entry type: "avc1", "hvc1", "vp09", "mp4a"...
    uint32_t timescale = 0;
    int width = 0, height = 0;          // video
    int channels = 0, sample_rate = 0;  // audio (from the sample entry)
    std::vector<uint8_t> avcc;  // H.264 decoder configuration (avcC payload)
    std::vector<uint8_t> asc;   // AAC AudioSpecificConfig (from esds)
    uint8_t object_type = 0;    // esds objectTypeIndication (0x40 = AAC)
    std::vector<Sample> samples;  // in decode order
    int64_t duration_us = 0;
};

struct File {
    std::vector<Track> tracks;
    int video = -1, audio = -1;  // indexes into tracks, -1 if none
    int64_t duration_us = 0;
};

// Reads size bytes at offset into out; false on failure.
using Reader = std::function<bool(uint64_t offset, void *out, size_t size)>;

bool parse(const Reader &read, uint64_t file_size, File &out, std::string &error);

// H.264: the SPS and PPS from an avcC box as Annex-B (each after 00 00 00 01),
// and the length size of the samples' NAL units. False if malformed.
bool avccToAnnexB(const std::vector<uint8_t> &avcc, std::string &out, int &nal_length_size);
// Rewrites a sample's length-prefixed NAL units as Annex-B. False if malformed.
bool sampleToAnnexB(const uint8_t *data, size_t size, int nal_length_size, std::string &out);

}  // namespace mp4
