// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Camera.h"
#include "core/Uuid.h"
#include "geometry/Mesh.h"
#include "selection/PickAccelerator.h"

#include <memory>
#include <vector>

namespace os::sel {

// OriginAxis: one of the X/Y/Z axis lines drawn through the origin (index
// 0/1/2), pickable while a tool waits for a target (Align). Datum: a
// construction axis or plane (bodyId holds its id).
enum class PickKind { None, Face, Edge, Profile, OriginAxis, Datum };

struct PickTarget {
    Uuid bodyId;
    std::shared_ptr<const geom::Mesh> mesh;
    // Built once per mesh and kept with it (SceneCache). Without one, every
    // triangle and edge segment is tested: the reference implementation the
    // accelerated path must match exactly.
    std::shared_ptr<const PickAccelerator> accelerator;
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
// they are the smaller target. Targets with an accelerator only test the
// triangles and segments near the ray, with identical results.
PickResult pick(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen, const PickOptions& options = {});

// Faces only; returns the nearest hit under the cursor (used for orbit pivots).
PickResult pickFace(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen);

} // namespace os::sel
