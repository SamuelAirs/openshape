// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Picking with bounding-volume hierarchies must give exactly what testing
// every triangle and edge segment gives: the same hit, depth, point, edge
// priority and tolerances. Randomized comparisons over several meshes,
// cameras (orthographic and perspective, near and far) and cursor
// positions (anywhere, and right next to edges).
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "selection/PickAccelerator.h"
#include "selection/Picking.h"

#include "PortableRandom.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <random>
#include <sstream>

using namespace os;

namespace {

struct Model {
    std::string name;
    std::vector<sel::PickTarget> accelerated;
    std::vector<sel::PickTarget> linear;
    Vec3 lo, hi;
};

Model modelOf(const std::string& name, const std::vector<geom::Shape>& shapes)
{
    Model m;
    m.name = name;
    geom::BoundingBox all;
    for (const auto& s : shapes) {
        const Uuid id = Uuid::generate();
        auto mesh = std::make_shared<const geom::Mesh>(geom::tessellate(s));
        m.accelerated.push_back({id, mesh, std::make_shared<const sel::PickAccelerator>(*mesh)});
        m.linear.push_back({id, mesh, nullptr});
        const auto b = geom::approximateBoundingBox(s);
        all.min = all.valid ? Vec3{std::min(all.min.x, b.min.x), std::min(all.min.y, b.min.y), std::min(all.min.z, b.min.z)} : b.min;
        all.max = all.valid ? Vec3{std::max(all.max.x, b.max.x), std::max(all.max.y, b.max.y), std::max(all.max.z, b.max.z)} : b.max;
        all.valid = true;
    }
    m.lo = all.min;
    m.hi = all.max;
    return m;
}

geom::Shape box(Vec3 origin, Vec3 size)
{
    return geom::makeBox(origin, size).value();
}

std::vector<Model> models()
{
    std::vector<Model> out;
    out.push_back(modelOf("box", {box({-10, -10, 0}, {20, 20, 20})}));

    // A filleted plate with a grid of holes (curved faces, many edges).
    geom::Shape plate = box({0, 0, 0}, {60, 40, 8});
    std::vector<int> vertical;
    for (int e = 0; e < plate.edgeCount(); ++e)
        if (const auto info = geom::edgeInfo(plate, e); info && std::abs(std::abs(info->tangent.z) - 1) < 1e-9)
            vertical.push_back(e);
    plate = geom::filletEdges(plate, vertical, 5).value();
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 3; ++j) {
            const auto drill = geom::makeCylinder({10.0 + 10 * i, 10.0 + 10 * j, -1}, {0, 0, 1}, 2.5, 10).value();
            plate = geom::booleanOp(plate, drill, geom::BooleanKind::Subtract).value();
        }
    out.push_back(modelOf("plate with holes", {plate}));

    // Two bodies side by side and one overlapping them (cross-body ties and
    // hidden edges), including a cylinder.
    out.push_back(modelOf("three bodies", {box({0, 0, 0}, {10, 10, 10}), box({10, 0, 0}, {10, 10, 10}),
                                           geom::makeCylinder({5, 5, -5}, {1, 0, 0}, 4, 20).value()}));

    // The two-piece part with blend corners from the kernel crash regression.
    std::ifstream in(std::string(OPENSHAPE_TEST_DATA_DIR) + "/fuse-crash-fillet-corners.brep", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    if (auto part = geom::fromBrepString(text.str()))
        out.push_back(modelOf("blended two-piece part", {part.value()}));
    return out;
}

void expectSame(const sel::PickResult& a, const sel::PickResult& b, const std::string& where)
{
    EXPECT_EQ(int(a.kind), int(b.kind)) << where;
    EXPECT_EQ(a.bodyId, b.bodyId) << where;
    EXPECT_EQ(a.index, b.index) << where;
    // Exactly equal: the same arithmetic on the same triangle or segment.
    EXPECT_EQ(a.point.x, b.point.x) << where;
    EXPECT_EQ(a.point.y, b.point.y) << where;
    EXPECT_EQ(a.point.z, b.point.z) << where;
    EXPECT_EQ(a.depth, b.depth) << where;
    EXPECT_EQ(a.screenDistance, b.screenDistance) << where;
}

} // namespace

TEST(PickAccelerator, TreeCoversEveryTriangleAndSegment)
{
    for (const Model& m : models()) {
        const auto& target = m.accelerated.front();
        const auto& acc = *target.accelerator;
        EXPECT_TRUE(acc.matches(*target.mesh)) << m.name;
        EXPECT_GE(acc.triangleNodeCount(), 1u) << m.name;
        // A leaf holds at most 4 items: at least n/4 leaves, so ~n/2 nodes.
        EXPECT_GE(acc.triangleNodeCount(), target.mesh->triangleCount() / 4) << m.name;
        // Every segment is found by a query with an unbounded reach.
        std::size_t segments = 0;
        for (const auto& e : target.mesh->edges)
            segments += e.points.size() >= 6 ? e.points.size() / 3 - 1 : 0;
        const auto all = acc.segmentsNear({{0, 0, 0}, {0, 0, 1}}, [](const sel::Box3&) { return 1e9; });
        EXPECT_EQ(all.size(), segments) << m.name;
        for (std::size_t i = 1; i < all.size(); ++i)
            EXPECT_TRUE(all[i - 1].edge < all[i].edge || (all[i - 1].edge == all[i].edge && all[i - 1].point < all[i].point));
    }
}

TEST(PickAccelerator, NearestHitMatchesLinearScanForRandomRays)
{
    test::PortableRandom rng(7);
    const auto unit = [](test::PortableRandom& r) { return r.uniform(-1, 1); };
    for (const Model& m : models()) {
        const geom::Mesh& mesh = *m.accelerated.front().mesh;
        const auto& acc = *m.accelerated.front().accelerator;
        const Vec3 c = (acc.vertexBounds().min + acc.vertexBounds().max) * 0.5;
        const Vec3 half = (acc.vertexBounds().max - acc.vertexBounds().min) * 0.5;
        int hits = 0;
        for (int i = 0; i < 1500; ++i) {
            // Origins inside and around the part; some axis-aligned directions.
            const Vec3 origin = c + Vec3{half.x * 2 * unit(rng), half.y * 2 * unit(rng), half.z * 2 * unit(rng)};
            Vec3 dir = i % 7 == 0 ? Vec3{0, 0, unit(rng) < 0 ? -1.0 : 1.0} : Vec3{unit(rng), unit(rng), unit(rng)};
            if (dir.length() < 1e-3)
                dir = {1, 0, 0};
            const Ray ray{origin, dir.normalized()};
            std::optional<sel::PickAccelerator::Hit> linear;
            for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
                const auto hit = intersectRayTriangle(ray, mesh.vertex(mesh.indices[3 * t]), mesh.vertex(mesh.indices[3 * t + 1]),
                                                      mesh.vertex(mesh.indices[3 * t + 2]));
                if (hit && (!linear || *hit < linear->t))
                    linear = sel::PickAccelerator::Hit{*hit, std::uint32_t(t)};
            }
            const auto fast = acc.nearestHit(mesh, ray);
            ASSERT_EQ(fast.has_value(), linear.has_value()) << m.name << " ray " << i;
            if (linear) {
                ++hits;
                EXPECT_EQ(fast->t, linear->t) << m.name << " ray " << i;
                EXPECT_EQ(fast->triangle, linear->triangle) << m.name << " ray " << i;
            }
        }
        EXPECT_GT(hits, 300) << m.name;
    }
}

// Exact ties: the same triangle twice (different faces). The linear scan
// keeps the first; so must the tree.
TEST(PickAccelerator, TiesGoToTheFirstTriangle)
{
    geom::Mesh mesh;
    auto vertex = [&](Vec3 p) {
        mesh.positions.insert(mesh.positions.end(), {float(p.x), float(p.y), float(p.z)});
        mesh.normals.insert(mesh.normals.end(), {0.f, 0.f, 1.f});
        return std::uint32_t(mesh.vertexCount() - 1);
    };
    // Twenty copies of the same square (two triangles each) stacked at z = 0,
    // then one at z = 1 above them; faces numbered by copy.
    for (int copy = 0; copy < 20; ++copy) {
        const float z = copy == 19 ? 1.f : 0.f;
        const auto a = vertex({0, 0, z}), b = vertex({1, 0, z}), c = vertex({1, 1, z}), d = vertex({0, 1, z});
        mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        mesh.triangleFace.insert(mesh.triangleFace.end(), {std::uint32_t(copy), std::uint32_t(copy)});
    }
    const sel::PickAccelerator acc(mesh);
    const Ray down{{0.3, 0.6, 5}, {0, 0, -1}};
    const auto hit = acc.nearestHit(mesh, down);
    ASSERT_TRUE(hit);
    EXPECT_EQ(mesh.triangleFace[hit->triangle], 19u); // the one on top
    const Ray up{{0.3, 0.6, -5}, {0, 0, 1}};
    const auto first = acc.nearestHit(mesh, up);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->triangle, 1u); // copy 0's second triangle (u + v > 1 half); the first of 19 equal hits
}

TEST(PickAccelerator, PickingMatchesTheLinearScan)
{
    test::PortableRandom rng(2024);
    const auto unit = [](test::PortableRandom& r) { return r.unit(); };
    int compared = 0, edgeHits = 0, faceHits = 0;
    double slowMs = 0, fastMs = 0;
    for (const Model& m : models()) {
        for (int view = 0; view < 12; ++view) {
            Camera camera;
            camera.viewportSize = {1000, 700};
            camera.projection = view % 3 == 2 ? Camera::Projection::Perspective : Camera::Projection::Orthographic;
            camera.yaw = unit(rng) * 2 * kPi;
            camera.pitch = (unit(rng) - 0.5) * kPi * 0.95;
            camera.fit(m.lo, m.hi);
            if (view % 4 == 3) {
                // Zoomed in: most of the part off screen (perspective: eye close).
                camera.orthoHeight *= 0.2;
                camera.distance *= 0.35;
            }
            // The part's edges: cursor positions right next to them.
            std::vector<Vec2> cursors;
            for (int i = 0; i < 60; ++i)
                cursors.push_back({unit(rng) * camera.viewportSize.x, unit(rng) * camera.viewportSize.y});
            const auto& mesh = *m.linear.front().mesh;
            for (int i = 0; i < 60 && !mesh.edges.empty(); ++i) {
                const auto& edge = mesh.edges[std::size_t(unit(rng) * double(mesh.edges.size())) % mesh.edges.size()];
                const std::size_t points = edge.points.size() / 3;
                if (points == 0)
                    continue;
                const std::size_t k = std::size_t(unit(rng) * double(points)) % points;
                const Vec3 p{edge.points[3 * k], edge.points[3 * k + 1], edge.points[3 * k + 2]};
                cursors.push_back(camera.project(p) + Vec2{(unit(rng) - 0.5) * 30, (unit(rng) - 0.5) * 30});
            }
            for (const Vec2& cursor : cursors) {
                for (double tolerance : {6.0, 18.0}) {
                    sel::PickOptions options;
                    options.edgeTolerance = tolerance;
                    const std::string where = m.name + ", view " + std::to_string(view) + ", cursor " + std::to_string(cursor.x)
                                            + " " + std::to_string(cursor.y) + ", tolerance " + std::to_string(tolerance);
                    sel::PickResult slow, fast;
                    slowMs += std::chrono::duration<double, std::milli>([&] {
                                  const auto t0 = std::chrono::steady_clock::now();
                                  slow = sel::pick(m.linear, camera, cursor, options);
                                  return std::chrono::steady_clock::now() - t0;
                              }())
                                  .count();
                    fastMs += std::chrono::duration<double, std::milli>([&] {
                                  const auto t0 = std::chrono::steady_clock::now();
                                  fast = sel::pick(m.accelerated, camera, cursor, options);
                                  return std::chrono::steady_clock::now() - t0;
                              }())
                                  .count();
                    expectSame(fast, slow, where);
                    expectSame(sel::pickFace(m.accelerated, camera, cursor), sel::pickFace(m.linear, camera, cursor), where);
                    options.pickFaces = false;
                    expectSame(sel::pick(m.accelerated, camera, cursor, options), sel::pick(m.linear, camera, cursor, options), where);
                    ++compared;
                    edgeHits += slow.kind == sel::PickKind::Edge;
                    faceHits += slow.kind == sel::PickKind::Face;
                }
            }
        }
    }
    EXPECT_GT(compared, 3000);
    EXPECT_GT(edgeHits, 300); // the comparison covers both kinds of hits
    EXPECT_GT(faceHits, 300);
    std::printf("picked %d times: linear scan %.1f ms, accelerated %.1f ms\n", compared, slowMs, fastMs);
}
