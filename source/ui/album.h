// SPDX-License-Identifier: MIT
//
// The console's screenshot album, for attaching pictures. Lists from the
// album service (caps:a, which has ready-made thumbnails) and also scans the
// album folders directly (system memory and microSD via the image directory
// file systems, and sdmc:/Nintendo/Album), since the service's listing can
// leave files out. Duplicates are merged. Videos are left out.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <switch.h>

#include "util/albumfile.h"

class Album {
public:
    static constexpr size_t kMaxItems = 3000;              // newest first
    static constexpr uint64_t kMaxFileBytes = 10u << 20;   // Discord's upload limit

    struct Item {
        std::string key;  // image-cache path of the thumbnail ("local:album/...")
        album::Date date;
        uint64_t size = 0;
        bool caps = false;
        CapsAlbumFileId id{};  // when caps
        std::string path;      // otherwise, a file path (sdmc:/..., albumnand:/...)
        bool saved = false;    // from the Saved folder (any picture, named by the user)
        std::string name;      // saved: the file name

        std::string label() const { return saved ? name : date.label(); }
        std::string uploadName() const { return saved ? name : date.fileName(); }
        std::string contentType() const;
    };

    // Lists the screenshots. False (with error()) when none could be read.
    bool refresh();
    const std::vector<Item> &items() const { return m_items; }
    // Pictures in config::kSavedDir, newest first.
    bool refreshSaved();
    const std::vector<Item> &saved() const { return m_saved; }
    const std::string &savedError() const { return m_savedError; }
    const std::string &error() const { return m_error; }
    void exit();

    // For the image cache's local loader; thread-safe.
    bool loadThumbnail(const std::string &key, std::string &jpeg);
    // The whole JPEG, for sending.
    bool loadFile(const Item &item, std::string &jpeg, std::string &error);

private:
    std::mutex m_mutex;  // guards m_byKey and caps calls
    std::vector<Item> m_items, m_saved;
    std::string m_savedError;
    void index();  // rebuilds m_byKey; call with m_mutex held
    std::map<std::string, Item> m_byKey;
    std::string m_error;
    bool m_capsTried = false, m_capsOk = false;
    bool m_mountTried = false;
    std::vector<std::string> m_mounts;  // mounted image directories ("albumnand:")

    void listCaps(std::vector<Item> &out);
    // Scans root/YYYY/MM/DD/*.jpg (and root/Extra/<id>/YYYY/...).
    void listDir(const std::string &root, std::vector<Item> &out, int &skipped, std::string &example);
};
