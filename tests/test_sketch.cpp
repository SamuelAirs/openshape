#include "sketch/Sketch.h"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>

using namespace os;
using namespace os::sketch;

namespace {

Vec2 pos(const Sketch& s, EntityId id)
{
    return s.point(id)->position;
}

} // namespace

TEST(Sketch, OriginAlwaysPresentAndFixed)
{
    Sketch s;
    ASSERT_NE(s.point(kOriginId), nullptr);
    EXPECT_TRUE(s.point(kOriginId)->fixed);
    EXPECT_FALSE(s.remove(kOriginId));
}

TEST(Sketch, RectangleDimensionsSolveExactly)
{
    // 60 x 40 rectangle anchored at the origin: fully constrained.
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {55, 37}, kOriginId);
    EXPECT_EQ(r.corners[0], kOriginId);
    s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60.0});
    s.addConstraint({ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 40.0});
    const auto report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    EXPECT_EQ(report.degreesOfFreedom, 0);
    EXPECT_NEAR(pos(s, r.corners[2]).x, 60.0, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[2]).y, 40.0, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[3]).x, 0.0, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[3]).y, 40.0, 1e-9);
}

TEST(Sketch, UnconstrainedRectangleHasFourDof)
{
    Sketch s;
    addRectangle(s, {5, 5}, {20, 15});
    const auto report = solve(s);
    ASSERT_TRUE(report.ok);
    EXPECT_EQ(report.degreesOfFreedom, 4); // position (2) + width + height
}

TEST(Sketch, EditingDimensionMovesGeometry)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {60, 40}, kOriginId);
    const EntityId width = s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60.0});
    s.addConstraint({ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 40.0});
    ASSERT_TRUE(solve(s).ok);
    s.constraint(width)->value = 75.0;
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(pos(s, r.corners[1]).x, 75.0, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[2]).x, 75.0, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[2]).y, 40.0, 1e-9);
}

TEST(Sketch, CircleDiameter)
{
    Sketch s;
    const EntityId c = s.addPoint({15, 15});
    const EntityId circle = s.addCircle(c, 2.0);
    s.addConstraint({ConstraintKind::Diameter, circle, kNoEntity, 6.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(s.circle(circle)->radius, 3.0, 1e-9);
}

TEST(Sketch, CoincidentAndLineConstraints)
{
    Sketch s;
    const EntityId a = s.addPoint({1, 1});
    const EntityId b = s.addPoint({10, 3});
    const EntityId line = s.addLine(a, b);
    s.addConstraint({ConstraintKind::Coincident, a, kOriginId});
    s.addConstraint({ConstraintKind::Horizontal, line});
    s.addConstraint({ConstraintKind::Distance, a, b, 12.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(pos(s, a).x, 0.0, 1e-9);
    EXPECT_NEAR(pos(s, a).y, 0.0, 1e-9);
    EXPECT_NEAR(pos(s, b).y, 0.0, 1e-9);
    EXPECT_NEAR(std::abs(pos(s, b).x), 12.0, 1e-9);
}

TEST(Sketch, VerticalConstraint)
{
    Sketch s;
    const EntityId a = s.addPoint({0, 0});
    const EntityId b = s.addPoint({2, 10});
    const EntityId line = s.addLine(a, b);
    s.addConstraint({ConstraintKind::Vertical, line});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(pos(s, a).x, pos(s, b).x, 1e-9);
}

TEST(Sketch, ConflictingConstraintsAreReported)
{
    Sketch s;
    const EntityId a = s.addPoint({0, 0}, true);
    const EntityId b = s.addPoint({10, 0});
    const EntityId line = s.addLine(a, b);
    s.addConstraint({ConstraintKind::Horizontal, line});
    s.addConstraint({ConstraintKind::HorizontalDistance, a, b, 10.0});
    const EntityId bad = s.addConstraint({ConstraintKind::Distance, a, b, 5.0});
    const auto report = solve(s);
    EXPECT_FALSE(report.ok);
    EXPECT_FALSE(report.message.empty());
    EXPECT_FALSE(report.conflicting.empty());
    EXPECT_NE(std::find(report.conflicting.begin(), report.conflicting.end(), bad), report.conflicting.end());
    // Positions are untouched by a failed solve.
    EXPECT_NEAR(pos(s, b).x, 10.0, 1e-12);
}

TEST(Sketch, DraggingMovesFreeGeometryOnly)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {20, 10}, kOriginId);
    ASSERT_TRUE(solve(s).ok);
    // Drag the opposite corner: the rectangle stays axis-aligned and anchored.
    ASSERT_TRUE(solveDragging(s, r.corners[2], {30, 18}).ok);
    EXPECT_NEAR(pos(s, r.corners[2]).x, 30.0, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[2]).y, 18.0, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[1]).y, 0.0, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[3]).x, 0.0, 1e-9);
    EXPECT_EQ(pos(s, kOriginId).x, 0.0);
}

TEST(Sketch, RemoveCascades)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {20, 10});
    s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 20.0});
    EXPECT_EQ(s.lines().size(), 4u);
    EXPECT_TRUE(s.remove(r.corners[1])); // removes the two lines through it
    EXPECT_EQ(s.lines().size(), 2u);
    for (const auto& [id, c] : s.constraints()) {
        EXPECT_NE(c.a, r.corners[1]);
        EXPECT_NE(c.b, r.corners[1]);
        EXPECT_NE(c.a, r.edges[0]);
        EXPECT_NE(c.a, r.edges[1]);
    }
    EXPECT_TRUE(s.remove(r.edges[2]));
    // Corner 2 is now unused and removed with the line; corner 3 is still used.
    EXPECT_EQ(s.point(r.corners[2]), nullptr);
    EXPECT_NE(s.point(r.corners[3]), nullptr);
}

TEST(Sketch, JsonRoundTrip)
{
    Sketch s(Uuid::generate(), Plane::fromNormal({1, 2, 3}, {0, -1, 0}));
    s.setName("Sketch 1");
    const auto r = addRectangle(s, {0, 0}, {60, 30}, kOriginId);
    s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60.0});
    const EntityId center = s.addPoint({15, 15});
    const EntityId circle = s.addCircle(center, 3);
    s.addConstraint({ConstraintKind::Diameter, circle, kNoEntity, 6.0});
    const auto json = s.toJson();
    auto back = Sketch::fromJson(json);
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    EXPECT_EQ(back.value().toJson(), json);
    EXPECT_EQ(back.value().id(), s.id());
    // New ids after loading never collide with existing ones.
    const EntityId fresh = back.value().addPoint({1, 1});
    EXPECT_EQ(s.point(fresh), nullptr);
}

TEST(Sketch, JsonRejectsMalformed)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {60, 30});
    const auto good = s.toJson();
    auto rejects = [](const nlohmann::json& j) { return !Sketch::fromJson(j).ok(); };
    auto j = good;
    j["lines"][0]["start"] = 999;
    EXPECT_TRUE(rejects(j));
    j = good;
    j["points"][1]["x"] = "a";
    EXPECT_TRUE(rejects(j));
    j = good;
    j["plane"]["xAxis"] = nlohmann::json::array({1, 1, 0});
    EXPECT_TRUE(rejects(j));
    j = good;
    j["constraints"][0]["type"] = "Telepathy";
    EXPECT_TRUE(rejects(j));
    j = good;
    j["constraints"][0]["a"] = r.corners[0]; // horizontal constraint on a point
    EXPECT_TRUE(rejects(j));
    j = good;
    j["points"][1]["id"] = j["points"][0]["id"];
    EXPECT_TRUE(rejects(j));
    j = good;
    j.erase("points");
    EXPECT_TRUE(rejects(j));
}

TEST(Plane, FrameIsOrthonormalAndRoundTrips)
{
    for (const Vec3 n : {Vec3{0, 0, 1}, Vec3{0, 0, -1}, Vec3{1, 0, 0}, Vec3{0.3, -0.5, 0.8}}) {
        const Plane p = Plane::fromNormal({3, 4, 5}, n);
        EXPECT_NEAR(p.xAxis.dot(p.yAxis), 0.0, 1e-12);
        EXPECT_NEAR(p.normal().dot(n.normalized()), 1.0, 1e-12);
        const Vec2 local{7.5, -2.25};
        const Vec2 back = p.toLocal(p.toWorld(local));
        EXPECT_NEAR(back.x, local.x, 1e-12);
        EXPECT_NEAR(back.y, local.y, 1e-12);
    }
    const Plane top = Plane::fromNormal({0, 0, 0}, {0, 0, 1});
    EXPECT_NEAR(top.xAxis.x, 1.0, 1e-12); // XY sketches keep world X to the right
}
