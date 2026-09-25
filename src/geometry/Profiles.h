// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <vector>

// Closed-profile detection for sketches and extrusion of profiles.
namespace os::geom {

struct PlaneFrame {
    Vec3 origin;
    Vec3 xAxis{1, 0, 0};
    Vec3 yAxis{0, 1, 0};
    Vec3 normal() const { return xAxis.cross(yAxis); }
};

// A curve lying in the plane (world coordinates).
struct PlanarCurve {
    enum class Kind { Segment, Circle, Arc } kind = Kind::Segment;
    Vec3 start, end;   // Segment, Arc
    Vec3 center;       // Circle, Arc
    double radius = 0; // Circle, Arc
    // An Arc runs counterclockwise about the plane normal from start to end.
};

// A bounded region enclosed by the curves: one candidate profile.
struct Region {
    Shape face;
    double area = 0;
    Vec3 centroid;
    Vec3 interiorPoint; // guaranteed inside the region (centroid may not be)
};

// Finds every bounded region of the planar arrangement formed by the curves.
// Nested loops produce separate regions (a circle inside a rectangle gives
// the plate-with-hole and the disc). Curves that do not close anything are
// ignored. Regions are sorted by decreasing area.
Result<std::vector<Region>> findRegions(const PlaneFrame& plane, const std::vector<PlanarCurve>& curves);

// True if `point` (world, on the plane) lies inside the region face.
bool regionContains(const Shape& face, const Vec3& point);

// Offsets one connected chain (or closed loop) of curves in the plane by
// |distance|, to one side or the other by its sign (which side depends on
// the chain; callers pick the result nearest to where the user points).
// Corners between lines stay sharp; arcs keep their centers. Fails when the
// curves are not one connected chain or the offset collapses.
Result<std::vector<PlanarCurve>> offsetCurves(const PlaneFrame& plane, const std::vector<PlanarCurve>& curves,
                                              double distance);

// Sweeps faces along `vector` and fuses the prisms into one shape.
Result<Shape> extrudeFaces(const std::vector<Shape>& faces, const Vec3& vector);

// Revolves faces around an axis (in their plane) by `angle` radians and fuses
// the results. Fails if a face crosses the axis.
Result<Shape> revolveFaces(const std::vector<Shape>& faces, const Vec3& axisOrigin, const Vec3& axisDirection,
                           double angle);

} // namespace os::geom
