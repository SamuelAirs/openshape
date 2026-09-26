// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/PreviewWorker.h"

#include "core/Log.h"

#include <cstddef>
#include <exception>
#include <thread>

namespace os::interact {

namespace {
// The kernel recurses deeply in places. Secondary threads get 512 KB of
// stack on macOS and iOS (the main thread 8 MB and 1 MB); on Windows every
// thread gets the executable's reserve, as the GUI thread does.
[[maybe_unused]] constexpr std::size_t kWorkerStackBytes = 16u * 1024u * 1024u;
} // namespace

PreviewWorker::PreviewWorker(std::function<void()> notify) : notify_(std::move(notify))
{
#if defined(_WIN32)
    thread_ = std::thread([this] { run(); });
#else
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, kWorkerStackBytes);
    started_ = pthread_create(&thread_, &attributes, &PreviewWorker::threadMain, this) == 0;
    pthread_attr_destroy(&attributes);
    if (!started_) // not expected; the default stack is better than no worker
        started_ = pthread_create(&thread_, nullptr, &PreviewWorker::threadMain, this) == 0;
    if (!started_)
        OS_LOG(Error, Interaction) << "the preview worker thread could not start";
#endif
}

#if !defined(_WIN32)
void* PreviewWorker::threadMain(void* worker)
{
    static_cast<PreviewWorker*>(worker)->run();
    return nullptr;
}
#endif

PreviewWorker::~PreviewWorker()
{
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
        waiting_.reset();
    }
    wake_.notify_all();
#if defined(_WIN32)
    if (thread_.joinable())
        thread_.join();
#else
    if (started_)
        pthread_join(thread_, nullptr);
#endif
}

void PreviewWorker::submit(Job job)
{
    {
        const std::lock_guard lock(mutex_);
        if (waiting_)
            ++jobsReplaced_;
        waiting_ = std::move(job);
    }
    wake_.notify_one();
}

void PreviewWorker::dropWaiting()
{
    bool nowIdle = false;
    {
        const std::lock_guard lock(mutex_);
        if (waiting_) {
            ++jobsReplaced_;
            waiting_.reset();
        }
        nowIdle = !running_;
    }
    if (nowIdle)
        idle_.notify_all();
}

int PreviewWorker::deliver()
{
    std::vector<Delivery> ready;
    {
        const std::lock_guard lock(mutex_);
        ready.swap(finished_);
    }
    for (Delivery& delivery : ready)
        if (delivery)
            delivery();
    return static_cast<int>(ready.size());
}

bool PreviewWorker::busy() const
{
    const std::lock_guard lock(mutex_);
    return waiting_.has_value() || running_ || !finished_.empty();
}

bool PreviewWorker::computing() const
{
    const std::lock_guard lock(mutex_);
    return waiting_.has_value() || running_;
}

bool PreviewWorker::running() const
{
    const std::lock_guard lock(mutex_);
    return running_;
}

bool PreviewWorker::waitUntilIdle(std::chrono::milliseconds timeout) const
{
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [this] { return !waiting_ && !running_; });
}

void PreviewWorker::setJobDelayForTesting(std::chrono::milliseconds delay)
{
    const std::lock_guard lock(mutex_);
    delay_ = delay;
}

std::uint64_t PreviewWorker::jobsRun() const
{
    const std::lock_guard lock(mutex_);
    return jobsRun_;
}

std::uint64_t PreviewWorker::jobsReplaced() const
{
    const std::lock_guard lock(mutex_);
    return jobsReplaced_;
}

void PreviewWorker::run()
{
    // Kernel timings logged from here are off the GUI thread.
    setThreadLogPrefix("[worker] ");
    for (;;) {
        Job job;
        std::chrono::milliseconds delay{0};
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || waiting_.has_value(); });
            if (stop_)
                return;
            job = std::move(*waiting_);
            waiting_.reset();
            running_ = true;
            delay = delay_;
        }
        if (delay.count() > 0)
            std::this_thread::sleep_for(delay);
        Delivery delivery;
        try {
            delivery = job();
        } catch (const std::exception& e) {
            // Jobs report their own failures; this is a bug, not a refused value.
            OS_LOG(Error, Interaction) << "preview job threw: " << e.what();
        } catch (...) {
            OS_LOG(Error, Interaction) << "preview job threw an unknown exception";
        }
        bool deliverNow = false;
        {
            const std::lock_guard lock(mutex_);
            running_ = false;
            ++jobsRun_;
            if (delivery && !stop_) {
                finished_.push_back(std::move(delivery));
                deliverNow = true;
            }
        }
        idle_.notify_all();
        if (deliverNow && notify_)
            notify_();
    }
}

} // namespace os::interact
