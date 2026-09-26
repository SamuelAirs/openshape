// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <optional>
#include <vector>

// Drilled holes (plain, counterbored, countersunk) and the queries they need.
// Lengths in millimeters, angles in radians.
namespace os::geom {

enum class HoleHead { None, Counterbore, Countersink };

// One round hole, drilled from `entry` (a point on the surface) along
// `direction` (into the material).
struct HoleCut {
    Vec3 entry;
    Vec3 direction{0, 0, -1};
    double diameter = 0; // the hole
    // false: the hole is already there (only its head is cut, e.g. a
    // counterbore on an existing hole); `diameter` is then that hole's.
    bool drillShaft = true;
    double depth = 0;         // shaft depth from the entry surface (ignored when throughAll)
    bool throughAll = false;  // the shaft goes through everything behind the entry
    HoleHead head = HoleHead::None;
    double headDiameter = 0;  // counterbore / countersink diameter at the surface
    double headDepth = 0;     // counterbore depth
    double headAngle = 0;     // countersink included angle (90 degrees for metric screws)
};

// How deep a countersink of `headDiameter` over a hole of `holeDiameter` goes.
double countersinkDepth(double headDiameter, double holeDiameter, double angle);
// How deep the head (counterbore or countersink) of a hole reaches; 0 without one.
double headReach(const HoleCut& hole);
// The volume a hole's head removes from solid material around its (already
// empty) shaft: the counterbore ring pi (D^2 - d^2) / 4 h, or the countersink
// cone frustum minus the shaft inside it.
double headVolume(const HoleCut& hole);

// From `point` on the part's surface straight along `direction` (into the
// material): how far until the line leaves the material. nullopt when the
// line does not start into material there (e.g. beside the part).
std::optional<double> materialDepth(const Shape& shape, const Vec3& point, const Vec3& direction);

// A flat face seen in a frame on its plane (u along xAxis, v along yAxis):
// its bounding rectangle, and where its straight edges' middles and its
// circles' centers are (world points), for placing holes on it.
struct FaceOutline {
    bool valid = false;
    double minU = 0, minV = 0, maxU = 0, maxV = 0;
    std::vector<Vec3> edgeMidpoints; // straight edges
    std::vector<Vec3> circleCenters; // circular edges (holes, round outlines)
};
FaceOutline faceOutline(const Shape& shape, int faceIndex, const Vec3& origin, const Vec3& xAxis, const Vec3& yAxis);
// Whether `point` (on the face's plane) lies on the face (inside or on its
// boundary, not in one of its holes).
bool faceContains(const Shape& shape, int faceIndex, const Vec3& point);

// Cuts all holes in one boolean. Fails with a plain message for impossible
// sizes (a head narrower than its hole, a countersink deeper than a blind
// hole) and when the result is unchanged; callers check what else the
// intent implies (e.g. the exact volume a head removes).
Result<Shape> drillHoles(const Shape& shape, const std::vector<HoleCut>& holes);

} // namespace os::geom
