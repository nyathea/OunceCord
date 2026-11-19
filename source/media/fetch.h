// SPDX-License-Identifier: MIT
//
// Downloads one video into memory on a background thread. Cancelling just
// stops caring: the thread finishes on its own, its result is dropped, and
// it is joined once done. (Threads can't be detached here: libounce has no
// thread detach, so pthread_detach crashes.)
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class VideoFetch {
public:
    static constexpr size_t kMaxBytes = 200u << 20;

    ~VideoFetch();
    void start(const std::string &url);
    void cancel();
    bool active() const { return m_state != nullptr; }
    // True once finished: data on success, error otherwise.
    bool take(std::string &data, std::string &error);

private:
    struct State {
        std::mutex mutex;
        bool done = false;
        std::string data, error;
    };
    struct Job {
        std::thread thread;
        std::shared_ptr<State> state;
    };
    std::shared_ptr<State> m_state;  // the current download
    std::vector<Job> m_jobs;         // its thread, and cancelled ones still running

    void reap(bool wait);  // joins finished threads (all of them if wait)
};
