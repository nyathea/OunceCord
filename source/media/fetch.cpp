// SPDX-License-Identifier: MIT
#include "media/fetch.h"

#include <thread>

#include "discord/cdn.h"
#include "discord/rest.h"
#include "net/http.h"
#include "util/util.h"

VideoFetch::~VideoFetch()
{
    cancel();
    reap(true);
}

void VideoFetch::reap(bool wait)
{
    for (size_t i = 0; i < m_jobs.size();) {
        bool done;
        {
            std::lock_guard<std::mutex> lock(m_jobs[i].state->mutex);
            done = m_jobs[i].state->done;
        }
        if (done || wait) {
            if (m_jobs[i].thread.joinable())
                m_jobs[i].thread.join();
            m_jobs.erase(m_jobs.begin() + i);
        } else {
            i++;
        }
    }
}

void VideoFetch::start(const std::string &url)
{
    cancel();
    reap(false);
    auto state = std::make_shared<State>();
    m_state = state;
    std::thread thread([state, url] {
        std::string host, path, error, data;
        if (!discord::cdn::splitImageUrl(url, host, path)) {
            error = "Can't play a video from there.";
        } else {
            HttpClient http(host);
            HttpResponse res = http.request("GET", path, {{"User-Agent", discord::Identity::get().userAgent()}});
            if (res.status != 200)
                error = "Couldn't download the video (" +
                        (res.status ? "HTTP " + std::to_string(res.status) : "network error: " + res.error) + ").";
            else if (res.body.size() > kMaxBytes)
                error = "That video is too big to play here.";
            else
                data = std::move(res.body);
            logf("fetch: %s: %s, %zu bytes", host.c_str(), error.empty() ? "ok" : error.c_str(), data.size());
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data = std::move(data);
        state->error = std::move(error);
        state->done = true;
    });
    m_jobs.push_back({std::move(thread), state});
}

void VideoFetch::cancel()
{
    m_state.reset();
    reap(false);
}

bool VideoFetch::take(std::string &data, std::string &error)
{
    std::shared_ptr<State> state = m_state;
    if (!state)
        return false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->done)
            return false;
        data = std::move(state->data);
        error = std::move(state->error);
    }
    m_state.reset();
    reap(false);
    return true;
}
