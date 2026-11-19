// SPDX-License-Identifier: MIT
#include "ui/saver.h"

#include <sys/stat.h>

#include <cstdio>
#include <map>
#include <memory>

#include "config.h"
#include "discord/cdn.h"
#include "discord/rest.h"
#include "discord/upload.h"
#include "net/http.h"
#include "util/imageutil.h"
#include "util/util.h"

void Saver::start()
{
    if (m_running)
        return;
    m_running = true;
    m_thread = std::thread(&Saver::worker, this);
}

void Saver::stop()
{
    if (!m_running)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void Saver::save(const std::string &url, const std::string &name)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push_back({url, name});
        m_busy = true;
    }
    m_cv.notify_all();
}

bool Saver::takeResult(std::string &message, bool &ok)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_results.empty())
        return false;
    message = std::move(m_results.front().first);
    ok = m_results.front().second;
    m_results.pop_front();
    return true;
}

static bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void Saver::worker()
{
    std::map<std::string, std::unique_ptr<HttpClient>> clients;
    const HttpHeaders headers = {{"User-Agent", discord::Identity::get().userAgent()}, {"Accept", "image/*"}};
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running || !m_jobs.empty(); });
            if (!m_running)
                break;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }
        auto finish = [&](std::string msg, bool ok) {
            logf("saver: %s", msg.c_str());
            std::lock_guard<std::mutex> lock(m_mutex);
            m_results.emplace_back(std::move(msg), ok);
            m_busy = !m_jobs.empty();
        };

        std::string host, path;
        if (!discord::cdn::splitImageUrl(job.url, host, path)) {
            finish("Can't save a picture from there.", false);
            continue;
        }
        auto fetch = [&](const std::string &p) {
            std::unique_ptr<HttpClient> &http = clients[host];
            if (!http)
                http = std::make_unique<HttpClient>(host);
            return http->request("GET", p, headers);
        };
        HttpResponse res = fetch(path);
        std::string ext = res.status == 200 ? imageutil::sniffExtension(res.body) : "";
        bool proxy = host.find("discordapp.net") != std::string::npos;
        if (res.status == 200 && (ext == "webp" || ext.empty()) && proxy) {
            HttpResponse png = fetch(path + (path.find('?') == std::string::npos ? "?" : "&") + "format=png");
            if (png.status == 200 && imageutil::sniffExtension(png.body) == "png") {
                res = std::move(png);
                ext = "png";
            }
        }
        if (res.status != 200) {
            finish("Couldn't download the picture (" +
                       (res.status ? "HTTP " + std::to_string(res.status) : std::string("network error")) + ").",
                   false);
            continue;
        }
        if (ext.empty()) {
            finish("That isn't a picture OunceCord can save.", false);
            continue;
        }
        if (res.body.size() > kMaxBytes) {
            finish("That picture is too big to save.", false);
            continue;
        }

        config::ensureDir();
        mkdir(config::kSavedDir, 0777);
        std::string base = discord::safeFilename(job.name.empty() ? "image" : job.name);
        std::string name = imageutil::withExtension(base, ext);
        std::string stem = name.substr(0, name.size() - ext.size() - 1);
        std::string file = std::string(config::kSavedDir) + "/" + name;
        for (int n = 2; fileExists(file) && n < 1000; n++) {
            name = stem + "-" + std::to_string(n) + "." + ext;
            file = std::string(config::kSavedDir) + "/" + name;
        }
        FILE *f = fopen(file.c_str(), "wb");
        bool ok = f && fwrite(res.body.data(), 1, res.body.size(), f) == res.body.size();
        if (f)
            fclose(f);
        if (!ok) {
            remove(file.c_str());
            finish("Couldn't write to the SD card.", false);
            continue;
        }
        finish("Saved " + name, true);
    }
    for (auto &c : clients)
        c.second->close();
}
