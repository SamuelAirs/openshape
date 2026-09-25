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
    enum class Kind { Segment, Circle } kind = Kind::Segment;
    Vec3 start, end;   // Segment
    Vec3 center;       // Circle
    double radius = 0; // Circle
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

// Sweeps faces along `vector` and fuses the prisms into one shape.
Result<Shape> extrudeFaces(const std::vector<Shape>& faces, const Vec3& vector);

// Revolves faces around an axis (in their plane) by `angle` radians and fuses
// the results. Fails if a face crosses the axis.
Result<Shape> revolveFaces(const std::vector<Shape>& faces, const Vec3& axisOrigin, const Vec3& axisDirection,
                           double angle);

} // namespace os::geom
