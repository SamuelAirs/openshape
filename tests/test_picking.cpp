// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "interaction/Manipulator.h"
#include "selection/Picking.h"

#include <gtest/gtest.h>

using namespace os;

namespace {

struct Scene {
    Uuid id = Uuid::generate();
    geom::Shape shape;
    std::vector<sel::PickTarget> targets;
    Camera camera;

    Scene()
    {
        shape = geom::makeBox({-10, -10, 0}, {20, 20, 20}).value();
        auto mesh = std::make_shared<const geom::Mesh>(geom::tessellate(shape));
        targets.push_back({id, mesh, std::make_shared<const sel::PickAccelerator>(*mesh)});
        camera.viewportSize = {1000, 800};
        camera.fit({-10, -10, 0}, {10, 10, 20});
    }

    int faceWithNormal(const Vec3& n) const
    {
        for (int i = 0; i < shape.faceCount(); ++i)
            if (geom::faceInfo(shape, i)->normal.dot(n) > 0.999)
                return i;
        return -1;
    }
};

} // namespace

TEST(Picking, TopFaceUnderCursor)
{
    Scene s;
    const Vec2 p = s.camera.project({0, 0, 20});
    const auto hit = sel::pick(s.targets, s.camera, p);
    ASSERT_EQ(hit.kind, sel::PickKind::Face);
    EXPECT_EQ(hit.bodyId, s.id);
    EXPECT_EQ(hit.index, s.faceWithNormal({0, 0, 1}));
    EXPECT_NEAR(hit.point.z, 20.0, 1e-4);
}

TEST(Picking, NothingInEmptySpace)
{
    Scene s;
    EXPECT_FALSE(sel::pick(s.targets, s.camera, {5, 5}).hit());
}

TEST(Picking, EdgeWinsNearEdge)
{
    Scene s;
    // Midpoint of the top front edge (y = -10, z = 20), nudged 3px.
    Vec2 p = s.camera.project({0, -10, 20});
    p.y += 3;
    const auto hit = sel::pick(s.targets, s.camera, p);
    ASSERT_EQ(hit.kind, sel::PickKind::Edge);
    const auto info = geom::edgeInfo(s.shape, hit.index);
    ASSERT_TRUE(info);
    EXPECT_NEAR(info->midpoint.y, -10.0, 1e-6);
    EXPECT_NEAR(info->midpoint.z, 20.0, 1e-6);
}

TEST(Picking, TouchToleranceIsLarger)
{
    Scene s;
    Vec2 p = s.camera.project({0, -10, 20});
    p.y += 12; // inside the front face, 12px from the edge
    sel::PickOptions mouse;
    mouse.edgeTolerance = 6;
    EXPECT_EQ(sel::pick(s.targets, s.camera, p, mouse).kind, sel::PickKind::Face);
    sel::PickOptions touch;
    touch.edgeTolerance = 18;
    EXPECT_EQ(sel::pick(s.targets, s.camera, p, touch).kind, sel::PickKind::Edge);
}

// In perspective, along a long edge the place on the projected segment is
// not the place on the edge: the point must be the one under the cursor, or
// the occlusion test throws out the visible edge (a Rotate click on a box's
// edge picked the face behind it).
TEST(Picking, LongEdgeAnywhereAlongItInBothProjections)
{
    const geom::Shape bar = geom::makeBox({0, 0, 0}, {200, 20, 10}).value();
    auto mesh = std::make_shared<const geom::Mesh>(geom::tessellate(bar));
    const Uuid id = Uuid::generate();
    const std::vector<sel::PickTarget> targets{{id, mesh, std::make_shared<const sel::PickAccelerator>(*mesh)}};
    for (const auto projection : {Camera::Projection::Orthographic, Camera::Projection::Perspective}) {
        Camera camera;
        camera.projection = projection;
        camera.viewportSize = {1200, 800};
        camera.fit({0, 0, 0}, {200, 20, 10});
        for (const double x : {15.0, 60.0, 100.0, 140.0, 185.0}) {
            const Vec3 onEdge{x, 0, 10}; // the top front edge
            const Vec2 p = camera.project(onEdge);
            const auto hit = sel::pick(targets, camera, p);
            ASSERT_EQ(hit.kind, sel::PickKind::Edge) << "x " << x;
            const auto info = geom::edgeInfo(bar, hit.index);
            EXPECT_NEAR(info->midpoint.y, 0.0, 1e-6);
            EXPECT_NEAR(info->midpoint.z, 10.0, 1e-6);
            EXPECT_NEAR((camera.project(hit.point) - p).length(), 0.0, 1e-6) << "the point under the cursor";
            EXPECT_NEAR(hit.point.x, x, 1e-6);
        }
    }
}

// The center of a box face lies on the diagonal its two triangles share: a
// click there picks that face, not the one behind it (nor a hidden edge).
TEST(Picking, FaceCentersInBothProjections)
{
    for (const auto projection : {Camera::Projection::Orthographic, Camera::Projection::Perspective}) {
        Scene s;
        s.camera.projection = projection;
        s.camera.fit({-10, -10, 0}, {10, 10, 20});
        for (const auto& [center, normal] : {std::pair<Vec3, Vec3>{{0, 0, 20}, {0, 0, 1}},
                                             std::pair<Vec3, Vec3>{{0, -10, 10}, {0, -1, 0}},
                                             std::pair<Vec3, Vec3>{{10, 0, 10}, {1, 0, 0}}}) {
            const auto hit = sel::pick(s.targets, s.camera, s.camera.project(center));
            ASSERT_EQ(hit.kind, sel::PickKind::Face);
            EXPECT_EQ(hit.index, s.faceWithNormal(normal));
            EXPECT_NEAR((hit.point - center).length(), 0.0, 1e-6);
        }
    }
}

TEST(Picking, HiddenBackEdgeIsNotPicked)
{
    Scene s;
    // The bottom back edge (y = +10, z = 0) is hidden behind the box in the
    // default iso view (camera in front-right-top).
    const Vec2 p = s.camera.project({0, 10, 0});
    const auto hit = sel::pick(s.targets, s.camera, p);
    if (hit.kind == sel::PickKind::Edge) {
        const auto info = geom::edgeInfo(s.shape, hit.index);
        EXPECT_FALSE(std::abs(info->midpoint.y - 10) < 1e-6 && std::abs(info->midpoint.z) < 1e-6);
    }
}

TEST(Manipulator, DragAlongAxisFollowsPointer)
{
    Scene s;
    interact::LinearManipulator m({0, 0, 20}, {0, 0, 1});
    const Vec2 start = s.camera.project({0, 0, 20 + 5});
    m.beginDrag(s.camera, start, 0.0);
    const Vec2 end = s.camera.project({0, 0, 20 + 5 + 12.5});
    EXPECT_NEAR(m.dragTo(s.camera, end), 12.5, 1e-6);
}

TEST(Manipulator, HitTest)
{
    Scene s;
    interact::LinearManipulator m({0, 0, 20}, {0, 0, 1});
    const double px = s.camera.pixelSize({0, 0, 20});
    const Vec2 onShaft = s.camera.project({0, 0, 20 + 40 * px});
    EXPECT_TRUE(m.hitTest(s.camera, onShaft, 0.0, 10).has_value());
    EXPECT_FALSE(m.hitTest(s.camera, onShaft + Vec2{60, 0}, 0.0, 10).has_value());
}

TEST(Manipulator, SnapIncrements)
{
    EXPECT_DOUBLE_EQ(interact::snapIncrement(0.1, 8), 1.0);
    EXPECT_DOUBLE_EQ(interact::snapIncrement(0.02, 8), 0.2);
    EXPECT_DOUBLE_EQ(interact::snapIncrement(0.5, 8), 5.0);
    EXPECT_DOUBLE_EQ(interact::snapValue(14.7, 1.0), 15.0);
    EXPECT_DOUBLE_EQ(interact::snapValue(0.3 * 3, 0.1), 0.9);
}
