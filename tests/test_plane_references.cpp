// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What of a body lies in a plane: the reference geometry a sketch on a face
// snaps to (geom::planeReferences). Exact positions of a 20 mm cube's top
// face, a hole in it, and nothing for a plane that misses the body.
#include "geometry/Modeling.h"
#include "geometry/PlaneReferences.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace os;
using namespace os::geom;

namespace {

Shape cube()
{
    auto box = makeBox({-10, -10, 0}, {20, 20, 20});
    EXPECT_TRUE(box.ok());
    return box.value();
}

PlaneFrame top(double z = 20) { return {{0, 0, z}, {1, 0, 0}, {0, 1, 0}}; }

bool has(const std::vector<Vec3>& list, Vec3 p)
{
    return std::any_of(list.begin(), list.end(), [&](const Vec3& q) { return (q - p).length() < 1e-9; });
}

} // namespace

TEST(PlaneReferences, TopFaceOfACube)
{
    const PlaneReferences refs = planeReferences(cube(), top());
    ASSERT_EQ(refs.edges.size(), 4u);
    for (const auto& e : refs.edges) {
        EXPECT_EQ(e.kind, PlaneEdge::Kind::Line);
        EXPECT_NEAR((e.end - e.start).length(), 20.0, 1e-9);
        EXPECT_NEAR(e.start.z, 20.0, 1e-9);
    }
    ASSERT_EQ(refs.vertices.size(), 4u);
    for (const Vec3 corner : {Vec3{-10, -10, 20}, Vec3{10, -10, 20}, Vec3{10, 10, 20}, Vec3{-10, 10, 20}})
        EXPECT_TRUE(has(refs.vertices, corner)) << corner.x << ", " << corner.y;
    ASSERT_EQ(refs.faceCenters.size(), 1u);
    EXPECT_NEAR((refs.faceCenters.front() - Vec3{0, 0, 20}).length(), 0.0, 1e-9);
}

TEST(PlaneReferences, BottomFaceOnTheGroundAndAPlaneThatMisses)
{
    // A sketch on the ground sees the footprint of a box standing on it.
    const PlaneReferences ground = planeReferences(cube(), top(0));
    EXPECT_EQ(ground.edges.size(), 4u);
    EXPECT_EQ(ground.vertices.size(), 4u);
    EXPECT_EQ(ground.faceCenters.size(), 1u);
    // Halfway up: the plane cuts the box but no edge or face lies in it.
    EXPECT_TRUE(planeReferences(cube(), top(10)).empty());
    // Above it: nothing (the bounding box test).
    EXPECT_TRUE(planeReferences(cube(), top(30)).empty());
}

TEST(PlaneReferences, AHoleGivesACircleAndItsCenterButNoCorner)
{
    auto cylinder = makeCylinder({3, 4, -1}, {0, 0, 1}, 2.5, 22);
    ASSERT_TRUE(cylinder.ok());
    auto drilled = booleanOp(cube(), cylinder.value(), BooleanKind::Subtract);
    ASSERT_TRUE(drilled.ok());
    const PlaneReferences refs = planeReferences(drilled.value(), top());
    const auto circles = std::count_if(refs.edges.begin(), refs.edges.end(),
                                       [](const PlaneEdge& e) { return e.kind == PlaneEdge::Kind::Circle; });
    ASSERT_EQ(circles, 1);
    for (const auto& e : refs.edges)
        if (e.kind == PlaneEdge::Kind::Circle) {
            EXPECT_NEAR((e.center - Vec3{3, 4, 20}).length(), 0.0, 1e-7);
            EXPECT_NEAR(e.radius, 2.5, 1e-9);
        }
    // The rim's seam vertex is no corner: the four face corners only.
    EXPECT_EQ(refs.vertices.size(), 4u);
    ASSERT_EQ(refs.faceCenters.size(), 1u);
}

TEST(PlaneReferences, AnArcRunsCounterclockwiseAboutThePlaneNormal)
{
    // Half a cylinder standing on the ground: its top has a semicircle and a
    // straight edge; seen from above (normal +z) and from below (normal -z)
    // the arc is counterclockwise about each normal.
    auto cylinder = makeCylinder({0, 0, 0}, {0, 0, 1}, 5, 10);
    ASSERT_TRUE(cylinder.ok());
    auto half = booleanOp(cylinder.value(), makeBox({-10, -10, -1}, {20, 10, 12}).value(), BooleanKind::Subtract);
    ASSERT_TRUE(half.ok());
    for (const double sign : {1.0, -1.0}) {
        const PlaneFrame plane{{0, 0, 10}, {1, 0, 0}, {0, sign, 0}}; // normal +z, then -z
        const PlaneReferences refs = planeReferences(half.value(), plane);
        int arcs = 0;
        for (const auto& e : refs.edges) {
            if (e.kind != PlaneEdge::Kind::Arc)
                continue;
            ++arcs;
            EXPECT_NEAR(e.radius, 5.0, 1e-9);
            // The arc's middle is at +y (the half kept): start -> middle -> end
            // turns counterclockwise about the normal.
            const Vec3 n = plane.normal();
            const Vec3 mid{0, 5, 10};
            EXPECT_GT((mid - e.start).cross(e.end - mid).dot(n), 0.0) << "sign " << sign;
        }
        EXPECT_EQ(arcs, 1);
    }
}
