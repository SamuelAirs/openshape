// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "measure/ObjectOutline.h"
#include "measure/StrokeFit.h"

#include <vector>

// A found outline as points the owner can drag (docs/MEASURE.md, "Find
// outline"): a loop of points where a point marked `through` is the middle
// of an arc from the point before it to the point after it (three points fix
// an arc, so dragging any of them keeps a clean arc); the others are corners
// joined by lines. A circle is two half arcs. From such loops: the drawn
// path, the report (each side, width x height, area, holes) and the sketch.
namespace os::measure {

struct ProfileLoop {
    std::vector<Vec2> points;
    std::vector<bool> through; // same size as points; never two in a row, never all
};

// The loop's points: each piece's start, and an arc's middle (through).
ProfileLoop profileLoopOf(const OutlineLoop& loop);

// The pieces again (lines, and arcs through three points); a `through`
// point whose three points line up gives two lines.
std::vector<FittedSegment> profileSegments(const ProfileLoop& loop);
// Whether the loop is one circle: two arcs (or more) on the same circle
// within `tolerance`; the circle in `circle`.
bool profileIsCircle(const ProfileLoop& loop, double tolerance, Circle* circle = nullptr);

// The closed path with arcs as short chords (at most `degreesPerStep` of an
// arc each), not repeating the first point.
std::vector<Vec2> profilePolyline(const ProfileLoop& loop, double degreesPerStep = 4);

// The signed area enclosed (shoelace with arcs; positive counterclockwise
// in a y-up frame).
double profileSignedArea(const ProfileLoop& loop);

// The angle (radians) that turns the loop's longest straight side onto the
// x axis (0 for a loop with no line).
double profileAlignment(const ProfileLoop& loop);

struct ProfileReport {
    struct Side {
        double length = 0; // along the line or the arc
        bool arc = false;
        double radius = 0;
        Vec2 middle; // the middle of the side (the loop's units, not scaled)
    };
    struct Hole {
        bool circle = false;
        double diameter = 0;     // a round hole
        double width = 0;        // any hole: its size along the aligned axes
        double height = 0;
        Vec2 center;             // the loop's units
    };
    std::vector<Side> sides;
    double perimeter = 0;
    double area = 0; // the outer loop's minus the holes'
    // The overall size along the part's own axes (its longest straight
    // side along the width), and that angle (radians, in the loop's frame).
    double width = 0;
    double height = 0;
    double angle = 0;
    bool circle = false; // the outer edge is one circle
    double diameter = 0;
    std::vector<Hole> holes;
};

// Lengths and areas are scaled by `scale` (millimetres per unit of the
// points: a straightened photo's mm per pixel).
ProfileReport describeProfile(const ProfileLoop& outer, const std::vector<ProfileLoop>& holes, double scale, double circleTolerance);

} // namespace os::measure
