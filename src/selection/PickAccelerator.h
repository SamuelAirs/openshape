// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "geometry/Mesh.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace os::sel {

// Axis-aligned box in world coordinates.
struct Box3 {
    Vec3 min{1e300, 1e300, 1e300};
    Vec3 max{-1e300, -1e300, -1e300};
    bool empty() const { return min.x > max.x; }
    void add(const Vec3& p);
    void add(const Box3& b);
};

// Bounding-volume hierarchies over a display mesh's triangles and edge
// segments, built once per mesh (meshes never change after tessellation:
// a new shape gets a new mesh and a new accelerator). Queries return the
// same answers as testing every triangle and segment:
// - nearestHit: the smallest ray parameter, ties going to the lowest
//   triangle index (the first one a linear scan would keep);
// - segmentsNear: every edge segment that could project within a pixel
//   radius of the cursor (a conservative superset), in mesh order, so the
//   caller's linear logic runs unchanged on fewer segments.
class PickAccelerator {
public:
    explicit PickAccelerator(const geom::Mesh& mesh);

    // Bounds of all mesh vertices (what the linear scan tests first).
    const Box3& vertexBounds() const { return vertexBounds_; }
    // True when built from a mesh of this size (a guard against mismatches).
    bool matches(const geom::Mesh& mesh) const;

    struct Hit {
        double t = 0;
        std::uint32_t triangle = 0;
    };
    std::optional<Hit> nearestHit(const geom::Mesh& mesh, const Ray& ray) const;

    // An edge segment: points [point, point + 1] of mesh.edges[edge].
    struct Segment {
        std::uint32_t edge = 0;
        std::uint32_t point = 0; // index of the first point (not the float offset)
    };
    // Segments whose boxes come within `radius(box)` of the infinite line
    // through `ray`, sorted by (edge, point). `radius` gives, for a box, an
    // upper bound of how far from the line a point in it may be and still be
    // picked (the pixel tolerance times the pixel size at the box's far
    // side); a negative value rejects the box.
    std::vector<Segment> segmentsNear(const Ray& ray, const std::function<double(const Box3&)>& radius) const;

    std::size_t triangleNodeCount() const { return triangleNodes_.size(); }
    std::size_t segmentNodeCount() const { return segmentNodes_.size(); }

private:
    struct Node {
        Box3 box;
        std::uint32_t first = 0; // leaf: first item in the order array; inner: left child (right = left + 1)
        std::uint32_t count = 0; // items in a leaf; 0 for an inner node
    };
    static void build(std::vector<Node>& nodes, std::vector<std::uint32_t>& order, const std::vector<Box3>& boxes);

    Box3 vertexBounds_;
    std::size_t triangleCount_ = 0;
    std::size_t segmentTotal_ = 0;
    double padding_ = 0; // added to every box: keeps rounding from pruning a real hit
    std::vector<Node> triangleNodes_;
    std::vector<std::uint32_t> triangleOrder_;
    std::vector<Node> segmentNodes_;
    std::vector<Segment> segments_;           // all segments, in mesh order
    std::vector<std::uint32_t> segmentOrder_; // indices into segments_
};

// Ray parameter interval [tNear, tFar] where the line p + t*d meets the box
// grown by `pad`; nullopt when it misses. With `rayOnly`, t < 0 is excluded.
std::optional<std::pair<double, double>> lineBoxInterval(const Ray& ray, const Box3& box, double pad, bool rayOnly);

} // namespace os::sel
