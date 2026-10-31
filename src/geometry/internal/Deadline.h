// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions of this file are adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE).

#pragma once

// Private to the geometry library: a time limit for one kernel algorithm.

#include "core/Log.h"
#include "core/Result.h"
#include "geometry/KernelSignals.h"

#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>

#include <atomic>
#include <chrono>
#include <string>

namespace os::geom::detail {

// What the user reads when a kernel algorithm ran into its time limit.
inline constexpr const char* kTookTooLongMessage = "This took too long and was stopped.";

// A progress indicator that only answers "stop now?": once the time limit
// of the calling thread (kernelTimeLimit()) has passed, OCCT algorithms that
// check their progress range give up. Pass `deadline->Start()` as the range
// of one algorithm, then ask fired(): true means the algorithm was told to
// stop, so its result (if any) is incomplete and must not be used. A result
// that finished after the limit without being told to stop is complete and
// kept. Held by a Handle (OCCT reference counting). Adapted from
// OS3DDeadlineProgress; the limit comes from the thread (the preview worker
// gets a shorter one) and the clock is std::chrono::steady_clock.
class KernelDeadline : public Message_ProgressIndicator {
public:
    KernelDeadline() : deadline_(std::chrono::steady_clock::now() + kernelTimeLimit()) {}

    bool fired() const { return fired_.load(std::memory_order_relaxed); }

    // OCCT may ask from several of its worker threads at once.
    Standard_Boolean UserBreak() override
    {
        if (!fired_.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() >= deadline_)
            fired_.store(true, std::memory_order_relaxed);
        return fired_.load(std::memory_order_relaxed);
    }

    void Show(const Message_ProgressScope&, const Standard_Boolean) override {}

private:
    std::chrono::steady_clock::time_point deadline_;
    std::atomic<bool> fired_{false};
};

// The failure for an algorithm stopped by its deadline (logged as a warning:
// a real model that needs longer is worth knowing about).
template <typename R>
R tookTooLong(const std::string& operation)
{
    const std::string dev = operation + " stopped by its time limit ("
                          + std::to_string(kernelTimeLimit().count()) + " ms)";
    OS_LOG(Warning, Kernel) << dev;
    return R::failure(ErrorCode::KernelFailure, kTookTooLongMessage, dev);
}

// True for a failure made by tookTooLong (callers skip their retries and
// size searches: the next attempt would take as long).
template <typename R>
bool stoppedByTimeLimit(const R& result)
{
    return !result.ok() && result.userMessage() == kTookTooLongMessage;
}

} // namespace os::geom::detail
