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

} // namespace os::measure
