// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "sketch/Sketch.h"

#include <vector>

namespace os::doc {
class Document;
}

namespace os::interact {

// The model geometry lying in a sketch's plane, in sketch coordinates: the
// outline of the face a sketch is drawn on (and of any other visible body's
// faces and edges in that plane), as snap targets and faint reference marks.
// Like the origin it is never part of a profile, never extruded and never
// saved: it is recomputed from the bodies, so it follows the face when the
// body changes. A point snapped to it gets its position, no constraint.
struct SketchReferences {
    struct Curve {
        enum class Kind { Line, Circle, Arc, Polyline } kind = Kind::Line;
        Vec2 start, end;          // Line, Arc (counterclockwise from start to end)
        Vec2 center;              // Circle, Arc
        double radius = 0;        // Circle, Arc
        std::vector<Vec2> points; // Polyline
    };
    // Corner: a vertex; Midpoint: the middle of a straight edge or an arc;
    // Center: of a flat face, a hole (a circle) or an arc.
    enum class PointKind { Corner, Midpoint, Center };
    struct Point {
        Vec2 position;
        PointKind kind = PointKind::Corner;
    };
    std::vector<Curve> curves;
    std::vector<Point> points;

    bool empty() const { return curves.empty() && points.empty(); }
    // The nearest point on a curve to `p` (sketch coordinates), and which curve; false when there are none.
    bool nearestOnCurve(Vec2 p, Vec2& on, std::size_t& curve) const;
};

// The visible bodies' geometry lying in `plane` (a kernel query per body the
// plane touches; bodies it misses cost a bounding-box test).
SketchReferences sketchReferences(const doc::Document& document, const sketch::Plane& plane);

} // namespace os::interact
