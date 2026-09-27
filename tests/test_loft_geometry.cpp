// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Loft through flat profiles (geom::loftFaces), checked against exact
// volumes: prisms, pyramid and cone frustums, and what must be refused.
#include "geometry/Loft.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

using namespace os;
using namespace os::geom;

namespace {

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

// A plane at height z (normal +Z, or -Z when `down`: a sketch on a face
// looking down).
PlaneFrame at(double z, bool down = false)
{
    PlaneFrame p;
    p.origin = {0, 0, z};
    if (down)
        p.yAxis = {0, -1, 0};
    return p;
}

std::vector<PlanarCurve> square(double side, double z, Vec3 center = {})
{
    const double h = side / 2;
    const Vec3 c{center.x, center.y, z};
    const Vec3 a = c + Vec3{-h, -h, 0}, b = c + Vec3{h, -h, 0}, d = c + Vec3{h, h, 0}, e = c + Vec3{-h, h, 0};
    return {segment(a, b), segment(b, d), segment(d, e), segment(e, a)};
}

// The largest region the curves enclose, on `plane`.
Shape region(const PlaneFrame& plane, const std::vector<PlanarCurve>& curves)
{
    auto regions = findRegions(plane, curves);
    EXPECT_TRUE(regions.ok()) << regions.developerMessage();
    return regions.ok() && !regions.value().empty() ? regions.value().front().face : Shape();
}

Shape squareAt(double side, double z) { return region(at(z), square(side, z)); }
Shape circleAt(double radius, double z) { return region(at(z), {circle({0, 0, z}, radius)}); }

// The area of the loft's cross-section at height z: a thin slab of it
// divided by its thickness.
double sectionArea(const Shape& solid, double z)
{
    constexpr double t = 1e-3;
    const auto slab = makeBox({-100, -100, z - t / 2}, {200, 200, t});
    EXPECT_TRUE(slab.ok());
    const auto common = booleanOp(solid, slab.value(), BooleanKind::Intersect);
    EXPECT_TRUE(common.ok()) << common.developerMessage();
    return common.ok() ? volume(common.value()) / t : 0.0;
}

} // namespace

TEST(Loft, TwoEqualSquaresMakeAPrism)
{
    for (bool ruled : {false, true}) {
        auto loft = loftFaces({squareAt(10, 0), squareAt(10, 20)}, ruled);
        ASSERT_TRUE(loft.ok()) << loft.developerMessage();
        EXPECT_NEAR(volume(loft.value()), 2000.0, 1e-6) << (ruled ? "straight" : "smooth");
        const auto box = boundingBox(loft.value());
        EXPECT_NEAR(box.min.x, -5, 1e-6);
        EXPECT_NEAR(box.max.x, 5, 1e-6);
        EXPECT_NEAR(box.min.y, -5, 1e-6);
        EXPECT_NEAR(box.max.y, 5, 1e-6);
        EXPECT_NEAR(box.min.z, 0, 1e-6);
        EXPECT_NEAR(box.max.z, 20, 1e-6);
        EXPECT_EQ(loft.value().solidCount(), 1);
        EXPECT_TRUE(isValid(loft.value()));
    }
}

TEST(Loft, SquaresOfTwoSizesMakeAPyramidFrustum)
{
    auto loft = loftFaces({squareAt(20, 0), squareAt(10, 30)}, /*ruled=*/true);
    ASSERT_TRUE(loft.ok()) << loft.developerMessage();
    EXPECT_NEAR(frustumVolume(400, 100, 30), 7000.0, 1e-9);
    EXPECT_NEAR(volume(loft.value()), 7000.0, 1e-6);
    EXPECT_EQ(loft.value().faceCount(), 6);
    // Six flat faces (the sides are planes, not flat B-splines: push/pull
    // and sketches take them).
    for (int f = 0; f < loft.value().faceCount(); ++f)
        EXPECT_TRUE(faceInfo(loft.value(), f)->isPlanar()) << "face " << f << " kind " << int(faceInfo(loft.value(), f)->kind);
    const auto box = boundingBox(loft.value());
    EXPECT_NEAR(box.size().x, 20, 1e-6);
    EXPECT_NEAR(box.size().z, 30, 1e-6);
}

TEST(Loft, CirclesMakeAConeFrustumEitherWay)
{
    const double expected = kPi * 30 / 3 * (100 + 50 + 25); // pi * 10 * 175
    EXPECT_NEAR(expected, 5497.787143782138, 1e-9);
    for (bool ruled : {false, true}) {
        auto loft = loftFaces({circleAt(10, 0), circleAt(5, 30)}, ruled);
        ASSERT_TRUE(loft.ok()) << loft.developerMessage();
        EXPECT_NEAR(volume(loft.value()), expected, 1e-4) << (ruled ? "straight" : "smooth");
        const auto box = boundingBox(loft.value());
        EXPECT_NEAR(box.size().x, 20, 1e-5);
        EXPECT_NEAR(box.min.z, 0, 1e-6);
        EXPECT_NEAR(box.max.z, 30, 1e-6);
    }
}

TEST(Loft, ThreeProfilesSmoothAndStraightDiffer)
{
    // A barrel: 10 x 10 at the ends, 30 x 30 in the middle.
    const std::vector<Shape> profiles{squareAt(10, 0), squareAt(30, 15), squareAt(10, 30)};
    auto straight = loftFaces(profiles, true);
    auto smooth = loftFaces(profiles, false);
    ASSERT_TRUE(straight.ok()) << straight.developerMessage();
    ASSERT_TRUE(smooth.ok()) << smooth.developerMessage();
    // Straight: two pyramid frustums meeting at the middle profile.
    EXPECT_NEAR(volume(straight.value()), 2 * frustumVolume(100, 900, 15), 1e-6);
    EXPECT_NEAR(volume(straight.value()), 13000.0, 1e-6);
    // Smooth bulges past the straight faces between the profiles.
    EXPECT_GT(volume(smooth.value()), volume(straight.value()) + 100.0);
    for (const auto* loft : {&straight, &smooth}) {
        // Both pass through the middle profile: 30 wide there, 900 mm² across.
        const auto box = boundingBox(loft->value());
        EXPECT_NEAR(box.size().x, 30, 1e-4);
        EXPECT_NEAR(box.size().y, 30, 1e-4);
        EXPECT_NEAR(box.size().z, 30, 1e-6);
        EXPECT_NEAR(sectionArea(loft->value(), 15), 900.0, 0.5);
        EXPECT_NEAR(sectionArea(loft->value(), 0.01), 100.0, 5.0);
    }
    // Straight halfway up the lower half: 20 x 20 exactly.
    EXPECT_NEAR(sectionArea(straight.value(), 7.5), 400.0, 0.5);
    EXPECT_GT(sectionArea(smooth.value(), 7.5), 410.0);
}

TEST(Loft, SquareToCircle)
{
    // A 20 x 20 square at the bottom, a circle of radius 8 at 25: a valid
    // solid between the cone from the circle inscribed in the square (inside
    // it) and the prism of the square (around it), passing through both.
    const double circleArea = kPi * 64;
    const double inner = frustumVolume(kPi * 100, circleArea, 25), outer = 400 * 25;
    double volumes[2] = {0, 0};
    for (bool ruled : {false, true}) {
        auto loft = loftFaces({squareAt(20, 0), circleAt(8, 25)}, ruled);
        ASSERT_TRUE(loft.ok()) << loft.developerMessage();
        EXPECT_TRUE(isValid(loft.value()));
        EXPECT_EQ(loft.value().solidCount(), 1);
        const double v = volume(loft.value());
        volumes[ruled ? 1 : 0] = v;
        EXPECT_GT(v, inner) << v;
        EXPECT_LT(v, outer) << v;
        // At least as much as a frustum of these two areas (the square's
        // corners reach out further than a scaled circle would).
        EXPECT_GE(v, frustumVolume(400, circleArea, 25) - 1e-6) << v;
        const auto box = boundingBox(loft.value());
        EXPECT_NEAR(box.size().x, 20, 1e-4);
        EXPECT_NEAR(box.size().z, 25, 1e-6);
        EXPECT_NEAR(sectionArea(loft.value(), 0.001), 400.0, 1.0);
        EXPECT_NEAR(sectionArea(loft.value(), 24.999), circleArea, 1.0);
    }
    // With two profiles, smooth and straight are the same solid.
    EXPECT_NEAR(volumes[0], volumes[1], 1e-3);
    // And back: the order of the profiles does not matter for the volume.
    auto down = loftFaces({circleAt(8, 25), squareAt(20, 0)}, true);
    ASSERT_TRUE(down.ok()) << down.developerMessage();
    EXPECT_NEAR(volume(down.value()), volumes[1], 1e-3);
}

TEST(Loft, ProfilesFacingOppositeWays)
{
    // The upper profile drawn on a plane looking down (a sketch on a face's
    // underside): its loop runs the other way, the loft is the same.
    const Shape lower = squareAt(20, 0);
    const Shape upper = region(at(30, true), square(10, 30));
    auto loft = loftFaces({lower, upper}, true);
    ASSERT_TRUE(loft.ok()) << loft.developerMessage();
    EXPECT_NEAR(volume(loft.value()), 7000.0, 1e-6);
}

TEST(Loft, TurnedSquareDoesNotTwistBadly)
{
    // A square turned 45 degrees above another: a valid, twisted solid
    // (straight: its sides are ruled surfaces), with the right ends.
    std::vector<PlanarCurve> diamond;
    const double r = 5 * std::sqrt(2.0);
    const Vec3 p[4] = {{r, 0, 20}, {0, r, 20}, {-r, 0, 20}, {0, -r, 20}};
    for (int i = 0; i < 4; ++i)
        diamond.push_back(segment(p[i], p[(i + 1) % 4]));
    for (bool ruled : {false, true}) {
        auto loft = loftFaces({squareAt(10, 0), region(at(20), diamond)}, ruled);
        ASSERT_TRUE(loft.ok()) << loft.developerMessage();
        const double v = volume(loft.value());
        EXPECT_GT(v, 0.0);
        EXPECT_LE(v, 2000.0 + 1e-6);
        EXPECT_NEAR(sectionArea(loft.value(), 0.01), 100.0, 3.0);
    }
}

TEST(Loft, HolesAreLoftedThrough)
{
    // A ring to a smaller ring (a hose adapter): outer cone minus inner cone.
    auto ring = [](double outer, double inner, double z) {
        auto regions = findRegions(at(z), {circle({0, 0, z}, outer), circle({0, 0, z}, inner)});
        EXPECT_TRUE(regions.ok() && regions.value().size() == 2);
        // Largest first: the disc inside, then the ring around it.
        EXPECT_NEAR(regions.value().back().area, kPi * (outer * outer - inner * inner), 1e-6);
        return regions.value().back().face;
    };
    const double h = 40;
    auto loft = loftFaces({ring(15, 13, 0), ring(10, 8, h)}, true);
    ASSERT_TRUE(loft.ok()) << loft.developerMessage();
    const double expected = frustumVolume(kPi * 225, kPi * 100, h) - frustumVolume(kPi * 169, kPi * 64, h);
    EXPECT_NEAR(volume(loft.value()), expected, 1e-3);
    EXPECT_EQ(loft.value().solidCount(), 1);

    // A hole in only one of them: refused.
    auto mixed = loftFaces({ring(15, 13, 0), circleAt(10, h)}, true);
    ASSERT_FALSE(mixed.ok());
    EXPECT_NE(mixed.userMessage().find("same number of holes"), std::string::npos) << mixed.userMessage();
}

TEST(Loft, RefusesWhatIsNotALoft)
{
    // Fewer than two profiles.
    auto one = loftFaces({squareAt(10, 0)}, false);
    ASSERT_FALSE(one.ok());
    EXPECT_EQ(one.userMessage(), "Select two or more closed profiles to loft.");
    // Two profiles in the same plane.
    auto flat = loftFaces({squareAt(10, 0), region(at(0), {circle({30, 0, 0}, 4)})}, false);
    ASSERT_FALSE(flat.ok());
    EXPECT_NE(flat.userMessage().find("same plane"), std::string::npos) << flat.userMessage();
    // Profiles that cross each other (a vertical square through a horizontal
    // one): the loft would pass through itself.
    PlaneFrame upright;
    upright.origin = {0, 0, 0};
    upright.xAxis = {1, 0, 0};
    upright.yAxis = {0, 0, 1};
    const Shape standing = region(upright, {segment({-3, 0, -3}, {3, 0, -3}), segment({3, 0, -3}, {3, 0, 3}),
                                            segment({3, 0, 3}, {-3, 0, 3}), segment({-3, 0, 3}, {-3, 0, -3})});
    auto crossing = loftFaces({squareAt(10, 0), standing}, true);
    EXPECT_FALSE(crossing.ok()) << "volume " << (crossing.ok() ? volume(crossing.value()) : 0.0);
}

TEST(Loft, ArchBackToTheSamePlane)
{
    // Circles of radius 3 standing across a half circle of radius 20 (from
    // the origin over to x = 40): the first and the last profile share the
    // ground plane (not in a row, so allowed) and face opposite ways. Close
    // to half a torus: pi r^2 x pi R.
    std::vector<Shape> profiles;
    for (int step = 0; step <= 4; ++step) {
        const double a = kPi * step / 4;
        PlaneFrame across; // normal along the half circle: (sin a, 0, cos a)
        across.origin = {20 - 20 * std::cos(a), 0, 20 * std::sin(a)};
        across.xAxis = {0, 1, 0};
        across.yAxis = {-std::cos(a), 0, std::sin(a)};
        profiles.push_back(region(across, {circle(across.origin, 3)}));
    }
    const double halfTorus = kPi * 9 * kPi * 20;
    auto arch = loftFaces(profiles, false);
    ASSERT_TRUE(arch.ok()) << arch.developerMessage();
    EXPECT_NEAR(volume(arch.value()), halfTorus, 0.02 * halfTorus);
    const auto box = boundingBox(arch.value());
    EXPECT_NEAR(box.min.z, 0, 1e-6);
    EXPECT_NEAR(box.max.z, 23, 0.1);
    EXPECT_NEAR(box.min.x, -3, 0.1);
    EXPECT_NEAR(box.max.x, 43, 0.1);
}

TEST(Loft, IsQuickEnoughToPreview)
{
    // Square to circle, smooth, with every check: well under a frame budget
    // of a slow device (the preview worker runs it while the user decides).
    const auto start = std::chrono::steady_clock::now();
    auto loft = loftFaces({squareAt(20, 0), circleAt(8, 25), squareAt(12, 50)}, false);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ASSERT_TRUE(loft.ok()) << loft.developerMessage();
    EXPECT_LT(ms, 2000.0);
    std::printf("[          ] three-profile smooth loft: %.1f ms\n", ms);
}
