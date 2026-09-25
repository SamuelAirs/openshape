// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <map>
#include <vector>

namespace os::interact {

// One touch point of a multi-touch frame, as the platform reports it.
struct TouchPoint {
    int id = 0;
    Vec2 position; // logical pixels in the viewport
    enum class State { Pressed, Moved, Stationary, Released } state = State::Moved;
};

// What a touch sequence means for the application.
struct TouchIntent {
    enum class Kind {
        PointerPress,   // one finger went down (may become a tap, a drag or a manipulator drag)
        PointerMove,
        PointerRelease,
        PointerCancel,  // a second finger arrived: the one-finger press is not a tap
        DoubleTap,
        Pan,            // two fingers moved: from -> to (centroids)
        Pinch,          // two fingers spread: scale > 1 zooms in, about `position`
        Undo,           // a quick two-finger tap
        Redo,           // a quick three-finger tap
    };
    Kind kind = Kind::PointerMove;
    Vec2 position;
    Vec2 from, to;
    double scale = 1.0;
};

// Turns raw touch frames into intents, Shapr3D/Procreate style: one finger
// points (tap, drag, double-tap), two fingers navigate (pan and pinch once
// they really move), and a quick tap with two or three fingers undoes or
// redoes. Framework-independent so it is unit-testable and portable (iPad).
class TouchGestureRecognizer {
public:
    // Feeds one frame: every point currently on the screen (released points
    // included in the frame they lift). `seconds` is a monotonic timestamp.
    std::vector<TouchIntent> update(const std::vector<TouchPoint>& points, double seconds);

    // Thresholds (logical pixels / seconds).
    static constexpr double kTapMovement = 12;     // a tap moves less than this
    static constexpr double kTapDuration = 0.35;   // and lifts within this
    static constexpr double kDoubleTapGap = 0.35;
    static constexpr double kDoubleTapDistance = 24;
    static constexpr double kNavigateStart = 10;   // two fingers move this far before navigating

private:
    void reset();

    // Sequence state: from the first finger down until all fingers lift.
    bool active_ = false;
    double startTime_ = 0;
    int maxFingers_ = 0;
    bool moved_ = false;       // any finger went past the tap movement
    bool pointerDown_ = false; // a one-finger press is in progress
    bool navigating_ = false;  // two-finger pan/pinch started
    bool twoPhase_ = false;    // two or more fingers are down right now
    std::map<int, Vec2> start_; // first position of every finger in the sequence
    Vec2 lastCentroid_;
    double lastSpan_ = 0;
    Vec2 navStartCentroid_;    // where the current two-finger phase began
    double navStartSpan_ = 0;
    // Double-tap detection across sequences.
    double lastTapTime_ = -1e9;
    Vec2 lastTapPosition_;
};

} // namespace os::interact
