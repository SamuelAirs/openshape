// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "selection/PickAccelerator.h"

#include "core/Timer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace os::sel {

namespace {
constexpr std::uint32_t kLeafSize = 4;

Vec3 center(const Box3& b)
{
    return (b.min + b.max) * 0.5;
}

double axisValue(const Vec3& v, int axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
} // namespace

void Box3::add(const Vec3& p)
{
    min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
    max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
}

void Box3::add(const Box3& b)
{
    if (b.empty())
        return;
    add(b.min);
    add(b.max);
}

std::optional<std::pair<double, double>> lineBoxInterval(const Ray& ray, const Box3& box, double pad, bool rayOnly)
{
    if (box.empty())
        return std::nullopt;
    double t0 = rayOnly ? 0.0 : -std::numeric_limits<double>::infinity();
    double t1 = std::numeric_limits<double>::infinity();
    const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const double d[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const double lo[3] = {box.min.x - pad, box.min.y - pad, box.min.z - pad};
    const double hi[3] = {box.max.x + pad, box.max.y + pad, box.max.z + pad};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-15) {
            if (o[i] < lo[i] || o[i] > hi[i])
                return std::nullopt;
            continue;
        }
        double a = (lo[i] - o[i]) / d[i], c = (hi[i] - o[i]) / d[i];
        if (a > c)
            std::swap(a, c);
        t0 = std::max(t0, a);
        t1 = std::min(t1, c);
        if (t0 > t1)
            return std::nullopt;
    }
    return std::make_pair(t0, t1);
}

PickAccelerator::PickAccelerator(const geom::Mesh& mesh)
{
    ScopedTimer timer("PickAccelerator build");
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i)
        vertexBounds_.add(mesh.vertex(i));
    triangleCount_ = mesh.triangleCount();
    // Rounding in the triangle test puts hit points at most a few ulps
    // outside their triangle's box: pad boxes far beyond that.
    if (!vertexBounds_.empty())
        padding_ = 1e-6 * (vertexBounds_.max - vertexBounds_.min).length() + 1e-9;

    std::vector<Box3> boxes(triangleCount_);
    for (std::size_t t = 0; t < triangleCount_; ++t)
        for (int k = 0; k < 3; ++k)
            boxes[t].add(mesh.vertex(mesh.indices[3 * t + std::size_t(k)]));
    build(triangleNodes_, triangleOrder_, boxes);

    boxes.clear();
    for (std::size_t e = 0; e < mesh.edges.size(); ++e) {
        const auto& p = mesh.edges[e].points;
        for (std::size_t i = 0; i + 5 < p.size(); i += 3) {
            segments_.push_back({std::uint32_t(e), std::uint32_t(i / 3)});
            Box3 b;
            b.add(Vec3{p[i], p[i + 1], p[i + 2]});
            b.add(Vec3{p[i + 3], p[i + 4], p[i + 5]});
            boxes.push_back(b);
        }
    }
    segmentTotal_ = segments_.size();
    build(segmentNodes_, segmentOrder_, boxes);
}

bool PickAccelerator::matches(const geom::Mesh& mesh) const
{
    if (mesh.triangleCount() != triangleCount_)
        return false;
    std::size_t segments = 0;
    for (const auto& edge : mesh.edges)
        segments += edge.points.size() >= 6 ? edge.points.size() / 3 - 1 : 0;
    return segments == segmentTotal_;
}

void PickAccelerator::build(std::vector<Node>& nodes, std::vector<std::uint32_t>& order, const std::vector<Box3>& boxes)
{
    nodes.clear();
    order.resize(boxes.size());
    std::iota(order.begin(), order.end(), 0u);
    if (boxes.empty())
        return;
    std::vector<Vec3> centers(boxes.size());
    for (std::size_t i = 0; i < boxes.size(); ++i)
        centers[i] = center(boxes[i]);

    struct Task {
        std::uint32_t node, begin, end;
    };
    nodes.reserve(2 * boxes.size() / kLeafSize + 1);
    nodes.push_back({});
    std::vector<Task> stack{{0, 0, std::uint32_t(boxes.size())}};
    while (!stack.empty()) {
        const Task task = stack.back();
        stack.pop_back();
        Box3 box, centerBox;
        for (std::uint32_t i = task.begin; i < task.end; ++i) {
            box.add(boxes[order[i]]);
            centerBox.add(centers[order[i]]);
        }
        nodes[task.node].box = box;
        const std::uint32_t count = task.end - task.begin;
        if (count <= kLeafSize) {
            nodes[task.node].first = task.begin;
            nodes[task.node].count = count;
            continue;
        }
        // Median split along the widest spread of the item centers.
        const Vec3 spread = centerBox.max - centerBox.min;
        const int axis = spread.x >= spread.y && spread.x >= spread.z ? 0 : spread.y >= spread.z ? 1 : 2;
        const std::uint32_t mid = task.begin + count / 2;
        std::nth_element(order.begin() + task.begin, order.begin() + mid, order.begin() + task.end,
                         [&](std::uint32_t a, std::uint32_t b) { return axisValue(centers[a], axis) < axisValue(centers[b], axis); });
        const std::uint32_t left = std::uint32_t(nodes.size());
        nodes.push_back({});
        nodes.push_back({});
        nodes[task.node].first = left;
        nodes[task.node].count = 0;
        stack.push_back({left, task.begin, mid});
        stack.push_back({left + 1, mid, task.end});
    }
}

std::optional<PickAccelerator::Hit> PickAccelerator::nearestHit(const geom::Mesh& mesh, const Ray& ray) const
{
    std::optional<Hit> best;
    if (triangleNodes_.empty())
        return best;
    std::uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = triangleNodes_[stack[--top]];
        const auto span = lineBoxInterval(ray, node.box, padding_, true);
        // Equal entry parameters are kept: a tie at the same distance goes to
        // the lower triangle index, which may lie in this box.
        if (!span || (best && span->first > best->t))
            continue;
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i) {
                const std::uint32_t t = triangleOrder_[i];
                const auto hit = intersectRayTriangle(ray, mesh.vertex(mesh.indices[3 * std::size_t(t)]),
                                                      mesh.vertex(mesh.indices[3 * std::size_t(t) + 1]),
                                                      mesh.vertex(mesh.indices[3 * std::size_t(t) + 2]));
                if (hit && (!best || *hit < best->t || (*hit == best->t && t < best->triangle)))
                    best = Hit{*hit, t};
            }
            continue;
        }
        // Depth is about log2(n / 4) + 1; 64 entries hold any mesh.
        stack[top++] = node.first + 1;
        stack[top++] = node.first;
    }
    return best;
}

std::vector<PickAccelerator::Segment> PickAccelerator::segmentsNear(const Ray& ray,
                                                                   const std::function<double(const Box3&)>& radius) const
{
    std::vector<Segment> out;
    if (segmentNodes_.empty())
        return out;
    std::uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = segmentNodes_[stack[--top]];
        const double r = radius(node.box);
        if (r < 0 || !lineBoxInterval(ray, node.box, r + padding_, false))
            continue;
        if (node.count > 0) {
            for (std::uint32_t i = node.first; i < node.first + node.count; ++i)
                out.push_back(segments_[segmentOrder_[i]]);
            continue;
        }
        stack[top++] = node.first + 1;
        stack[top++] = node.first;
    }
    std::sort(out.begin(), out.end(), [](const Segment& a, const Segment& b) {
        return a.edge != b.edge ? a.edge < b.edge : a.point < b.point;
    });
    return out;
}

} // namespace os::sel
