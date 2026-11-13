// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "selection/Picking.h"

#include "core/Timer.h"

#include <algorithm>
#include <limits>

namespace os::sel {

namespace {

struct Bounds {
    Vec3 min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec3 max{-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
};

Bounds boundsOf(const geom::Mesh& mesh)
{
    Bounds b;
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const Vec3 v = mesh.vertex(i);
        b.min = {std::min(b.min.x, v.x), std::min(b.min.y, v.y), std::min(b.min.z, v.z)};
        b.max = {std::max(b.max.x, v.x), std::max(b.max.y, v.y), std::max(b.max.z, v.z)};
    }
    return b;
}

// An edge is hidden when a face lies this far (in pixels' worth of depth at
// the edge) in front of it along its own line of sight.
constexpr double kEdgeCoverPx = 1.5;

bool rayHitsBox(const Ray& ray, const Bounds& b, double pad)
{
    double t0 = -std::numeric_limits<double>::max(), t1 = std::numeric_limits<double>::max();
    const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const double d[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const double lo[3] = {b.min.x - pad, b.min.y - pad, b.min.z - pad};
    const double hi[3] = {b.max.x + pad, b.max.y + pad, b.max.z + pad};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-15) {
            if (o[i] < lo[i] || o[i] > hi[i])
                return false;
            continue;
        }
        double a = (lo[i] - o[i]) / d[i], c = (hi[i] - o[i]) / d[i];
        if (a > c)
            std::swap(a, c);
        t0 = std::max(t0, a);
        t1 = std::min(t1, c);
        if (t0 > t1)
            return false;
    }
    return t1 >= 0;
}

// PickOptions::narrowFaceShare: the pointer is `distance` px from an edge (at
// `onEdge` on screen) and over `face`. True when that face is so narrow
// there, straight across from the edge, that the pointer is outside the
// edge's share of its width: the tap is for the face.
bool narrowFaceWins(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen, const PickResult& face,
                    Vec2 onEdge, double distance, const PickOptions& options)
{
    // Right on the edge (a pen's tip, a zoomed-in finger) is the edge.
    constexpr double kOnEdgePx = 3.0;
    constexpr double kStepPx = 2.0;
    if (distance <= kOnEdgePx)
        return false;
    // How far beyond the pointer the face must go on for the edge's share
    // of its width to reach the pointer: a face that wide is the edge's here.
    const double span = distance / options.narrowFaceShare - distance;
    const Vec2 away = (screen - onEdge) * (1.0 / distance);
    for (double reach = kStepPx;; reach += kStepPx) {
        const double at = std::min(reach, span);
        const PickResult beyond = pickFace(targets, camera, screen + away * at);
        if (beyond.kind != PickKind::Face || !(beyond.bodyId == face.bodyId) || beyond.index != face.index)
            return true; // the face ends before that
        if (at >= span)
            return false;
    }
}

} // namespace

PickResult pickFace(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen)
{
    PickResult best;
    double bestT = std::numeric_limits<double>::max();
    const Ray ray = camera.rayAt(screen);
    for (const auto& target : targets) {
        if (!target.mesh)
            continue;
        const geom::Mesh& mesh = *target.mesh;
        if (target.accelerator && target.accelerator->matches(mesh)) {
            // Same first test as below, then only the triangles near the ray.
            const Box3& vb = target.accelerator->vertexBounds();
            Bounds bounds;
            if (!vb.empty())
                bounds = {vb.min, vb.max};
            if (!rayHitsBox(ray, bounds, 1e-6))
                continue;
            const auto hit = target.accelerator->nearestHit(mesh, ray);
            if (hit && hit->t < bestT) {
                bestT = hit->t;
                best.kind = PickKind::Face;
                best.bodyId = target.bodyId;
                best.index = static_cast<int>(mesh.triangleFace[hit->triangle]);
                best.point = ray.at(hit->t);
            }
            continue;
        }
        if (!rayHitsBox(ray, boundsOf(mesh), 1e-6))
            continue;
        for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
            const auto hit = intersectRayTriangle(ray, mesh.vertex(mesh.indices[3 * t]), mesh.vertex(mesh.indices[3 * t + 1]),
                                                  mesh.vertex(mesh.indices[3 * t + 2]));
            if (hit && *hit < bestT) {
                bestT = *hit;
                best.kind = PickKind::Face;
                best.bodyId = target.bodyId;
                best.index = static_cast<int>(mesh.triangleFace[t]);
                best.point = ray.at(*hit);
            }
        }
    }
    if (best.hit())
        best.depth = camera.depthOf(best.point);
    return best;
}

PickResult pick(const std::vector<PickTarget>& targets, const Camera& camera, Vec2 screen, const PickOptions& options)
{
    const PickResult face = pickFace(targets, camera, screen);
    if (!options.pickEdges)
        return options.pickFaces ? face : PickResult{};

    PickResult bestEdge;
    double bestDistance = options.edgeTolerance;
    Vec2 bestOnScreen; // the nearest point of that edge, on screen
    // One segment, in the order a scan over all edges would meet it: the
    // nearest wins, and of equally near ones the last.
    auto visit = [&](const PickTarget& target, const geom::Mesh::EdgePolyline& edge, std::size_t i) {
        const auto& p = edge.points;
        const Vec3 a{p[i], p[i + 1], p[i + 2]};
        const Vec3 b{p[i + 3], p[i + 4], p[i + 5]};
        if (camera.projection == Camera::Projection::Perspective && (camera.depthOf(a) <= 0 || camera.depthOf(b) <= 0))
            return;
        double t = 0;
        const Vec2 sa = camera.project(a), sb = camera.project(b);
        const double d = distanceToSegment2D(screen, sa, sb, &t);
        if (d > bestDistance)
            return;
        const Vec2 onScreen = sa + (sb - sa) * t;
        // The point of the edge under the cursor. In perspective the place on
        // the projected segment is not the place on the edge (1/depth is what
        // varies linearly on screen); taking one for the other put the point
        // millimetres off along a long edge, and the occlusion test below
        // then threw out a visible edge.
        if (camera.projection == Camera::Projection::Perspective) {
            const double wa = camera.depthOf(a), wb = camera.depthOf(b);
            t = t * wa / (t * wa + (1 - t) * wb);
        }
        const Vec3 onEdge = a + (b - a) * t;
        // Occlusion: reject edges clearly behind the visible surface.
        if (face.hit()) {
            const double slack = camera.pixelSize(onEdge) * (options.edgeTolerance + 2);
            if (camera.depthOf(onEdge) > face.depth + slack)
                return;
        }
        // That slack lets through the edges a shallow feature hides just
        // under the surface: where a pad or a raised letter meets the face it
        // stands on, behind its top. At a finger's tolerance it is several
        // millimetres deep, and a tap on the top took one of them. The edge
        // itself must be in sight: nothing clearly in front of it along its
        // own line of sight (its own faces are met at its depth).
        if (const PickResult cover = pickFace(targets, camera, onScreen);
            cover.hit() && cover.depth < camera.depthOf(onEdge) - camera.pixelSize(onEdge) * kEdgeCoverPx)
            return;
        bestDistance = d;
        bestOnScreen = onScreen;
        bestEdge.kind = PickKind::Edge;
        bestEdge.bodyId = target.bodyId;
        bestEdge.index = edge.edgeIndex;
        bestEdge.point = onEdge;
        bestEdge.depth = camera.depthOf(onEdge);
        bestEdge.screenDistance = d;
    };
    // How far from the pick ray a point in `box` may lie and still project
    // within the tolerance: the tolerance (plus a pixel) at the box's far
    // side. Boxes entirely behind a perspective eye hold only skipped segments.
    const Ray ray = camera.rayAt(screen);
    auto reach = [&](const Box3& box) {
        Vec3 farthest = box.min;
        double deepest = -std::numeric_limits<double>::max();
        for (int c = 0; c < 8; ++c) {
            const Vec3 corner{c & 1 ? box.max.x : box.min.x, c & 2 ? box.max.y : box.min.y, c & 4 ? box.max.z : box.min.z};
            const double depth = camera.depthOf(corner);
            if (depth > deepest) {
                deepest = depth;
                farthest = corner;
            }
        }
        if (camera.projection == Camera::Projection::Perspective && deepest <= 0)
            return -1.0;
        return camera.pixelSize(farthest) * (options.edgeTolerance * 1.05 + 1.0);
    };
    for (const auto& target : targets) {
        if (!target.mesh)
            continue;
        const geom::Mesh& mesh = *target.mesh;
        if (target.accelerator && target.accelerator->matches(mesh)) {
            for (const auto& s : target.accelerator->segmentsNear(ray, reach))
                visit(target, mesh.edges[s.edge], 3 * std::size_t(s.point));
            continue;
        }
        for (const auto& edge : mesh.edges)
            for (std::size_t i = 0; i + 5 < edge.points.size(); i += 3)
                visit(target, edge, i);
    }
    if (bestEdge.hit() && options.pickFaces && options.narrowFaceShare > 0 && face.hit() && narrowFaceWins(targets, camera, screen, face, bestOnScreen, bestDistance, options))
        return face;
    if (bestEdge.hit())
        return bestEdge;
    return options.pickFaces ? face : PickResult{};
}

} // namespace os::sel
