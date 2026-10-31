// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "interaction/TouchGestures.h"

#include <set>
#include <vector>

namespace os::interact {

// Palm rejection for drawing with a pen (Apple Pencil): the hand that holds
// the pen rests on the screen while it draws and just after it lifts. A
// finger touch that BEGINS while a pen contact is down, or within kGrace of
// the pen lifting, is ignored for its whole life (by touch id): no press,
// no navigation, no gestures, no undo/redo taps. Touches already on the
// screen when the pen comes down are ignored from then on (the caller ends
// what they were doing: TouchGestureRecognizer::abandon). After the grace
// period fingers work as before.
//
// Works without pressure, hover or Pencil gestures (the owner's Pencil USB-C
// has none of them; the iPhone has no hover). Framework-independent: the
// caller feeds pen down/up and touch frames with timestamps from one
// monotonic clock (seconds).
class PalmRejection {
public:
    // How long after the pen lifts a new touch still counts as the hand:
    // the palm settles and lifts again between strokes within a few hundred
    // milliseconds of the pen, while reaching for the view with a finger
    // after drawing takes longer than half a second.
    static constexpr double kGrace = 0.5;

    // A pen contact began. Touches on the screen now are ignored from now on.
    void penDown(double seconds);
    // The pen contact ended (the grace period starts).
    void penUp(double seconds);
    bool penIsDown() const { return penDown_; }
    // Whether a touch beginning at `seconds` would be taken for the hand.
    bool rejecting(double seconds) const;

    // One touch frame (every point on the screen, released ones in the frame
    // they lift): the points of the touches that are not the hand.
    std::vector<TouchPoint> filter(const std::vector<TouchPoint>& points, double seconds);
    // The touch sequence was cancelled by the system: no touch is on the screen.
    void touchesCancelled();

    // Whether this touch id is being ignored (tests).
    bool ignores(int id) const { return rejected_.contains(id); }

private:
    bool penDown_ = false;
    double penUpTime_ = -1e9;
    std::set<int> live_;     // touches on the screen
    std::set<int> rejected_; // those of them that are ignored
};

} // namespace os::interact
