// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "selection/Picking.h"

#include "core/Timer.h"

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
    // One segment, in the order a scan over all edges would meet it: the
    // nearest wins, and of equally near ones the last.
    auto visit = [&](const PickTarget& target, const geom::Mesh::EdgePolyline& edge, std::size_t i) {
        const auto& p = edge.points;
        const Vec3 a{p[i], p[i + 1], p[i + 2]};
        const Vec3 b{p[i + 3], p[i + 4], p[i + 5]};
        if (camera.projection == Camera::Projection::Perspective && (camera.depthOf(a) <= 0 || camera.depthOf(b) <= 0))
            return;
        double t = 0;
        const double d = distanceToSegment2D(screen, camera.project(a), camera.project(b), &t);
        if (d > bestDistance)
            return;
        const Vec3 onEdge = a + (b - a) * t;
        // Occlusion: reject edges clearly behind the visible surface.
        if (face.hit()) {
            const double slack = camera.pixelSize(onEdge) * (options.edgeTolerance + 2);
            if (camera.depthOf(onEdge) > face.depth + slack)
                return;
        }
        bestDistance = d;
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
    if (bestEdge.hit())
        return bestEdge;
    return options.pickFaces ? face : PickResult{};
}

} // namespace os::sel
