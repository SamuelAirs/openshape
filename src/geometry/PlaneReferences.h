// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "geometry/Profiles.h"
#include "geometry/Shape.h"

#include <vector>

// What of a body lies in a plane: the reference geometry a sketch on that
// plane snaps to (a face's corners, edge middles, center and holes).
namespace os::geom {

// An edge lying in the plane (world coordinates): straight lines, circles
// and circular arcs exactly, any other curve as a polyline.
struct PlaneEdge {
    enum class Kind { Line, Circle, Arc, Polyline } kind = Kind::Line;
    Vec3 start, end;          // Line, Arc: an Arc runs counterclockwise about the plane normal from start to end
    Vec3 center;              // Circle, Arc
    double radius = 0;        // Circle, Arc
    std::vector<Vec3> points; // Polyline
};

struct PlaneReferences {
    std::vector<PlaneEdge> edges;
    std::vector<Vec3> vertices;    // the vertices lying in the plane (edge ends, a cone's tip)
    std::vector<Vec3> faceCenters; // area centroids of the flat faces lying in the plane
    bool empty() const { return edges.empty() && vertices.empty() && faceCenters.empty(); }
};

// The edges, vertices and flat faces of `shape` that lie in `plane` (within
// `tolerance` mm). Empty for a shape the plane does not touch (a bounding
// box test first) or on a kernel failure (logged). Takes the kernel lock.
PlaneReferences planeReferences(const Shape& shape, const PlaneFrame& plane, double tolerance = 1e-3);

} // namespace os::geom
