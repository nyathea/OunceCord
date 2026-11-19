// SPDX-License-Identifier: MIT
//
// Saves pictures from chat into config::kSavedDir, downloading the full-size
// original on a worker thread. WebP (which the picker can't preview) is
// asked for as PNG instead.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class Saver {
public:
    static constexpr size_t kMaxBytes = 50u << 20;

    void start();
    void stop();
    // url: an image URL on a host the image cache may fetch. name: a file
    // name to start from (the extension is fixed to match the data).
    void save(const std::string &url, const std::string &name);
    bool busy() const { return m_busy; }
    // Messages for the UI ("Saved cat.png"), one at a time.
    bool takeResult(std::string &message, bool &ok);

private:
    struct Job {
        std::string url, name;
    };
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    std::deque<std::pair<std::string, bool>> m_results;
    std::thread m_thread;
    std::atomic<bool> m_running{false}, m_busy{false};

    void worker();
};
