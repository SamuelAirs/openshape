// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Log.h"

#include <chrono>
#include <string>

namespace os {

// Logs the wall-clock duration of a scope under the PERFORMANCE category.
// Cheap enough to leave in release builds around kernel/tessellation calls.
class ScopedTimer {
public:
    explicit ScopedTimer(std::string label) : label_(std::move(label)), start_(Clock::now()) {}
    ~ScopedTimer()
    {
        OS_LOG(Debug, Performance) << label_ << " took " << elapsedMs() << " ms";
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    double elapsedMs() const
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    std::string label_;
    Clock::time_point start_;
};

} // namespace os
