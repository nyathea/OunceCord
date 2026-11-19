// SPDX-License-Identifier: MIT
#include "media/source.h"

#include <cstring>

bool MemorySource::read(uint64_t offset, void *out, size_t size)
{
    if (offset > m_data.size() || size > m_data.size() - offset)
        return false;
    memcpy(out, m_data.data() + offset, size);
    return true;
}

FileSource::~FileSource()
{
    if (m_file)
        fclose(m_file);
}

bool FileSource::open(const std::string &path)
{
    m_file = fopen(path.c_str(), "rb");
    if (!m_file)
        return false;
    fseek(m_file, 0, SEEK_END);
    long end = ftell(m_file);
    m_size = end > 0 ? (uint64_t)end : 0;
    return true;
}

bool FileSource::read(uint64_t offset, void *out, size_t size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file || offset > m_size || size > m_size - offset)
        return false;
    if (fseeko(m_file, (off_t)offset, SEEK_SET) != 0)
        return false;
    return fread(out, 1, size, m_file) == size;
}
