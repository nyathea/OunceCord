// SPDX-License-Identifier: MIT
//
// Random-access byte sources for the demuxer: a downloaded file in memory,
// or a file on the SD card. read() is thread-safe.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

class MediaSource {
public:
    virtual ~MediaSource() = default;
    virtual uint64_t size() const = 0;
    virtual bool read(uint64_t offset, void *out, size_t size) = 0;
};

class MemorySource : public MediaSource {
public:
    explicit MemorySource(std::string data) : m_data(std::move(data)) {}
    uint64_t size() const override { return m_data.size(); }
    bool read(uint64_t offset, void *out, size_t size) override;

private:
    std::string m_data;
};

class FileSource : public MediaSource {
public:
    ~FileSource() override;
    bool open(const std::string &path);
    uint64_t size() const override { return m_size; }
    bool read(uint64_t offset, void *out, size_t size) override;

private:
    std::mutex m_mutex;
    FILE *m_file = nullptr;
    uint64_t m_size = 0;
};
