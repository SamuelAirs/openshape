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

// The owner on his iPhone Duo, 2026-11-12: "There are locks to where you can
// rotate your image". The turntable stopped dead at the top and bottom views.
TEST(Camera, OrbitGoesOverTheTopAndComesBackRound)
{
    Camera c;
    c.viewportSize = {400, 800};
    const Camera start = c;
    const double step = 10; // px: 0.08 rad
    int upsideDown = 0;
    double lastUpZ = c.up().z;
    Vec3 lastEye = c.eye();
    for (int i = 0; i < 2000 && !(i > 100 && std::abs(c.pitch - start.pitch) < 0.04 && !c.upsideDown()); ++i) {
        c.orbit(0, step, c.target);
        // No jump: each step moves the eye by its 0.08 rad, and turns `up` as little.
        EXPECT_NEAR((c.eye() - lastEye).length(), 2 * c.distance * std::sin(0.04), 1e-6) << "step " << i;
        EXPECT_LT(std::abs(c.up().z - lastUpZ), 0.09) << "step " << i;
        lastEye = c.eye();
        lastUpZ = c.up().z;
        upsideDown += c.upsideDown() ? 1 : 0;
        EXPECT_GT(c.pitch, -kPi - 1e-12);
        EXPECT_LE(c.pitch, kPi + 1e-12);
    }
    EXPECT_GT(upsideDown, 30) << "half of the way round is upside down";
    EXPECT_FALSE(c.upsideDown());
    EXPECT_NEAR(c.yaw, start.yaw, 1e-9) << "an up-and-down drag never turns about the vertical";
    // Upside down, +Z points down the screen.
    Camera over = start;
    over.orbit(0, (kPi / 2 - start.pitch + 0.5) / 0.008, over.target);
    EXPECT_TRUE(over.upsideDown());
    EXPECT_LT(over.up().z, 0);
    EXPECT_GT(over.eye().z, over.target.z) << "just past the top: still above";
    EXPECT_LT(over.backward().dot(start.backward() - Vec3{0, 0, start.backward().z}), 0) << "on the other side of the vertical";
}

TEST_P(CameraBothProjections, OrbitOverTheTopKeepsPivotFixedOnScreen)
{
    Camera c = makeCamera(GetParam());
    const Vec3 pivot{10, -10, 20};
    const Vec2 before = c.project(pivot);
    for (int i = 0; i < 40; ++i)
        c.orbit(7, 25, pivot, c.orbitSense());
    EXPECT_TRUE(c.upsideDown() || std::abs(c.pitch) < kPi / 2);
    const Vec2 after = c.project(pivot);
    EXPECT_NEAR(after.x, before.x, 1e-6);
    EXPECT_NEAR(after.y, before.y, 1e-6);
}

// A sideways drag carries the near side of the model with the finger, the
// right way up and upside down (with the sense the drag took where it began).
TEST(Camera, SidewaysDragFollowsTheFingerUpsideDownToo)
{
    for (const double pitch : {0.6, 2.4, -0.6, -2.4}) {
        Camera c;
        c.viewportSize = {400, 800};
        c.pitch = pitch;
        const Vec3 near = c.target + c.backward() * 10.0; // between the pivot and the eye
        const double x0 = c.project(near).x;
        c.orbit(12, 0, c.target, c.orbitSense());
        EXPECT_GT(c.project(near).x, x0 + 0.5) << "pitch " << pitch;
    }
}

TEST(Camera, InterpolateFromUpsideDownTakesTheShortWay)
{
    Camera a, b;
    a.pitch = 3.0;  // upside down, nearly level
    b.pitch = -3.0; // and just past level the other way: 0.28 rad apart through +-pi
    const Camera mid = Camera::interpolate(a, b, 0.5);
    EXPECT_GT(std::abs(mid.pitch), 3.0);
    EXPECT_NEAR(Camera::interpolate(a, b, 1.0).pitch, -3.0, 1e-12);
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
