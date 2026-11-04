// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

// The phone as a level and an inclinometer (docs/MEASURE.md, "Level"), from
// gravity as the motion sensors report it, turned into the screen's frame by
// the platform layer: x to the right of the screen as it is shown, y down
// it, z out of the glass toward the viewer; in units of g (lying face up on
// a level table: (0, 0, -1)).
namespace os::measure {

struct Gravity {
    double x = 0;
    double y = 0;
    double z = -1;
};

struct Tilt {
    // Lying (nearer flat than upright): the bubble level.
    bool lying = true;
    // Lying: how far the glass is from level, the surface's slope (degrees),
    // and the bubble's place (-1..1 each way at `bubbleRangeDeg`; +x right,
    // +y down the screen, as a real bubble drifts to the high side).
    double slopeDeg = 0;
    Vec2 bubble;
    // Standing on an edge: the angle of the screen's bottom edge from
    // level (degrees, positive when the right end is higher), what a
    // carpenter's level reads when held against a surface.
    double edgeDeg = 0;
};

Tilt tiltFrom(const Gravity& g, double bubbleRangeDeg = 10.0);

// The angle between two resting positions (degrees, 0..180): lay the phone
// on one surface and keep it, lay it on another the same way round, and this
// is the angle between the surfaces.
double angleBetween(const Gravity& a, const Gravity& b);

// The edge angle relative to a kept one, in -180..180 (degrees).
double relativeEdgeAngle(double edgeDeg, double referenceEdgeDeg);

// ---- Calibration: the end-for-end (flip) method (docs/MEASURE.md, "Level:
// calibrating and checking"). A phone's motion sensors read a little off
// true level. Lay it on any steady surface and keep the reading, turn it half
// a turn in the same spot and keep it again: the surface's slope turns over
// with the phone and the sensor's offset does not, so the offset is half the
// sum of the two readings and the slope half their difference (on a 2° slope
// with a 0.5° offset the readings are 2.5° and -1.5°: offset 0.5°, slope 2°).
// No level surface is needed.

// Lying: the tilt as two angles (degrees): about the screen's y axis (the x
// component, atan(x / |z|)) and about its x axis (the y component).
Vec2 lyingAngles(const Gravity& g);
// The unit gravity with those angles; `faceDown` puts it out of the glass.
Gravity fromLyingAngles(Vec2 anglesDeg, bool faceDown = false);

struct FlipResult {
    Vec2 offsetDeg; // lying: both components; standing: x only
    Vec2 slopeDeg;  // the surface under the first reading
    double offsetSize() const { return offsetDeg.length(); }
    double slopeSize() const { return slopeDeg.length(); }
};
// Two lying readings, the second turned half a turn about the surface's
// normal from the first.
FlipResult lyingFlip(const Gravity& first, const Gravity& second);
// Two edge readings (degrees), the second with the phone turned end for end
// on the same edge.
FlipResult edgeFlip(double firstEdgeDeg, double secondEdgeDeg);

// A lying reading with an offset (lyingFlip's) taken off.
Gravity correctedLying(const Gravity& g, Vec2 offsetDeg);

// Gravity in the window's frame (Gravity's, turned with the interface) and in
// the device's own upright frame (the same axes, as when held upright): an
// offset belongs to the device, so it is kept in the upright frame and turned
// with the window. `turn` counts the window's quarter turns: 0 upright, 1
// landscape right (the device's bottom on the right), 2 upside down, 3
// landscape left.
Gravity windowToUpright(const Gravity& g, int turn);
Gravity uprightToWindow(const Gravity& g, int turn);

} // namespace os::measure
