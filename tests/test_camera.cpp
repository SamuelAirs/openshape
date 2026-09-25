// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Camera.h"

#include <gtest/gtest.h>

using namespace os;

namespace {
Camera makeCamera(Camera::Projection projection)
{
    Camera c;
    c.projection = projection;
    c.viewportSize = {1000, 800};
    c.fit({-10, -10, 0}, {10, 10, 20});
    return c;
}
} // namespace

class CameraBothProjections : public ::testing::TestWithParam<Camera::Projection> {};

TEST_P(CameraBothProjections, ProjectAndRayAreConsistent)
{
    const Camera c = makeCamera(GetParam());
    for (const Vec3 p : {Vec3{0, 0, 0}, Vec3{10, -10, 20}, Vec3{-7, 3, 5}}) {
        const Vec2 s = c.project(p);
        const Ray ray = c.rayAt(s);
        // Distance from p to the ray through its own projection must be ~0.
        const Vec3 w = p - ray.origin;
        const Vec3 closest = ray.origin + ray.direction * w.dot(ray.direction);
        EXPECT_NEAR((closest - p).length(), 0.0, 1e-6);
    }
}

TEST_P(CameraBothProjections, CenterOfScreenLooksAtTarget)
{
    const Camera c = makeCamera(GetParam());
    const Vec2 s = c.project(c.target);
    EXPECT_NEAR(s.x, 500, 1e-6);
    EXPECT_NEAR(s.y, 400, 1e-6);
}

TEST_P(CameraBothProjections, ZoomKeepsCursorPointFixed)
{
    Camera c = makeCamera(GetParam());
    const Vec2 cursor{700, 250};
    const Vec3 before = c.pointOnViewPlane(cursor, c.target);
    c.zoomAt(cursor, 0.5);
    const Vec2 after = c.project(before);
    EXPECT_NEAR(after.x, cursor.x, 1e-6);
    EXPECT_NEAR(after.y, cursor.y, 1e-6);
}

TEST_P(CameraBothProjections, PanMovesPointWithCursor)
{
    Camera c = makeCamera(GetParam());
    const Vec3 p = c.pointOnViewPlane({400, 300}, c.target);
    c.pan({400, 300}, {460, 340});
    const Vec2 s = c.project(p);
    EXPECT_NEAR(s.x, 460, 1e-6);
    EXPECT_NEAR(s.y, 340, 1e-6);
}

TEST_P(CameraBothProjections, OrbitKeepsPivotFixedOnScreen)
{
    Camera c = makeCamera(GetParam());
    const Vec3 pivot{10, -10, 20};
    const Vec2 before = c.project(pivot);
    c.orbit(37, -21, pivot);
    const Vec2 after = c.project(pivot);
    EXPECT_NEAR(after.x, before.x, 1e-6);
    EXPECT_NEAR(after.y, before.y, 1e-6);
}

INSTANTIATE_TEST_SUITE_P(Projections, CameraBothProjections,
                         ::testing::Values(Camera::Projection::Orthographic, Camera::Projection::Perspective));

TEST(Camera, StandardViewsOrientation)
{
    Camera c;
    c.viewportSize = {800, 800};
    c.setStandardView(StandardView::Top);
    EXPECT_NEAR(c.forward().z, -1.0, 1e-9);
    EXPECT_NEAR(c.right().x, 1.0, 1e-9); // +X to the right
    EXPECT_NEAR(c.up().y, 1.0, 1e-9);    // +Y up on screen

    c.setStandardView(StandardView::Front);
    EXPECT_NEAR(c.forward().y, 1.0, 1e-9); // looking along +Y
    EXPECT_NEAR(c.up().z, 1.0, 1e-9);
    EXPECT_NEAR(c.right().x, 1.0, 1e-9);

    c.setStandardView(StandardView::Right);
    EXPECT_NEAR(c.forward().x, -1.0, 1e-9);
    EXPECT_NEAR(c.right().y, 1.0, 1e-9);
}

TEST(Camera, OrbitClampsPitch)
{
    Camera c;
    c.orbit(0, 100000, c.target);
    EXPECT_LE(c.pitch, kPi / 2 + 1e-12);
    c.orbit(0, -100000, c.target);
    EXPECT_GE(c.pitch, -kPi / 2 - 1e-12);
}

TEST(Camera, InterpolateEndpoints)
{
    Camera a, b;
    a.yaw = 3.0;
    b.yaw = -3.0; // shortest path crosses +-pi
    b.orthoHeight = 10;
    const Camera mid = Camera::interpolate(a, b, 0.5);
    EXPECT_GT(std::abs(mid.yaw), 3.0);
    const Camera end = Camera::interpolate(a, b, 1.0);
    EXPECT_NEAR(end.yaw, -3.0, 1e-12);
    EXPECT_NEAR(end.orthoHeight, 10, 1e-12);
}
