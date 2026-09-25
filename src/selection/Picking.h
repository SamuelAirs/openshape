// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Camera.h"
#include "core/Uuid.h"
#include "geometry/Mesh.h"

#include <memory>
#include <vector>

namespace os::sel {

enum class PickKind { None, Face, Edge, Profile };

struct PickTarget {
    Uuid bodyId;
    std::shared_ptr<const geom::Mesh> mesh;
};

struct PickResult {
    PickKind kind = PickKind::None;
    Uuid bodyId;          // body, or sketch for PickKind::Profile
    int index = -1;       // topology index (face or edge) in the body's current shape, or region index
    Vec3 point;           // world-space hit point
    double depth = 0;     // view depth of the hit
    double screenDistance = 0; // for edges: pixels from the cursor
    bool hit() const { return kind != PickKind::None; }
};

struct PickOptions {
    // Screen-space tolerance for edges in logical pixels. Touch input uses a
    // much larger value than mouse input (see interaction/InputProfile).
    double edgeTolerance = 6.0;
    bool pickFaces = true;
    bool pickEdges = true;
};

// CPU picking against display meshes. Faces: nearest ray/triangle hit.
// Edges: nearest projected polyline within tolerance that is not hidden
// behind the nearest face. Edges win over faces when within tolerance, since
// they are the smaller target.
// TODO(OpenShape-M3): add a per-body BVH; brute force is fine for M0-sized models.
PickResult pick(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen, const PickOptions& options = {});

// Faces only; returns the nearest hit under the cursor (used for orbit pivots).
PickResult pickFace(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen);

} // namespace os::sel
