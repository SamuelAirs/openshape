// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/PalmRejection.h"

namespace os::interact {

void PalmRejection::penDown(double)
{
    penDown_ = true;
    rejected_.insert(live_.begin(), live_.end());
}

void PalmRejection::penUp(double seconds)
{
    penDown_ = false;
    penUpTime_ = seconds;
}

bool PalmRejection::rejecting(double seconds) const
{
    return penDown_ || seconds - penUpTime_ < kGrace;
}

std::vector<TouchPoint> PalmRejection::filter(const std::vector<TouchPoint>& points, double seconds)
{
    std::vector<TouchPoint> out;
    out.reserve(points.size());
    for (const TouchPoint& p : points) {
        // A touch first seen in this frame (normally Pressed; a platform may
        // drop the press of a touch that began elsewhere) begins now.
        if (!live_.contains(p.id)) {
            live_.insert(p.id);
            if (rejecting(seconds))
                rejected_.insert(p.id);
        }
        if (!rejected_.contains(p.id))
            out.push_back(p);
        if (p.state == TouchPoint::State::Released) {
            live_.erase(p.id);
            rejected_.erase(p.id);
        }
    }
    return out;
}

void PalmRejection::touchesCancelled()
{
    live_.clear();
    rejected_.clear();
}

} // namespace os::interact
