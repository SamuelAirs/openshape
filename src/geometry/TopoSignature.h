// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "geometry/Shape.h"

#include <optional>

// Interim topological-naming strategy (see docs/TOPOLOGICAL_NAMING.md).
//
// A persistent reference to a face/edge stores a *topology index hint* plus a
// *geometric signature* captured when the reference was made. When the
// upstream shape is regenerated, resolution first checks whether the hinted
// index still matches the signature; otherwise it searches for the best
// matching sub-shape. Resolution fails (rather than guessing wildly) when no
// candidate is close enough.
namespace os::geom {

struct FaceSignature {
    SurfaceKind kind = SurfaceKind::Other;
    Vec3 normal;
    Vec3 centroid;
    double area = 0;
};

struct EdgeSignature {
    CurveKind kind = CurveKind::Other;
    Vec3 midpoint;
    Vec3 tangent;
    double length = 0;
};

std::optional<FaceSignature> captureFaceSignature(const Shape& shape, int faceIndex);
std::optional<EdgeSignature> captureEdgeSignature(const Shape& shape, int edgeIndex);

// Returns the face index in `shape` that best matches the signature, or
// nullopt if nothing plausible exists. `hint` is tried first.
std::optional<int> resolveFace(const Shape& shape, const FaceSignature& signature, int hint);
std::optional<int> resolveEdge(const Shape& shape, const EdgeSignature& signature, int hint);

} // namespace os::geom
