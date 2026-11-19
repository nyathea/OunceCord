// SPDX-License-Identifier: MIT
#include "ui/album.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <cstdio>

#include "config.h"
#include "util/imageutil.h"
#include "util/util.h"

namespace {

constexpr const char *kSdAlbum = "sdmc:/Nintendo/Album";

bool readWhole(const std::string &path, std::string &out, uint64_t max)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    out.clear();
    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
        if (out.size() > max) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return !out.empty();
}

// Numeric subdirectories of path ("2026", "11", ...).
std::vector<std::string> numericDirs(const std::string &path)
{
    std::vector<std::string> out;
    if (DIR *d = opendir(path.c_str())) {
        while (dirent *e = readdir(d)) {
            std::string name = e->d_name;
            if (!name.empty() && name.size() <= 4 &&
                std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; }))
                out.push_back(name);
        }
        closedir(d);
    }
    return out;
}

}  // namespace

void Album::listCaps(std::vector<Item> &out)
{
    if (!m_capsTried) {
        m_capsTried = true;
        Result rc = capsaInitialize();
        m_capsOk = R_SUCCEEDED(rc);
        if (!m_capsOk)
            logf("album: caps:a unavailable (0x%x)", rc);
    }
    if (!m_capsOk)
        return;
    auto add = [&](const CapsAlbumEntry &e) {
        if (e.file_id.content != CapsAlbumFileContents_ScreenShot &&
            e.file_id.content != CapsAlbumFileContents_ExtraScreenShot)
            return;
        Item it;
        it.caps = true;
        it.id = e.file_id;
        it.size = e.size;
        const CapsAlbumFileDateTime &dt = e.file_id.datetime;
        it.date = {dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.id};
        char key[96];
        snprintf(key, sizeof(key), "local:album/c%d/%04d%02d%02d%02d%02d%02d%02d-%016llx-%d", (int)e.file_id.storage,
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.id,
                 (unsigned long long)e.file_id.application_id, (int)e.file_id.content);
        it.key = key;
        out.push_back(std::move(it));
    };
    for (CapsAlbumStorage storage : {CapsAlbumStorage_Nand, CapsAlbumStorage_Sd}) {
        // Both listing calls: firmware differs in what each returns.
        u64 count = 0, got = 0, count_ex = 0, got_ex = 0;
        Result rc = capsaGetAlbumFileCount(storage, &count);
        if (R_SUCCEEDED(rc) && count) {
            std::vector<CapsAlbumEntry> entries(count);
            rc = capsaGetAlbumFileList(storage, &got, entries.data(), count);
            if (R_SUCCEEDED(rc))
                for (u64 i = 0; i < got && i < count; i++)
                    add(entries[i]);
        }
        Result rc2 = capsaGetAlbumFileCountEx0(storage, CapsAlbumFileContentsFlag_ScreenShot, &count_ex);
        if (R_SUCCEEDED(rc2) && count_ex) {
            std::vector<CapsAlbumEntry> entries(count_ex);
            rc2 = capsaGetAlbumFileListEx0(storage, CapsAlbumFileContentsFlag_ScreenShot, &got_ex, entries.data(),
                                           count_ex);
            if (R_SUCCEEDED(rc2))
                for (u64 i = 0; i < got_ex && i < count_ex; i++)
                    add(entries[i]);
        }
        logf("album: caps storage %d: count %llu listed %llu (0x%x), ex0 count %llu listed %llu (0x%x)",
             (int)storage, (unsigned long long)count, (unsigned long long)got, rc, (unsigned long long)count_ex,
             (unsigned long long)got_ex, rc2);
    }
}

void Album::listDir(const std::string &root, std::vector<Item> &out, int &skipped, std::string &example)
{
    auto scanYears = [&](const std::string &base) {
        for (const auto &y : numericDirs(base)) {
            std::string yp = base + "/" + y;
            for (const auto &m : numericDirs(yp)) {
                std::string mp = yp + "/" + m;
                for (const auto &d : numericDirs(mp)) {
                    std::string dp = mp + "/" + d;
                    DIR *dir = opendir(dp.c_str());
                    if (!dir)
                        continue;
                    while (dirent *e = readdir(dir)) {
                        if (e->d_name[0] == '.')
                            continue;
                        Item it;
                        if (!album::parseFileName(e->d_name, it.date)) {
                            std::string n = e->d_name;
                            bool video = n.size() > 4 && (n.substr(n.size() - 4) == ".mp4" || n.substr(n.size() - 4) == ".MP4");
                            if (!video) {
                                skipped++;
                                if (example.empty())
                                    example = dp + "/" + n;
                            }
                            continue;
                        }
                        it.path = dp + "/" + e->d_name;
                        struct stat st{};
                        if (stat(it.path.c_str(), &st) == 0)
                            it.size = (uint64_t)st.st_size;
                        it.key = "local:album/f/" + it.path;
                        out.push_back(std::move(it));
                    }
                    closedir(dir);
                }
            }
        }
    };
    scanYears(root);
    // Screenshots from some software live under Extra/<application id>/.
    std::string extra = root + "/Extra";
    if (DIR *d = opendir(extra.c_str())) {
        std::vector<std::string> apps;
        while (dirent *e = readdir(d))
            if (e->d_name[0] != '.')
                apps.push_back(e->d_name);
        closedir(d);
        for (const auto &a : apps)
            scanYears(extra + "/" + a);
    }
}

bool Album::refresh()
{
    std::vector<Item> caps, files;
    int skipped = 0;
    std::string example;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        listCaps(caps);
        if (!m_mountTried) {
            m_mountTried = true;
            const std::pair<FsImageDirectoryId, const char *> dirs[] = {{FsImageDirectoryId_Nand, "albumnand"},
                                                                        {FsImageDirectoryId_Sd, "albumsd"}};
            for (const auto &[id, name] : dirs) {
                FsFileSystem fs;
                Result rc = fsOpenImageDirectoryFileSystem(&fs, id);
                if (R_SUCCEEDED(rc) && fsdevMountDevice(name, fs) >= 0)
                    m_mounts.push_back(std::string(name) + ":");
                else
                    logf("album: can't open the %s image directory (0x%x)", name, rc);
            }
        }
    }
    for (const auto &m : m_mounts) {
        size_t before = files.size();
        listDir(m, files, skipped, example);
        logf("album: %s has %zu screenshots", m.c_str(), files.size() - before);
    }
    size_t before = files.size();
    listDir(kSdAlbum, files, skipped, example);
    logf("album: %s has %zu screenshots", kSdAlbum, files.size() - before);
    if (skipped)
        logf("album: skipped %d files with unexpected names, e.g. %s", skipped, example.c_str());

    // Merge: one entry per screenshot (date plus its id), preferring the
    // album service's, which has a thumbnail API.
    std::map<uint64_t, Item> merged;
    for (auto &it : caps)
        merged.emplace(it.date.sortKey(), std::move(it));
    for (auto &it : files)
        merged.emplace(it.date.sortKey(), std::move(it));
    std::vector<Item> items;
    items.reserve(merged.size());
    for (auto it = merged.rbegin(); it != merged.rend() && items.size() < kMaxItems; ++it)
        items.push_back(std::move(it->second));

    m_error.clear();
    if (items.empty())
        m_error = "No screenshots found in the album.";
    logf("album: %zu screenshots in all (%zu from the album service)", items.size(), caps.size());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_items = std::move(items);
    index();
    return !m_items.empty();
}

void Album::index()
{
    m_byKey.clear();
    for (const auto &it : m_items)
        m_byKey[it.key] = it;
    for (const auto &it : m_saved)
        m_byKey[it.key] = it;
}

std::string Album::Item::contentType() const
{
    std::string t = imageutil::contentTypeForName(saved ? name : path);
    return t.empty() ? "image/jpeg" : t;
}

bool Album::refreshSaved()
{
    std::vector<std::pair<time_t, Item>> found;
    if (DIR *d = opendir(config::kSavedDir)) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.empty() || n[0] == '.' || imageutil::contentTypeForName(n).empty())
                continue;
            Item it;
            it.saved = true;
            it.name = n;
            it.path = std::string(config::kSavedDir) + "/" + n;
            struct stat st{};
            time_t mtime = 0;
            if (stat(it.path.c_str(), &st) == 0) {
                it.size = (uint64_t)st.st_size;
                mtime = st.st_mtime;
            }
            // The size and time are in the key so a replaced file reloads.
            it.key = "local:saved/" + n + "?" + std::to_string(it.size) + "-" + std::to_string((long long)mtime);
            found.emplace_back(mtime, std::move(it));
        }
        closedir(d);
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second.name < b.second.name;
    });
    std::vector<Item> saved;
    for (auto &f : found)
        if (saved.size() < kMaxItems)
            saved.push_back(std::move(f.second));
    m_savedError = saved.empty() ? "Nothing saved yet. Open a picture in chat and press (Y) to save it here." : "";
    std::lock_guard<std::mutex> lock(m_mutex);
    m_saved = std::move(saved);
    index();
    return !m_saved.empty();
}

bool Album::loadThumbnail(const std::string &key, std::string &jpeg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto found = m_byKey.find(key);
    if (found == m_byKey.end())
        return false;
    const Item &it = found->second;
    if (it.caps) {
        jpeg.resize(64 * 1024);
        u64 size = 0;
        Result rc = capsaLoadAlbumFileThumbnail(&it.id, &size, jpeg.data(), jpeg.size());
        if (R_FAILED(rc) || !size || size > jpeg.size())
            return false;
        jpeg.resize(size);
        return true;
    }
    std::string file;
    if (!readWhole(it.path, file, it.saved ? (64u << 20) : kMaxFileBytes))
        return false;
    std::string_view thumb;
    if (album::exifThumbnail(file, thumb))
        jpeg.assign(thumb.data(), thumb.size());
    else
        jpeg = std::move(file);  // no preview inside: use the picture itself
    return true;
}

bool Album::loadFile(const Item &item, std::string &jpeg, std::string &error)
{
    if (item.size > kMaxFileBytes) {
        error = "That picture is over Discord's 10 MB limit.";
        return false;
    }
    if (item.caps) {
        std::lock_guard<std::mutex> lock(m_mutex);
        u64 size = item.size;
        if (!size && R_FAILED(capsaGetAlbumFileSize(&item.id, &size)))
            size = 0;
        if (!size || size > kMaxFileBytes) {
            error = size ? "That picture is over Discord's 10 MB limit." : "Couldn't read that picture.";
            return false;
        }
        jpeg.resize(size);
        u64 got = 0;
        Result rc = capsaLoadAlbumFile(&item.id, &got, jpeg.data(), jpeg.size());
        if (R_FAILED(rc) || !got) {
            logf("album: load failed: 0x%x", rc);
            error = "Couldn't read that picture.";
            return false;
        }
        jpeg.resize(std::min<u64>(got, size));
        return true;
    }
    if (!readWhole(item.path, jpeg, kMaxFileBytes)) {
        error = "Couldn't read that picture.";
        return false;
    }
    return true;
}

void Album::exit()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_capsOk)
        capsaExit();
    m_capsOk = false;
    m_capsTried = false;
    for (const auto &m : m_mounts)
        fsdevUnmountDevice(m.substr(0, m.size() - 1).c_str());
    m_mounts.clear();
    m_mountTried = false;
}
