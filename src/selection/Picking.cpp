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
    for (const auto& target : targets) {
        if (!target.mesh)
            continue;
        for (const auto& edge : target.mesh->edges) {
            const auto& p = edge.points;
            for (std::size_t i = 0; i + 5 < p.size(); i += 3) {
                const Vec3 a{p[i], p[i + 1], p[i + 2]};
                const Vec3 b{p[i + 3], p[i + 4], p[i + 5]};
                if (camera.projection == Camera::Projection::Perspective
                    && (camera.depthOf(a) <= 0 || camera.depthOf(b) <= 0))
                    continue;
                double t = 0;
                const double d = distanceToSegment2D(screen, camera.project(a), camera.project(b), &t);
                if (d > bestDistance)
                    continue;
                const Vec3 onEdge = a + (b - a) * t;
                // Occlusion: reject edges clearly behind the visible surface.
                if (face.hit()) {
                    const double slack = camera.pixelSize(onEdge) * (options.edgeTolerance + 2);
                    if (camera.depthOf(onEdge) > face.depth + slack)
                        continue;
                }
                bestDistance = d;
                bestEdge.kind = PickKind::Edge;
                bestEdge.bodyId = target.bodyId;
                bestEdge.index = edge.edgeIndex;
                bestEdge.point = onEdge;
                bestEdge.depth = camera.depthOf(onEdge);
                bestEdge.screenDistance = d;
            }
        }
    }
    if (bestEdge.hit())
        return bestEdge;
    return options.pickFaces ? face : PickResult{};
}

} // namespace os::sel
