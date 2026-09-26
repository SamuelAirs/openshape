// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace os::interact {

// One background thread for interactive previews (TD-1). The latest request
// wins: a job submitted while another still waits to start replaces it; a
// running job always finishes (a kernel call cannot be interrupted), and the
// receiver of its result decides whether it is stale. Results come back on
// the thread that calls deliver() (the GUI thread): the worker only calls
// `notify`, which the UI turns into a queued call of deliver(). Qt-free.
class PreviewWorker {
public:
    // Runs on the calling thread of deliver(), with a job's result.
    using Delivery = std::function<void()>;
    // Runs on the worker; returns what to do with its result.
    using Job = std::function<Delivery()>;

    // `notify` is called on the worker thread each time a result is ready.
    explicit PreviewWorker(std::function<void()> notify = {});
    // Drops a waiting job and waits for a running one (its result is dropped).
    ~PreviewWorker();
    PreviewWorker(const PreviewWorker&) = delete;
    PreviewWorker& operator=(const PreviewWorker&) = delete;

    void submit(Job job);
    // Drops the job waiting to start, if any.
    void dropWaiting();
    // Runs the deliveries of finished jobs on the calling thread, oldest
    // first. Returns how many ran.
    int deliver();
    // A job waits or runs, or a result waits for deliver().
    bool busy() const;
    // A job waits or runs.
    bool computing() const;
    // A job runs (it cannot be stopped any more).
    bool running() const;
    // Waits (at most `timeout`) until no job waits or runs. True when idle.
    bool waitUntilIdle(std::chrono::milliseconds timeout) const;

    // Tests: every job starts this much later (stands in for a slow kernel).
    void setJobDelayForTesting(std::chrono::milliseconds delay);
    // Jobs run, and jobs replaced before they started.
    std::uint64_t jobsRun() const;
    std::uint64_t jobsReplaced() const;

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;         // a job to run, or stop
    mutable std::condition_variable idle_; // nothing waits or runs
    std::optional<Job> waiting_;
    bool running_ = false;
    bool stop_ = false;
    std::vector<Delivery> finished_;
    std::function<void()> notify_;
    std::chrono::milliseconds delay_{0};
    std::uint64_t jobsRun_ = 0;
    std::uint64_t jobsReplaced_ = 0;
    std::thread thread_;
};

} // namespace os::interact
