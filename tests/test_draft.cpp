// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrusions with drafted side walls, checked against exact frustum volumes.
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace os;
using namespace os::geom;

namespace {

double degrees(double d)
{
    return d * kPi / 180.0;
}

// h/3 (A1 + A2 + sqrt(A1 A2)): a pyramid or cone frustum.
double frustumVolume(double a1, double a2, double h)
{
    return h / 3 * (a1 + a2 + std::sqrt(a1 * a2));
}

PlanarCurve segment(Vec3 a, Vec3 b)
{
    PlanarCurve c;
    c.kind = PlanarCurve::Kind::Segment;
    c.start = a;
    c.end = b;
    return c;
}

PlanarCurve circle(Vec3 center, double radius)
{
    PlanarCurve c;
    c.kind = PlanarCurve::Kind::Circle;
    c.center = center;
    c.radius = radius;
    return c;
}

std::vector<PlanarCurve> square(double s)
{
    const double h = s / 2;
    return {segment({-h, -h, 0}, {h, -h, 0}), segment({h, -h, 0}, {h, h, 0}), segment({h, h, 0}, {-h, h, 0}),
            segment({-h, h, 0}, {-h, -h, 0})};
}

// The largest region the curves enclose (on the XY plane).
Shape region(const std::vector<PlanarCurve>& curves)
{
    auto regions = findRegions(PlaneFrame{}, curves);
    EXPECT_TRUE(regions.ok()) << regions.developerMessage();
    return regions.value().front().face;
}

} // namespace

TEST(Draft, SquareBecomesAPyramidFrustum)
{
    const Shape face = region(square(10));
    const double h = 10, a = degrees(5);
    auto drafted = extrudeFacesDrafted({face}, {0, 0, h}, a);
    ASSERT_TRUE(drafted.ok()) << drafted.developerMessage();
    const double top = 10 - 2 * h * std::tan(a);
    EXPECT_NEAR(volume(drafted.value()), frustumVolume(100, top * top, h), 1e-6);
    const auto box = boundingBox(drafted.value());
    EXPECT_NEAR(box.min.z, 0, 1e-7);
    EXPECT_NEAR(box.max.z, h, 1e-7);
    EXPECT_NEAR(box.size().x, 10, 1e-7) << "the base keeps its size";
    EXPECT_TRUE(isValid(drafted.value()));
    // 6 flat faces: the four walls lean in, corners stay sharp.
    EXPECT_EQ(drafted.value().faceCount(), 6);

    // Negative: widens.
    auto widened = extrudeFacesDrafted({face}, {0, 0, h}, -a);
    ASSERT_TRUE(widened.ok()) << widened.developerMessage();
    const double wide = 10 + 2 * h * std::tan(a);
    EXPECT_NEAR(volume(widened.value()), frustumVolume(100, wide * wide, h), 1e-6);

    // Downward: the same frustum below the plane.
    auto down = extrudeFacesDrafted({face}, {0, 0, -h}, a);
    ASSERT_TRUE(down.ok()) << down.developerMessage();
    EXPECT_NEAR(volume(down.value()), frustumVolume(100, top * top, h), 1e-6);
    EXPECT_NEAR(boundingBox(down.value()).min.z, -h, 1e-7);
    EXPECT_NEAR(boundingBox(down.value()).max.z, 0, 1e-7);

    // Zero draft is the plain extrusion.
    auto straight = extrudeFacesDrafted({face}, {0, 0, h}, 0);
    ASSERT_TRUE(straight.ok());
    EXPECT_NEAR(volume(straight.value()), 1000, 1e-6);
}

TEST(Draft, CircleBecomesAConeFrustum)
{
    const Shape face = region({circle({0, 0, 0}, 5)});
    const double h = 12, a = degrees(10);
    auto drafted = extrudeFacesDrafted({face}, {0, 0, h}, a);
    ASSERT_TRUE(drafted.ok()) << drafted.developerMessage();
    const double r2 = 5 - h * std::tan(a);
    EXPECT_NEAR(volume(drafted.value()), kPi * h / 3 * (25 + 5 * r2 + r2 * r2), 1e-6);
    bool cone = false;
    for (int i = 0; i < drafted.value().faceCount(); ++i)
        cone = cone || faceInfo(drafted.value(), i)->kind == SurfaceKind::Cone;
    EXPECT_TRUE(cone) << "the wall is an exact cone";
}

// A plate with a hole: the outline leans in and the hole widens.
TEST(Draft, HolesWidenAsTheOutlineNarrows)
{
    auto curves = square(20);
    curves.push_back(circle({0, 0, 0}, 3));
    auto regions = findRegions(PlaneFrame{}, curves);
    ASSERT_TRUE(regions.ok());
    Shape plate;
    for (const auto& r : regions.value())
        if (std::abs(r.area - (400 - kPi * 9)) < 1e-6)
            plate = r.face;
    ASSERT_FALSE(plate.isNull());
    const double h = 6, a = degrees(8), t = h * std::tan(a);
    auto drafted = extrudeFacesDrafted({plate}, {0, 0, h}, a);
    ASSERT_TRUE(drafted.ok()) << drafted.developerMessage();
    const double outer = frustumVolume(400, (20 - 2 * t) * (20 - 2 * t), h);
    const double hole = kPi * h / 3 * (9 + 3 * (3 + t) + (3 + t) * (3 + t));
    EXPECT_NEAR(volume(drafted.value()), outer - hole, 1e-6);
}

// An L (five convex corners, one reflex): the section at height z is the L
// offset inward by t = z tan(a) with sharp corners, of area
// A - P t + K t^2 with K = sum of tan(turn / 2) = 5 - 1.
TEST(Draft, ReflexCornersStaySharp)
{
    const Shape face = region({segment({0, 0, 0}, {20, 0, 0}), segment({20, 0, 0}, {20, 10, 0}), segment({20, 10, 0}, {10, 10, 0}),
                               segment({10, 10, 0}, {10, 20, 0}), segment({10, 20, 0}, {0, 20, 0}), segment({0, 20, 0}, {0, 0, 0})});
    const double h = 10, k = std::tan(degrees(7));
    auto drafted = extrudeFacesDrafted({face}, {0, 0, h}, degrees(7));
    ASSERT_TRUE(drafted.ok()) << drafted.developerMessage();
    const double expected = 300 * h - 80 * k * h * h / 2 + 4 * k * k * h * h * h / 3;
    EXPECT_NEAR(volume(drafted.value()), expected, 1e-6);
    EXPECT_EQ(drafted.value().faceCount(), 8);
}

TEST(Draft, RefusesADraftThatClosesTheShape)
{
    // A 4 mm wide bar: 10 mm up at 15 degrees moves each long wall in by 2.68 mm.
    const Shape bar = region({segment({0, 0, 0}, {20, 0, 0}), segment({20, 0, 0}, {20, 4, 0}), segment({20, 4, 0}, {0, 4, 0}),
                              segment({0, 4, 0}, {0, 0, 0})});
    EXPECT_FALSE(extrudeFacesDrafted({bar}, {0, 0, 10}, degrees(15)).ok());
    EXPECT_TRUE(extrudeFacesDrafted({bar}, {0, 0, 10}, degrees(10)).ok()) << "1.76 mm each side still leaves 0.47 mm";
    const Shape face = region(square(10));
    // 10 mm up at 30 degrees moves each wall in by 5.77 mm: more than half the width.
    auto closed = extrudeFacesDrafted({face}, {0, 0, 10}, degrees(30));
    EXPECT_FALSE(closed.ok());
    EXPECT_EQ(closed.userMessage(), "The draft closes the shape before the full height. Use a smaller angle or a shorter extrusion.");
    // A hole that closes when the draft widens the outline (and shrinks the hole).
    auto curves = square(20);
    curves.push_back(circle({0, 0, 0}, 1));
    auto regions = findRegions(PlaneFrame{}, curves);
    ASSERT_TRUE(regions.ok());
    Shape plate;
    for (const auto& r : regions.value())
        if (r.area > 300)
            plate = r.face;
    EXPECT_FALSE(extrudeFacesDrafted({plate}, {0, 0, 10}, degrees(-10)).ok());
    EXPECT_FALSE(extrudeFacesDrafted({face}, {0, 0, 10}, degrees(89.5)).ok());
}
