// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <deque>
#include <optional>
#include <utility>

// The iPhone Duo's fold as a measuring tool (docs/MEASURE.md, "Fold"). UIKit
// reports the hinge's angle (UIHinge.angle: radians, 0 closed, pi flat), so
// the open phone is a protractor: rest its halves on two faces (inside a
// corner, or over an edge with the screens on the faces) and the hinge reads
// the angle between them. Pinch something between the two free edges and
// the gap there follows from the angle and the length of a half: a caliper.
// The readings are smoothed and held when steady, because a hand moves and
// the screen may face the part, not the reader.
namespace os::measure {

// Degrees from the hinge's radians, kept in 0..360.
double hingeDegrees(double radians);

// What an angle between the halves means: the angle itself (inside a corner,
// or over an edge) and the bend (how far from flat: 180 - angle), which is
// what a sheet-metal bend or a sketch's turn is given as.
struct FoldAngles {
    double angleDeg = 0;
    double bendDeg = 0;
};
FoldAngles foldAngles(double hingeRadians);

// The gap between the free edges of two halves `armMm` long (from the hinge
// line to the edge) opened to the angle: 2 L sin(angle / 2).
double caliperGapMm(double hingeRadians, double armMm);
// The arm length that makes a known gap at an angle (calibration: pinch a
// card's side); nullopt when the angle is too small to tell.
std::optional<double> armFromKnownGap(double hingeRadians, double gapMm);
// How far an angle error of one degree moves the gap there (mm): the
// caliper's honesty, shown beside its reading.
double caliperMmPerDegree(double hingeRadians, double armMm);

// Smooths a stream of readings (an angle in degrees, from the hinge or the
// level) and says when it has been steady: a reading that moves less than
// `steadyRange` over `steadySeconds` is steady, and the first steady value
// after a move is the hold (the value shown once the phone is lifted to be
// read).
class SteadyReading {
public:
    explicit SteadyReading(double steadyRange = 0.4, double steadySeconds = 0.8, double smoothingSeconds = 0.15);

    struct State {
        double value = 0;   // smoothed
        bool steady = false;
        bool newHold = false; // this reading made a new hold
        std::optional<double> held;
    };

    // `seconds`: a steady clock (any origin), increasing.
    State feed(double value, double seconds);
    void reset();
    // Drops the hold (a new measurement starts).
    void clearHold() { held_.reset(); armed_ = true; }
    const std::optional<double>& held() const { return held_; }

private:
    double steadyRange_;
    double steadySeconds_;
    double smoothingSeconds_;
    std::optional<double> smoothed_;
    double lastTime_ = 0;
    std::deque<std::pair<double, double>> window_; // (time, smoothed value)
    std::optional<double> held_;
    bool armed_ = true; // the next steady stretch makes a hold
};

} // namespace os::measure
