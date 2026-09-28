// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions adapted from OpenShape3D (https://github.com/laanlabs/openshape3d,
// commit be3f2fc), Copyright (c) 2026 Laan Labs and the openshape3d
// contributors, MIT License (third_party/openshape3d/LICENSE).

#include "interaction/ViewCube.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace os::interact {

namespace {

struct FaceSpec {
    StandardView view;
    const char* name;
    Vec3 normal;
    // The label reads upright from the face's own standard view: its right
    // and up are that view's screen right and up (Camera::setStandardView).
    Vec3 right;
    Vec3 up;
};

// Z up: Front looks along +Y (from -Y), Right along -X, Top straight down.
constexpr std::array<FaceSpec, 6> kFaces{{
    {StandardView::Front, "Front", {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {StandardView::Back, "Back", {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}},
    {StandardView::Left, "Left", {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}},
    {StandardView::Right, "Right", {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
    {StandardView::Top, "Top", {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {StandardView::Bottom, "Bottom", {0, 0, -1}, {1, 0, 0}, {0, -1, 0}},
}};

double component(const Vec3& v, int axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

Vec3 unitAxis(int axis, double sign)
{
    return {axis == 0 ? sign : 0.0, axis == 1 ? sign : 0.0, axis == 2 ? sign : 0.0};
}

// Box coordinates of a cube-space vector (no translation: the cube's center
// is the box's center).
Vec2 projectVector(const Camera& camera, const Vec3& v)
{
    return {v.dot(camera.right()) / ViewCube::kViewHalfExtent, -v.dot(camera.up()) / ViewCube::kViewHalfExtent};
}

// Where a ray enters the cube [-h, h]^3 (slab method); the origin itself when
// it starts inside, nothing when it misses.
std::optional<Vec3> intersectCube(const Vec3& origin, const Vec3& direction)
{
    constexpr double h = ViewCube::kHalfExtent;
    double tMin = -std::numeric_limits<double>::infinity();
    double tMax = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const double o = component(origin, axis);
        const double d = component(direction, axis);
        if (std::abs(d) < 1e-12) {
            if (std::abs(o) > h)
                return std::nullopt;
            continue;
        }
        double t0 = (-h - o) / d;
        double t1 = (h - o) / d;
        if (t0 > t1)
            std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
    }
    if (tMin > tMax || tMax <= 0)
        return std::nullopt;
    return origin + direction * std::max(tMin, 0.0);
}

} // namespace

int ViewCubeTarget::id() const
{
    switch (kind) {
    case Kind::None: return -1;
    case Kind::Face: return static_cast<int>(face);
    case Kind::Corner: return ViewCube::cornerId(corner);
    }
    return -1;
}

int ViewCube::cornerId(const Vec3& corner)
{
    return 6 + (corner.x > 0 ? 1 : 0) + (corner.y > 0 ? 2 : 0) + (corner.z > 0 ? 4 : 0);
}

Vec2 ViewCube::project(const Camera& camera, const Vec3& cubePoint)
{
    return projectVector(camera, cubePoint);
}

ViewCubeLayout ViewCube::layout(const Camera& camera)
{
    const Vec3 back = camera.backward();
    ViewCubeLayout result;
    for (std::size_t i = 0; i < kFaces.size(); ++i) {
        const FaceSpec& spec = kFaces[i];
        ViewCubeFace& face = result.faces[i];
        face.view = spec.view;
        face.name = spec.name;
        face.normal = spec.normal;
        face.facing = spec.normal.dot(back);
        const Vec3 topLeft = (spec.normal - spec.right + spec.up) * kHalfExtent;
        face.origin = projectVector(camera, topLeft);
        face.across = projectVector(camera, spec.right * (2 * kHalfExtent));
        face.down = projectVector(camera, spec.up * (-2 * kHalfExtent));
        if (face.facing > kLabelMinFacing) {
            // Ramps from just visible to fully opaque over the usable range.
            const double t = (face.facing - kLabelMinFacing) / (1 - kLabelMinFacing);
            face.labelOpacity = std::clamp(t * 1.6, 0.25, 1.0);
        }
        const Vec3 corners[4] = {spec.normal - spec.right + spec.up, spec.normal + spec.right + spec.up,
                                 spec.normal + spec.right - spec.up, spec.normal - spec.right - spec.up};
        for (int c = 0; c < 4; ++c)
            face.cornerIds[static_cast<std::size_t>(c)] = cornerId(corners[c]);
    }
    // Four edges along each axis, at the four sign combinations of the other two.
    std::size_t e = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const int j = (axis + 1) % 3, k = (axis + 2) % 3;
        for (const double sj : {-1.0, 1.0}) {
            for (const double sk : {-1.0, 1.0}) {
                const Vec3 offset = unitAxis(j, sj) + unitAxis(k, sk);
                ViewCubeEdge& edge = result.edges[e++];
                edge.axis = axis;
                edge.from = projectVector(camera, (offset - unitAxis(axis, 1)) * kHalfExtent);
                edge.to = projectVector(camera, (offset + unitAxis(axis, 1)) * kHalfExtent);
                // Its faces are the ones with normals (j, sj) and (k, sk).
                edge.visible = sj * component(back, j) > 1e-6 || sk * component(back, k) > 1e-6;
            }
        }
    }
    return result;
}

ViewCubeTarget ViewCube::hit(const Camera& camera, Vec2 box)
{
    // An orthographic ray through the tap, in cube space: from in front of
    // the cube straight into the screen.
    const Vec3 right = camera.right(), up = camera.up(), back = camera.backward();
    const Vec3 origin = right * (box.x * kViewHalfExtent) + up * (-box.y * kViewHalfExtent) + back * (2 * kViewHalfExtent);
    const auto entry = intersectCube(origin, -back);
    ViewCubeTarget target;
    if (!entry)
        return target;
    const Vec3 p = *entry / kHalfExtent;
    const Vec3 a{std::abs(p.x), std::abs(p.y), std::abs(p.z)};
    if (std::min({a.x, a.y, a.z}) > kCornerFraction) {
        target.kind = ViewCubeTarget::Kind::Corner;
        target.corner = {p.x < 0 ? -1.0 : 1.0, p.y < 0 ? -1.0 : 1.0, p.z < 0 ? -1.0 : 1.0};
        target.direction = target.corner.normalized();
        return target;
    }
    // A face: the axis of the dominant coordinate.
    const int axis = a.x >= a.y && a.x >= a.z ? 0 : a.y >= a.z ? 1 : 2;
    const Vec3 normal = unitAxis(axis, component(p, axis) < 0 ? -1.0 : 1.0);
    for (const FaceSpec& spec : kFaces) {
        if (spec.normal.dot(normal) > 0.5) {
            target.kind = ViewCubeTarget::Kind::Face;
            target.face = spec.view;
            target.direction = normal;
            break;
        }
    }
    return target;
}

} // namespace os::interact
