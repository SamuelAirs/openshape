// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/Fold.h"

#include "core/Math.h"

#include <algorithm>
#include <cmath>

namespace os::measure {

double hingeDegrees(double radians)
{
    if (!std::isfinite(radians))
        return 0;
    return std::clamp(radians * 180.0 / kPi, 0.0, 360.0);
}

FoldAngles foldAngles(double hingeRadians)
{
    const double a = hingeDegrees(hingeRadians);
    return {a, 180.0 - a};
}

double caliperGapMm(double hingeRadians, double armMm)
{
    const double a = std::clamp(hingeRadians, 0.0, kPi);
    return 2.0 * armMm * std::sin(a * 0.5);
}

std::optional<double> armFromKnownGap(double hingeRadians, double gapMm)
{
    const double s = std::sin(std::clamp(hingeRadians, 0.0, kPi) * 0.5);
    if (s < std::sin(5.0 * kPi / 360.0) || !(gapMm > 0)) // under 5 degrees: too small to tell
        return std::nullopt;
    return gapMm / (2.0 * s);
}

double caliperMmPerDegree(double hingeRadians, double armMm)
{
    // d/dθ (2 L sin(θ/2)) = L cos(θ/2), per radian.
    return armMm * std::cos(std::clamp(hingeRadians, 0.0, kPi) * 0.5) * kPi / 180.0;
}

SteadyReading::SteadyReading(double steadyRange, double steadySeconds, double smoothingSeconds)
    : steadyRange_(steadyRange), steadySeconds_(steadySeconds), smoothingSeconds_(smoothingSeconds)
{
}

void SteadyReading::reset()
{
    smoothed_.reset();
    window_.clear();
    held_.reset();
    armed_ = true;
}

SteadyReading::State SteadyReading::feed(double value, double seconds)
{
    State state;
    if (!std::isfinite(value)) {
        state.value = smoothed_.value_or(0);
        state.held = held_;
        return state;
    }
    if (!smoothed_) {
        smoothed_ = value;
    } else {
        // Exponential smoothing with a time constant, whatever the rate.
        const double dt = std::max(0.0, seconds - lastTime_);
        const double k = smoothingSeconds_ > 0 ? 1.0 - std::exp(-dt / smoothingSeconds_) : 1.0;
        *smoothed_ += (value - *smoothed_) * k;
    }
    lastTime_ = seconds;
    window_.emplace_back(seconds, *smoothed_);
    while (!window_.empty() && window_.front().first < seconds - steadySeconds_ - 0.5)
        window_.pop_front();

    // Steady: the window covers the whole steady time and stays in range.
    double lo = *smoothed_, hi = *smoothed_;
    for (const auto& [t, v] : window_) {
        if (t < seconds - steadySeconds_)
            continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    bool covered = false;
    for (const auto& [t, v] : window_)
        if (t <= seconds - steadySeconds_) {
            covered = true;
            break;
        }
    state.steady = covered && hi - lo <= steadyRange_;

    // A move away from the hold re-arms it; the next steady stretch holds.
    if (held_ && std::abs(*smoothed_ - *held_) > 3 * steadyRange_)
        armed_ = true;
    if (state.steady && armed_) {
        held_ = *smoothed_;
        armed_ = false;
        state.newHold = true;
    }
    state.value = *smoothed_;
    state.held = held_;
    return state;
}

} // namespace os::measure
