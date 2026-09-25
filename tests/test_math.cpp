// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Math.h"

#include <gtest/gtest.h>

using namespace os;

TEST(Math, InverseOfLookAtTimesOrtho)
{
    const Mat4 view = lookAt({30, -40, 25}, {1, 2, 3}, {0, 0, 1});
    const Mat4 proj = orthographic(-10, 10, -7, 7, 0.1, 500);
    const Mat4 vp = proj * view;
    const auto inv = vp.inverted();
    ASSERT_TRUE(inv.has_value());
    const Mat4 id = vp * *inv;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            EXPECT_NEAR(id.at(r, c), r == c ? 1.0 : 0.0, 1e-9);
}

TEST(Math, LookAtMapsTargetToNegativeZ)
{
    const Mat4 view = lookAt({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
    const Vec3 p = view.transformPoint({0, 0, 0});
    EXPECT_NEAR(p.x, 0, 1e-12);
    EXPECT_NEAR(p.y, 0, 1e-12);
    EXPECT_NEAR(p.z, -10, 1e-12);
}

TEST(Math, ProjectionDepthRange)
{
    const Mat4 ortho = orthographic(-1, 1, -1, 1, 1, 11);
    EXPECT_NEAR(ortho.transformPoint({0, 0, -1}).z, 0.0, 1e-12);
    EXPECT_NEAR(ortho.transformPoint({0, 0, -11}).z, 1.0, 1e-12);
    const Mat4 persp = perspective(kPi / 3, 1.5, 1, 100);
    EXPECT_NEAR(persp.transformPoint({0, 0, -1}).z, 0.0, 1e-12);
    EXPECT_NEAR(persp.transformPoint({0, 0, -100}).z, 1.0, 1e-12);
}

TEST(Math, RayTriangle)
{
    const Ray ray{{0.2, 0.2, 5}, {0, 0, -1}};
    const auto t = intersectRayTriangle(ray, {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 5.0, 1e-12);
    EXPECT_FALSE(intersectRayTriangle({{2, 2, 5}, {0, 0, -1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}).has_value());
    EXPECT_FALSE(intersectRayTriangle({{0.2, 0.2, 5}, {0, 0, 1}}, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}).has_value());
}

TEST(Math, ClosestLineParameterToRay)
{
    // Line along +Z through origin; ray along +X at height 7 crosses it.
    const auto t = closestLineParameterToRay({0, 0, 0}, {0, 0, 1}, {{-10, 0, 7}, {1, 0, 0}});
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(*t, 7.0, 1e-12);
    EXPECT_FALSE(closestLineParameterToRay({0, 0, 0}, {0, 0, 1}, {{1, 0, 0}, {0, 0, 1}}).has_value());
}

TEST(Math, DistanceToSegment)
{
    double t = -1;
    EXPECT_NEAR(distanceToSegment2D({5, 3}, {0, 0}, {10, 0}, &t), 3.0, 1e-12);
    EXPECT_NEAR(t, 0.5, 1e-12);
    EXPECT_NEAR(distanceToSegment2D({-4, 3}, {0, 0}, {10, 0}), 5.0, 1e-12);
}
