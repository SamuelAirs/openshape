// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <optional>
#include <vector>

// A hand-drawn stroke turned into sketch geometry (docs/MEASURE.md,
// "Trace"): the Pencil or a finger run around a part lying on the screen at
// 1:1, or along a straightened photo's edge, gives a wobbly line of samples;
// this finds the few straight lines, arcs or the circle it meant, within a
// tolerance, so the result is a clean profile to extrude.
namespace os::measure {

struct Circle {
    Vec2 center;
    double radius = 0;
};

// The circle through three points; nullopt when they line up.
std::optional<Circle> circleThrough(Vec2 a, Vec2 b, Vec2 c);
// The least-squares circle through points (algebraic fit refined by a few
// geometric steps); nullopt for fewer than three or collinear points.
std::optional<Circle> fitCircle(const std::vector<Vec2>& points);

// Ramer-Douglas-Peucker: the fewest vertices of an open polyline within
// `tolerance` of every sample.
std::vector<Vec2> simplifyPolyline(const std::vector<Vec2>& points, double tolerance);

struct FitOptions {
    double tolerance = 0.5; // the largest distance from a sample to the result (sketch units, mm)
    double closeGap = 3.0;  // ends nearer than this close the shape
    bool circles = true;    // a closed, round stroke becomes one circle
    bool arcs = true;       // curved runs become arcs
};

// One piece of a fitted stroke: a line from `start` to `end`, or an arc
// from `start` to `end` passing through `through` (center and radius given
// too, for convenience).
struct FittedSegment {
    bool arc = false;
    Vec2 start;
    Vec2 end;
    Vec2 through;
    Vec2 center;
    double radius = 0;
};

struct FittedStroke {
    enum class Kind { Empty, Circle, Path };
    Kind kind = Kind::Empty;
    bool closed = false;
    Circle circle;                       // Kind::Circle
    std::vector<FittedSegment> segments; // Kind::Path, end to end (closed: the last ends at the first's start)
    double maxError = 0;                 // the largest distance from a sample to the result
};

FittedStroke fitStroke(const std::vector<Vec2>& samples, const FitOptions& options);

} // namespace os::measure
