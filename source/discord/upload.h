// SPDX-License-Identifier: MIT
//
// Sending a file with a message: a multipart/form-data body holding the
// message JSON ("payload_json") and the file ("files[0]"). Platform-free.
#pragma once

#include <string>

namespace discord {

struct Upload {
    std::string filename;
    std::string content_type = "image/jpeg";
    std::string data;
};

// The message JSON must already list the file in "attachments" as
// {"id": 0, "filename": ...}. boundary must not occur in the data; pick a
// random one (multipartBoundary).
std::string multipartBody(const std::string &boundary, const std::string &payload_json, const Upload &upload);
std::string multipartBoundary(const unsigned char random[16]);
// Filenames go in a header: keep them to safe characters.
std::string safeFilename(const std::string &name);

}  // namespace discord
