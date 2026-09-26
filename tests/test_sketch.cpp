// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sketch/Sketch.h"
#include "sketch/SketchEdit.h"

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

namespace {
EntityId line(Sketch& s, Vec2 a, Vec2 b)
{
    return s.addLine(s.addPoint(a), s.addPoint(b));
}
Vec2 dir(const Sketch& s, EntityId l)
{
    return pos(s, s.line(l)->end) - pos(s, s.line(l)->start);
}
double cross(Vec2 a, Vec2 b)
{
    return a.x * b.y - a.y * b.x;
}
} // namespace

TEST(Sketch, ParallelPerpendicularEqual)
{
    Sketch s;
    const EntityId a = line(s, {0, 0}, {20, 1});
    const EntityId b = line(s, {0, 10}, {15, 14});
    const EntityId c = line(s, {30, 0}, {33, 12});
    ASSERT_NE(s.addConstraint({ConstraintKind::Parallel, a, b}), kNoEntity);
    ASSERT_NE(s.addConstraint({ConstraintKind::Perpendicular, a, c}), kNoEntity);
    ASSERT_NE(s.addConstraint({ConstraintKind::Equal, a, b}), kNoEntity);
    EXPECT_EQ(s.addConstraint({ConstraintKind::Parallel, a, a}), kNoEntity); // a line with itself
    const auto report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    EXPECT_NEAR(cross(dir(s, a), dir(s, b)), 0.0, 1e-7);
    EXPECT_NEAR(dir(s, a).x * dir(s, c).x + dir(s, a).y * dir(s, c).y, 0.0, 1e-7);
    EXPECT_NEAR(dir(s, a).length(), dir(s, b).length(), 1e-7);
}

TEST(Sketch, TangentConcentricEqualCircles)
{
    Sketch s;
    const EntityId l = line(s, {0, 0}, {40, 0});
    const EntityId c1 = s.addCircle(s.addPoint({10, 7}), 5);
    const EntityId c2 = s.addCircle(s.addPoint({11, 6}), 2);
    const EntityId c3 = s.addCircle(s.addPoint({30, 9}), 3);
    ASSERT_NE(s.addConstraint({ConstraintKind::Tangent, l, c1}), kNoEntity);
    ASSERT_NE(s.addConstraint({ConstraintKind::Concentric, c1, c2}), kNoEntity);
    ASSERT_NE(s.addConstraint({ConstraintKind::Equal, c1, c3}), kNoEntity);
    ASSERT_NE(s.addConstraint({ConstraintKind::Tangent, c1, c3}), kNoEntity);
    const auto report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    const Vec2 o1 = pos(s, s.circle(c1)->center), o3 = pos(s, s.circle(c3)->center);
    const double r1 = s.circle(c1)->radius, r3 = s.circle(c3)->radius;
    // Tangent to the line y = 0 (the line stays put: it was already horizontal
    // but free, so check the distance to the solved line instead).
    const Vec2 p = pos(s, s.line(l)->start), d = dir(s, l);
    EXPECT_NEAR(std::abs(cross(d, o1 - p)) / d.length(), r1, 1e-7);
    EXPECT_NEAR((pos(s, s.circle(c2)->center) - o1).length(), 0.0, 1e-7);
    EXPECT_NEAR(r1, r3, 1e-7);
    EXPECT_NEAR((o3 - o1).length(), r1 + r3, 1e-7); // externally tangent
}

TEST(Sketch, PointOnLineAndMidpoint)
{
    Sketch s;
    const EntityId l = line(s, {0, 0}, {20, 0});
    const EntityId p = s.addPoint({7, 3});
    const EntityId m = s.addPoint({9, -2});
    ASSERT_NE(s.addConstraint({ConstraintKind::PointOnLine, p, l}), kNoEntity);
    ASSERT_NE(s.addConstraint({ConstraintKind::Midpoint, m, l}), kNoEntity);
    EXPECT_EQ(s.addConstraint({ConstraintKind::Midpoint, s.line(l)->start, l}), kNoEntity); // an endpoint
    const auto report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    const Vec2 a = pos(s, s.line(l)->start), b = pos(s, s.line(l)->end);
    EXPECT_NEAR(cross(b - a, pos(s, p) - a), 0.0, 1e-7);
    EXPECT_NEAR((pos(s, m) - (a + b) * 0.5).length(), 0.0, 1e-7);
}

TEST(Sketch, NewConstraintKindsAndConstructionRoundTrip)
{
    Sketch s;
    const EntityId a = line(s, {0, 0}, {20, 0});
    const EntityId b = line(s, {0, 5}, {20, 6});
    const EntityId c = s.addCircle(s.addPoint({5, 20}), 3);
    s.addConstraint({ConstraintKind::Parallel, a, b});
    s.addConstraint({ConstraintKind::Tangent, b, c});
    ASSERT_TRUE(s.setConstruction(a, true));
    ASSERT_TRUE(s.setConstruction(c, true));
    EXPECT_FALSE(s.setConstruction(kOriginId, true)); // points have no construction flag
    auto back = Sketch::fromJson(s.toJson());
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    EXPECT_EQ(back.value().constraints().size(), 2u);
    EXPECT_TRUE(back.value().line(a)->construction);
    EXPECT_FALSE(back.value().line(b)->construction);
    EXPECT_TRUE(back.value().circle(c)->construction);
}

TEST(Sketch, ArcKeepsItsEndsOnTheCircle)
{
    Sketch s;
    const EntityId c = s.addPoint({0, 0});
    const EntityId a = s.addPoint({8, 0});
    const EntityId b = s.addPoint({0, 8});
    const EntityId arc = s.addArc(c, a, b);
    ASSERT_NE(arc, kNoEntity);
    EXPECT_EQ(s.addArc(c, a, a), kNoEntity); // repeated point
    auto report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    EXPECT_EQ(report.degreesOfFreedom, 5); // center (2) + radius + two angles
    ASSERT_NE(s.addConstraint({ConstraintKind::Radius, arc, kNoEntity, 12.5}), kNoEntity);
    report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    EXPECT_EQ(report.degreesOfFreedom, 4);
    EXPECT_NEAR((pos(s, a) - pos(s, c)).length(), 12.5, 1e-7);
    EXPECT_NEAR((pos(s, b) - pos(s, c)).length(), 12.5, 1e-7);
    EXPECT_NEAR(s.arcRadius(arc), 12.5, 1e-7);
}

TEST(Sketch, ArcTangentToLineAndRemoval)
{
    Sketch s;
    const EntityId l = line(s, {-20, -3}, {20, -3});
    const EntityId c = s.addPoint({0, 4});
    const EntityId arc = s.addArc(c, s.addPoint({6, 4}), s.addPoint({-6, 4}));
    ASSERT_NE(s.addConstraint({ConstraintKind::Tangent, l, arc}), kNoEntity);
    const auto report = solve(s);
    ASSERT_TRUE(report.ok) << report.message;
    const Vec2 p = pos(s, s.line(l)->start), d = dir(s, l);
    EXPECT_NEAR(std::abs(cross(d, pos(s, c) - p)) / d.length(), s.arcRadius(arc), 1e-7);
    // Removing the arc drops its now unused points and its constraint.
    const std::size_t pointsBefore = s.points().size();
    ASSERT_TRUE(s.remove(arc));
    EXPECT_EQ(s.arcs().size(), 0u);
    EXPECT_EQ(s.points().size(), pointsBefore - 3);
    EXPECT_TRUE(s.constraints().empty());
}

TEST(Sketch, ArcsRoundTripAndOldFilesLoad)
{
    Sketch s;
    const EntityId arc = s.addArc(s.addPoint({0, 0}), s.addPoint({5, 0}), s.addPoint({0, 5}), true);
    s.addConstraint({ConstraintKind::Radius, arc, kNoEntity, 5});
    auto json = s.toJson();
    auto back = Sketch::fromJson(json);
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    ASSERT_NE(back.value().arc(arc), nullptr);
    EXPECT_TRUE(back.value().arc(arc)->construction);
    EXPECT_EQ(back.value().constraints().size(), 1u);
    // A sketch saved before arcs existed has no "arcs" entry.
    Sketch plain;
    line(plain, {0, 0}, {10, 0});
    auto oldJson = plain.toJson();
    oldJson.erase("arcs");
    EXPECT_TRUE(Sketch::fromJson(oldJson).ok());
}

namespace {
double distanceToLine(const Sketch& s, EntityId line, Vec2 p)
{
    const auto* l = s.line(line);
    const Vec2 a = pos(s, l->start), b = pos(s, l->end), d = b - a;
    return std::abs(d.x * (p.y - a.y) - d.y * (p.x - a.x)) / d.length();
}
} // namespace

TEST(SketchEdit, SlotIsTwoTangentLinesAndEqualArcs)
{
    Sketch s;
    const auto slot = addSlot(s, {0, 0}, {30, 0}, 5.0, kOriginId);
    ASSERT_NE(slot.arcs[0], kNoEntity);
    ASSERT_NE(slot.arcs[1], kNoEntity);
    EXPECT_EQ(slot.centers[0], kOriginId);
    ASSERT_TRUE(solve(s).ok);
    for (const EntityId line : slot.lines)
        for (const EntityId center : slot.centers)
            EXPECT_NEAR(distanceToLine(s, line, pos(s, center)), 5.0, 1e-9);
    EXPECT_NEAR(s.arcRadius(slot.arcs[0]), 5.0, 1e-9);
    EXPECT_NEAR(s.arcRadius(slot.arcs[1]), 5.0, 1e-9);
    // Free: the second center (2) and the radius (1); the first sits on the origin.
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 3);

    // A radius on one arc sizes both.
    s.addConstraint({ConstraintKind::Radius, slot.arcs[0], kNoEntity, 4.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(s.arcRadius(slot.arcs[1]), 4.0, 1e-9);
    for (const EntityId line : slot.lines)
        EXPECT_NEAR(distanceToLine(s, line, pos(s, slot.centers[1])), 4.0, 1e-9);
}

TEST(SketchEdit, FilletCornerKeepsTheRectangleDimensions)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {60, 40}, kOriginId);
    s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60.0});
    s.addConstraint({ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 40.0});
    ASSERT_TRUE(solve(s).ok);
    ASSERT_EQ(s.solveReport().degreesOfFreedom, 0);

    const auto radius = suggestedFilletRadius(s, r.corners[2]);
    ASSERT_TRUE(radius.has_value());
    EXPECT_DOUBLE_EQ(*radius, 10.0); // a quarter of 40, rounded down to a round number

    const auto arc = filletCorner(s, r.corners[2], 5.0);
    ASSERT_TRUE(arc.ok()) << arc.developerMessage();
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0) << "still fully defined";
    EXPECT_NEAR(s.arcRadius(arc.value()), 5.0, 1e-9);
    const auto* a = s.arc(arc.value());
    EXPECT_NEAR((pos(s, a->center) - Vec2{55, 35}).length(), 0.0, 1e-9);
    // The ends touch the rectangle's top and right sides.
    std::vector<Vec2> ends{pos(s, a->start), pos(s, a->end)};
    std::sort(ends.begin(), ends.end(), [](Vec2 p, Vec2 q) { return p.x < q.x; });
    EXPECT_NEAR((ends[0] - Vec2{55, 40}).length(), 0.0, 1e-9);
    EXPECT_NEAR((ends[1] - Vec2{60, 35}).length(), 0.0, 1e-9);
    EXPECT_NEAR((pos(s, r.corners[2]) - Vec2{60, 40}).length(), 0.0, 1e-9) << "the virtual sharp stays";

    // Changing the radius moves the tangent points.
    for (const auto& [id, c] : s.constraints())
        if (c.kind == ConstraintKind::Radius)
            s.constraint(id)->value = 8.0;
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR((pos(s, s.arc(arc.value())->center) - Vec2{52, 32}).length(), 0.0, 1e-9);

    // Not a corner of two lines, or too big: refused.
    EXPECT_FALSE(filletCorner(s, r.corners[2], 50.0).ok());
    EXPECT_FALSE(filletCorner(s, s.arc(arc.value())->center, 2.0).ok());
}

TEST(SketchEdit, TrimLineEndAtACrossing)
{
    // A plus sign: trimming the right arm of the horizontal line.
    Sketch s;
    const EntityId h = s.addLine(s.addPoint({-10, 0}), s.addPoint({10, 0}));
    const EntityId v = s.addLine(s.addPoint({0, -10}), s.addPoint({0, 10}));
    ASSERT_TRUE(trimAt(s, h, {6, 0}).ok());
    const auto* line = s.line(h);
    ASSERT_NE(line, nullptr);
    EXPECT_NEAR((pos(s, line->start) - Vec2{-10, 0}).length(), 0.0, 1e-9);
    EXPECT_NEAR((pos(s, line->end) - Vec2{0, 0}).length(), 0.0, 1e-9);
    EXPECT_EQ(s.points().size(), 5u) << "origin, 4 original ends minus the trimmed one, plus the new end";
    bool kept = false;
    for (const auto& [id, c] : s.constraints())
        kept = kept || (c.kind == ConstraintKind::PointOnLine && c.a == line->end && c.b == v);
    EXPECT_TRUE(kept) << "the new end stays on the vertical line";
    EXPECT_TRUE(solve(s).ok);
}

TEST(SketchEdit, TrimMiddleSplitsAndLoneCurvesDisappear)
{
    Sketch s;
    const EntityId h = s.addLine(s.addPoint({0, 0}), s.addPoint({30, 0}));
    s.addConstraint({ConstraintKind::Horizontal, h});
    s.addLine(s.addPoint({10, -5}), s.addPoint({10, 5}));
    s.addLine(s.addPoint({20, -5}), s.addPoint({20, 5}));
    const auto preview = trimPreview(s, h, {15, 0.5});
    ASSERT_EQ(preview.size(), 2u);
    EXPECT_NEAR((preview[0] - Vec2{10, 0}).length(), 0.0, 1e-9);
    EXPECT_NEAR((preview[1] - Vec2{20, 0}).length(), 0.0, 1e-9);
    ASSERT_TRUE(trimAt(s, h, {15, 0.5}).ok());
    EXPECT_EQ(s.lines().size(), 4u) << "the middle is gone, two pieces remain";
    std::size_t horizontals = 0;
    for (const auto& [id, c] : s.constraints())
        horizontals += c.kind == ConstraintKind::Horizontal ? 1 : 0;
    EXPECT_EQ(horizontals, 2u) << "both pieces stay horizontal";
    EXPECT_TRUE(solve(s).ok);

    // A line crossing nothing goes entirely, with its points.
    Sketch lone;
    const EntityId l = lone.addLine(lone.addPoint({0, 5}), lone.addPoint({9, 5}));
    ASSERT_TRUE(trimAt(lone, l, {4, 5}).ok());
    EXPECT_TRUE(lone.lines().empty());
    EXPECT_EQ(lone.points().size(), 1u) << "only the origin";
}

TEST(SketchEdit, TrimCircleBecomesAnArc)
{
    // A Ø20 circle cut by a vertical line through its center: trim the right half.
    Sketch s;
    const EntityId circle = s.addCircle(kOriginId, 10.0);
    s.addConstraint({ConstraintKind::Diameter, circle, kNoEntity, 20.0});
    const EntityId v = s.addLine(s.addPoint({0, -15}), s.addPoint({0, 15}));
    ASSERT_TRUE(trimAt(s, circle, {10, 0}).ok());
    EXPECT_TRUE(s.circles().empty());
    ASSERT_EQ(s.arcs().size(), 1u);
    const auto& [arcId, arc] = *s.arcs().begin();
    EXPECT_NEAR(s.arcRadius(arcId), 10.0, 1e-9);
    // Counterclockwise from the top to the bottom: the left half remains.
    EXPECT_NEAR((pos(s, arc.start) - Vec2{0, 10}).length(), 0.0, 1e-9);
    EXPECT_NEAR((pos(s, arc.end) - Vec2{0, -10}).length(), 0.0, 1e-9);
    std::size_t radius = 0, onLine = 0;
    for (const auto& [id, c] : s.constraints()) {
        radius += c.kind == ConstraintKind::Radius && c.a == arcId && std::abs(c.value - 10.0) < 1e-12 ? 1 : 0;
        onLine += c.kind == ConstraintKind::PointOnLine && c.b == v ? 1 : 0;
    }
    EXPECT_EQ(radius, 1u) << "the diameter became a radius";
    EXPECT_EQ(onLine, 2u);
    EXPECT_TRUE(solve(s).ok);
}

TEST(SketchEdit, PointOnCircleConstraint)
{
    Sketch s;
    const EntityId circle = s.addCircle(kOriginId, 10.0);
    s.addConstraint({ConstraintKind::Diameter, circle, kNoEntity, 20.0});
    const EntityId p = s.addPoint({7, 9});
    EXPECT_NE(s.addConstraint({ConstraintKind::PointOnCircle, p, circle}), kNoEntity);
    EXPECT_EQ(s.addConstraint({ConstraintKind::PointOnCircle, kOriginId, circle}), kNoEntity) << "not the center";
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(pos(s, p).length(), 10.0, 1e-9);
    const auto copy = Sketch::fromJson(s.toJson());
    ASSERT_TRUE(copy.ok());
    bool found = false;
    for (const auto& [id, c] : copy.value().constraints())
        found = found || c.kind == ConstraintKind::PointOnCircle;
    EXPECT_TRUE(found);
}

TEST(SketchEdit, CenterRectangleStaysCentered)
{
    Sketch s;
    const auto ids = addCenterRectangle(s, {0, 0}, {17, 9}, kOriginId);
    ASSERT_EQ(ids.center, kOriginId);
    ASSERT_NE(ids.diagonal, kNoEntity);
    EXPECT_TRUE(s.line(ids.diagonal)->construction);
    const auto& c = ids.rectangle.corners;
    EXPECT_NEAR((pos(s, c[0]) - Vec2{-17, -9}).length(), 0.0, 1e-12);
    EXPECT_NEAR((pos(s, c[2]) - Vec2{17, 9}).length(), 0.0, 1e-12);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 2) << "width and height";
    const EntityId width = s.addConstraint({ConstraintKind::HorizontalDistance, c[0], c[1], 40.0});
    s.addConstraint({ConstraintKind::VerticalDistance, c[1], c[2], 20.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0);
    EXPECT_NEAR((pos(s, c[0]) - Vec2{-20, -10}).length(), 0.0, 1e-9);
    EXPECT_NEAR((pos(s, c[2]) - Vec2{20, 10}).length(), 0.0, 1e-9);
    // A new width grows both sides equally.
    s.constraint(width)->value = 60.0;
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(pos(s, c[0]).x, -30.0, 1e-9);
    EXPECT_NEAR(pos(s, c[1]).x, 30.0, 1e-9);
    EXPECT_NEAR(pos(s, c[3]).y, 10.0, 1e-9);
    // Round trip.
    auto back = Sketch::fromJson(s.toJson());
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    EXPECT_EQ(back.value().lines().size(), 5u);
    EXPECT_EQ(back.value().constraints().size(), s.constraints().size());
    ASSERT_TRUE(solve(back.value()).ok);
    EXPECT_EQ(back.value().solveReport().degreesOfFreedom, 0);
    // A center away from existing points gets its own point; a flat drag is refused.
    Sketch t;
    const auto free = addCenterRectangle(t, {5, 5}, {8, 7});
    ASSERT_TRUE(solve(t).ok);
    EXPECT_EQ(t.solveReport().degreesOfFreedom, 4);
    EXPECT_EQ(addCenterRectangle(t, {5, 5}, {8, 5}).center, kNoEntity);
    (void)free;
}

namespace {
// Side lengths and corner distances from the center of a polygon.
void expectRegular(const Sketch& s, const PolygonIds& ids, double apothem)
{
    const int n = int(ids.corners.size());
    const double side = 2 * apothem * std::tan(kPi / n);
    const double circumradius = apothem / std::cos(kPi / n);
    const Vec2 c = pos(s, ids.center);
    for (int i = 0; i < n; ++i) {
        const auto* l = s.line(ids.sides[i]);
        EXPECT_NEAR((pos(s, l->end) - pos(s, l->start)).length(), side, 1e-7) << "side " << i;
        EXPECT_NEAR((pos(s, ids.corners[i]) - c).length(), circumradius, 1e-7) << "corner " << i;
    }
}
} // namespace

TEST(SketchEdit, PolygonStaysRegular)
{
    // A hexagon on the origin, the middle of its first side at (5, 0): 10 across flats.
    Sketch s;
    const auto ids = addPolygon(s, {0, 0}, {5, 0}, 6, kOriginId);
    ASSERT_EQ(ids.corners.size(), 6u);
    ASSERT_EQ(ids.sides.size(), 6u);
    EXPECT_EQ(ids.center, kOriginId);
    EXPECT_TRUE(s.circle(ids.outer)->construction);
    EXPECT_TRUE(s.circle(ids.inner)->construction);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 2) << "size and rotation";
    expectRegular(s, ids, 5.0);
    // The first side is vertical at x = 5.
    EXPECT_NEAR(pos(s, s.line(ids.sides[0])->start).x, 5.0, 1e-9);
    EXPECT_NEAR(pos(s, s.line(ids.sides[0])->end).x, 5.0, 1e-9);

    // Across flats 20 and a vertical first side: fully defined.
    const EntityId size = s.addConstraint({ConstraintKind::Diameter, ids.inner, kNoEntity, 20.0});
    s.addConstraint({ConstraintKind::Vertical, ids.sides[0]});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0);
    expectRegular(s, ids, 10.0);
    // Opposite sides are 20 apart (x = 10 and x = -10).
    EXPECT_NEAR(pos(s, s.line(ids.sides[3])->start).x, -10.0, 1e-9);

    // Dragging a corner of a free polygon keeps it regular.
    s.remove(size);
    ASSERT_TRUE(solveDragging(s, ids.corners[1], {9, 14}).ok);
    const double apothem = s.circle(ids.inner)->radius;
    EXPECT_GT(apothem, 10.5);
    expectRegular(s, ids, apothem);

    // Round trip.
    auto back = Sketch::fromJson(s.toJson());
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    EXPECT_EQ(back.value().lines().size(), 6u);
    EXPECT_EQ(back.value().circles().size(), 2u);
    EXPECT_EQ(back.value().constraints().size(), s.constraints().size());
}

// The polygon's inner construction circle touches every side at its middle:
// trimming a side removes the whole side, not half of it.
TEST(SketchEdit, TrimPolygonSideIgnoresTheTouchingInnerCircle)
{
    Sketch s;
    const auto ids = addPolygon(s, {0, 0}, {5, 0}, 6, kOriginId);
    s.addConstraint({ConstraintKind::Diameter, ids.inner, kNoEntity, 10.0});
    s.addConstraint({ConstraintKind::Vertical, ids.sides[0]});
    ASSERT_TRUE(solve(s).ok);
    const double side = 10 / std::sqrt(3.0);
    // Side 0 runs up along x = 5, from y = -side/2 to side/2; its middle touches the inner circle.
    const auto preview = trimPreview(s, ids.sides[0], {5, side / 4});
    ASSERT_EQ(preview.size(), 2u);
    EXPECT_NEAR((preview[1] - preview[0]).length(), side, 1e-9) << "the whole side, not up to the middle";
    ASSERT_TRUE(trimAt(s, ids.sides[0], {5, side / 4}).ok());
    EXPECT_EQ(s.line(ids.sides[0]), nullptr);
    EXPECT_EQ(s.lines().size(), 5u) << "the other five sides stay, whole";
    for (std::size_t i = 1; i < ids.sides.size(); ++i) {
        const auto* l = s.line(ids.sides[i]);
        ASSERT_NE(l, nullptr);
        EXPECT_NEAR((pos(s, l->end) - pos(s, l->start)).length(), side, 1e-9);
    }
    EXPECT_EQ(s.circles().size(), 2u) << "the construction circles stay";
    EXPECT_TRUE(solve(s).ok);

    // Trimming the inner circle itself: nothing crosses it, so it goes entirely.
    Sketch t;
    const auto hex = addPolygon(t, {0, 0}, {5, 0}, 6, kOriginId);
    ASSERT_TRUE(solve(t).ok);
    ASSERT_TRUE(trimAt(t, hex.inner, {0, 5}).ok());
    EXPECT_EQ(t.circle(hex.inner), nullptr);
    EXPECT_TRUE(t.arcs().empty());
    EXPECT_EQ(t.lines().size(), 6u);
}

// Touching construction curves are no cuts; crossing ones and touching
// profile curves still are.
TEST(SketchEdit, TrimAgainstConstructionCurves)
{
    auto build = [](bool construction, double circleY) {
        Sketch s;
        const EntityId line = s.addLine(s.addPoint({-10, 0}), s.addPoint({10, 0}));
        s.addCircle(s.addPoint({0, circleY}), 5.0, construction);
        return std::make_pair(s, line);
    };
    {
        auto [s, line] = build(true, 5.0); // a construction circle touching the line at (0, 0)
        const auto preview = trimPreview(s, line, {5, 0});
        ASSERT_EQ(preview.size(), 2u);
        EXPECT_NEAR((preview[1] - preview[0]).length(), 20.0, 1e-9) << "nothing cuts it: the whole line";
        ASSERT_TRUE(trimAt(s, line, {5, 0}).ok());
        EXPECT_TRUE(s.lines().empty());
    }
    {
        auto [s, line] = build(false, 5.0); // a profile circle touching it: a cut at (0, 0)
        ASSERT_TRUE(trimAt(s, line, {5, 0}).ok());
        ASSERT_EQ(s.lines().size(), 1u);
        const auto* l = s.line(line);
        ASSERT_NE(l, nullptr);
        EXPECT_NEAR((pos(s, l->start) - Vec2{-10, 0}).length(), 0.0, 1e-9);
        EXPECT_NEAR((pos(s, l->end) - Vec2{0, 0}).length(), 0.0, 1e-9);
    }
    {
        auto [s, line] = build(true, 3.0); // a construction circle crossing it at x = +-4: cuts
        ASSERT_TRUE(trimAt(s, line, {7, 0}).ok());
        const auto* l = s.line(line);
        ASSERT_NE(l, nullptr);
        EXPECT_NEAR((pos(s, l->end) - Vec2{4, 0}).length(), 0.0, 1e-9);
    }
}

TEST(SketchEdit, PolygonOddCountsAndLimits)
{
    Sketch s;
    const auto pentagon = addPolygon(s, {10, 10}, {10, 14}, 5);
    ASSERT_EQ(pentagon.sides.size(), 5u);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 4) << "center, size, rotation";
    expectRegular(s, pentagon, 4.0);
    // Side 0 is horizontal (its middle is straight above the center).
    const auto* first = s.line(pentagon.sides[0]);
    EXPECT_NEAR(pos(s, first->start).y, 14.0, 1e-9);
    EXPECT_NEAR(pos(s, first->end).y, 14.0, 1e-9);
    EXPECT_TRUE(addPolygon(s, {0, 0}, {5, 0}, 2).sides.empty());
    EXPECT_TRUE(addPolygon(s, {0, 0}, {5, 0}, kMaxPolygonSides + 1).sides.empty());
    EXPECT_TRUE(addPolygon(s, {0, 0}, {0, 0}, 6).sides.empty());
    EXPECT_EQ(polygonCorners({0, 0}, {1, 0}, 64).size(), 64u);
}

namespace {
// |cross| of (p - c1) and (p - c2): zero when both centers are on one line through p.
double offLine(Vec2 p, Vec2 c1, Vec2 c2)
{
    return std::abs(cross(p - c1, p - c2)) / std::max((p - c1).length() * (p - c2).length(), 1e-12);
}
} // namespace

TEST(Sketch, ArcsTangentAtASharedEndJoinSmoothly)
{
    // Arc 1 counterclockwise from (10, 0) to (0, 10) around the origin; arc 2
    // continues it from (0, 10), turning the same way around (0, 5).
    Sketch s;
    const EntityId p = s.addPoint({0, 10});
    const EntityId a1 = s.addArc(kOriginId, s.addPoint({10, 0}), p);
    const EntityId c2 = s.addPoint({0, 5});
    const EntityId a2 = s.addArc(c2, p, s.addPoint({-5, 5}));
    ASSERT_NE(s.addConstraint({ConstraintKind::Tangent, a1, a2}), kNoEntity);
    s.addConstraint({ConstraintKind::Radius, a2, kNoEntity, 3.0});
    ASSERT_TRUE(solve(s).ok) << s.solveReport().message;
    EXPECT_NEAR(s.arcRadius(a2), 3.0, 1e-9);
    EXPECT_NEAR(offLine(pos(s, p), pos(s, kOriginId), pos(s, c2)), 0.0, 1e-9);
    // Same side: the smaller arc's center lies between the point and the big center.
    EXPECT_NEAR((pos(s, c2) - pos(s, p)).length() + (pos(s, c2) - pos(s, kOriginId)).length(),
                (pos(s, p) - pos(s, kOriginId)).length(), 1e-9);
    // Dragging the shared point keeps the join smooth.
    ASSERT_TRUE(solveDragging(s, p, {-3, 9}).ok);
    EXPECT_NEAR(offLine(pos(s, p), pos(s, kOriginId), pos(s, c2)), 0.0, 1e-7);
    EXPECT_EQ(s.solveReport().conflicting.size(), 0u);
    EXPECT_EQ(s.solveReport().redundant.size(), 0u);
}

TEST(Sketch, ArcsMeetingEndToEndMakeAnSBend)
{
    // Arc 1 ends at (0, 10) going left; arc 2 (counterclockwise from (-5, 15)
    // to (0, 10) around (0, 15)) is traversed backwards: an S-bend.
    Sketch s;
    const EntityId p = s.addPoint({0, 10});
    const EntityId a1 = s.addArc(kOriginId, s.addPoint({10, 0}), p);
    const EntityId c2 = s.addPoint({0, 15});
    const EntityId a2 = s.addArc(c2, s.addPoint({-5, 15}), p);
    ASSERT_NE(s.addConstraint({ConstraintKind::Tangent, a1, a2}), kNoEntity);
    s.addConstraint({ConstraintKind::Radius, a2, kNoEntity, 8.0});
    ASSERT_TRUE(solve(s).ok) << s.solveReport().message;
    EXPECT_NEAR(s.arcRadius(a2), 8.0, 1e-9);
    // Opposite sides: the point lies between the two centers.
    EXPECT_NEAR((pos(s, c2) - pos(s, p)).length() + (pos(s, p) - pos(s, kOriginId)).length(),
                (pos(s, c2) - pos(s, kOriginId)).length(), 1e-9);
    auto back = Sketch::fromJson(s.toJson());
    ASSERT_TRUE(back.ok());
    ASSERT_TRUE(solve(back.value()).ok);
    EXPECT_NEAR((pos(back.value(), c2) - pos(back.value(), p)).length(), 8.0, 1e-9);
}

TEST(Sketch, SymmetricConstraintAndItsThirdEntity)
{
    Sketch s;
    const EntityId axis = line(s, {0, -10}, {0, 10});
    const EntityId p = s.addPoint({3, 1});
    const EntityId q = s.addPoint({-5, 4});
    const EntityId id = s.addConstraint({ConstraintKind::Symmetric, p, q, 0.0, axis});
    ASSERT_NE(id, kNoEntity);
    EXPECT_EQ(s.addConstraint({ConstraintKind::Symmetric, p, s.line(axis)->end, 0.0, axis}), kNoEntity) << "an axis end";
    EXPECT_EQ(s.addConstraint({ConstraintKind::Symmetric, p, q, 0.0, p}), kNoEntity) << "not a line";
    EXPECT_EQ(s.addConstraint({ConstraintKind::Coincident, p, q, 0.0, axis}), kNoEntity) << "only Symmetric has a third";
    s.addConstraint({ConstraintKind::Vertical, axis});
    ASSERT_TRUE(solve(s).ok);
    const Vec2 a = pos(s, p), b = pos(s, q), x = pos(s, s.line(axis)->start);
    EXPECT_NEAR(a.x + b.x, 2 * x.x, 1e-9);
    EXPECT_NEAR(a.y, b.y, 1e-9);
    EXPECT_EQ(s.constraintsOn(axis).size(), 2u);

    // JSON: "c" is written only when used, and read back.
    const auto json = s.toJson();
    std::size_t withC = 0;
    for (const auto& c : json["constraints"])
        withC += c.contains("c") ? 1 : 0;
    EXPECT_EQ(withC, 1u);
    auto back = Sketch::fromJson(json);
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    EXPECT_EQ(back.value().constraint(id)->c, axis);
    auto broken = json;
    for (auto& c : broken["constraints"])
        if (c.contains("c"))
            c["c"] = "axis";
    EXPECT_FALSE(Sketch::fromJson(broken).ok());

    // Removing the axis drops the constraint.
    ASSERT_TRUE(s.remove(axis));
    EXPECT_EQ(s.constraint(id), nullptr);
}

TEST(SketchEdit, MirrorHalfProfileAcrossACenterLine)
{
    // Half of a 20 x 20 square against a vertical construction center line.
    Sketch s;
    const EntityId axis = line(s, {0, -5}, {0, 25});
    s.setConstruction(axis, true);
    s.addConstraint({ConstraintKind::Vertical, axis});
    const EntityId bottomEnd = s.addPoint({0, 0}), right0 = s.addPoint({10, 0});
    const EntityId right1 = s.addPoint({10, 20}), topEnd = s.addPoint({0, 20});
    const std::vector<EntityId> half{s.addLine(bottomEnd, right0), s.addLine(right0, right1), s.addLine(right1, topEnd)};
    for (const EntityId p : {bottomEnd, topEnd})
        s.addConstraint({ConstraintKind::PointOnLine, p, axis});
    ASSERT_TRUE(solve(s).ok);
    const int dofBefore = s.solveReport().degreesOfFreedom;

    const auto made = mirrorCurves(s, {half[0], half[1], half[2], axis}, axis); // the axis itself is skipped
    ASSERT_TRUE(made.ok()) << made.developerMessage();
    EXPECT_EQ(made.value().size(), 3u);
    EXPECT_EQ(s.lines().size(), 7u);
    std::size_t symmetric = 0, onLine = 0;
    for (const auto& [id, c] : s.constraints()) {
        symmetric += c.kind == ConstraintKind::Symmetric ? 1 : 0;
        onLine += c.kind == ConstraintKind::PointOnLine ? 1 : 0;
    }
    EXPECT_EQ(symmetric, 2u) << "the two corners off the axis";
    EXPECT_EQ(onLine, 2u) << "the shared ends were already held on the axis";
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, dofBefore) << "the mirror image adds no freedom";
    EXPECT_TRUE(s.solveReport().redundant.empty());

    // The copy follows the original.
    ASSERT_TRUE(solveDragging(s, right1, {14, 26}).ok);
    const double axisX = pos(s, s.line(axis)->start).x; // the (vertical) axis may slide too
    bool found = false;
    for (const auto& [id, p] : s.points())
        found = found
             || (std::abs(p.position.x - (2 * axisX - pos(s, right1).x)) < 1e-7 && std::abs(p.position.y - pos(s, right1).y) < 1e-7);
    EXPECT_TRUE(found) << "a point mirrors the dragged corner";

    // Only the axis selected, or curves on the axis: nothing to mirror, nothing changed.
    const std::size_t constraintsBefore = s.constraints().size();
    EXPECT_FALSE(mirrorCurves(s, {axis}, axis).ok());
    const Vec2 axisStart = pos(s, s.line(axis)->start), axisEnd = pos(s, s.line(axis)->end);
    const EntityId along = line(s, axisStart + (axisEnd - axisStart) * 0.25, axisStart + (axisEnd - axisStart) * 0.5);
    EXPECT_FALSE(mirrorCurves(s, {along}, axis).ok());
    EXPECT_EQ(s.constraints().size(), constraintsBefore);
}

TEST(SketchEdit, MirrorCirclesAndArcsKeepSizeAndTurn)
{
    Sketch s;
    const EntityId axis = line(s, {0, -20}, {0, 20});
    s.addConstraint({ConstraintKind::Vertical, axis});
    const EntityId circle = s.addCircle(s.addPoint({5, 5}), 2);
    const EntityId diameter = s.addConstraint({ConstraintKind::Diameter, circle, kNoEntity, 4.0});
    const EntityId arc = s.addArc(s.addPoint({8, 0}), s.addPoint({10, 0}), s.addPoint({8, 2}));
    ASSERT_TRUE(solve(s).ok);
    const int dofBefore = s.solveReport().degreesOfFreedom;
    const auto made = mirrorCurves(s, {circle, arc}, axis);
    ASSERT_TRUE(made.ok());
    ASSERT_EQ(made.value().size(), 2u);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, dofBefore);
    EXPECT_TRUE(s.solveReport().redundant.empty());
    const auto* copy = s.circle(made.value()[0]);
    ASSERT_NE(copy, nullptr);
    EXPECT_NEAR((pos(s, copy->center) - Vec2{-5, 5}).length(), 0.0, 1e-9);
    // The mirrored arc is still a short counterclockwise quarter, around (-8, 0).
    const auto* image = s.arc(made.value()[1]);
    ASSERT_NE(image, nullptr);
    EXPECT_NEAR((pos(s, image->center) - Vec2{-8, 0}).length(), 0.0, 1e-9);
    EXPECT_NEAR((pos(s, image->start) - Vec2{-8, 2}).length(), 0.0, 1e-9);
    EXPECT_NEAR((pos(s, image->end) - Vec2{-10, 0}).length(), 0.0, 1e-9);
    // A new diameter resizes both circles.
    s.constraint(diameter)->value = 7.0;
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(copy->radius, 3.5, 1e-9);
}

// A pie slice: the arc's center is also the lines' corner, so every point of
// the copy is mirrored (the copy's radius is then implied twice; PlaneGCS
// sets the duplicate aside and still solves and drags).
TEST(SketchEdit, MirrorAPieSlice)
{
    Sketch s;
    const EntityId axis = line(s, {-2, -20}, {-2, 20});
    s.addConstraint({ConstraintKind::Vertical, axis});
    const EntityId corner = s.addPoint({5, 0}), from = s.addPoint({15, 0}), to = s.addPoint({5, 10});
    const EntityId arc = s.addArc(corner, from, to);
    const std::vector<EntityId> slice{s.addLine(corner, from), s.addLine(to, corner), arc};
    ASSERT_TRUE(solve(s).ok);
    const auto made = mirrorCurves(s, slice, axis);
    ASSERT_TRUE(made.ok());
    ASSERT_TRUE(solve(s).ok) << s.solveReport().message;
    EXPECT_TRUE(s.solveReport().conflicting.empty());
    ASSERT_TRUE(solveDragging(s, from, {18, 3}).ok);
    const double axisX = pos(s, s.line(axis)->start).x;
    const auto* image = s.arc(made.value()[2]);
    ASSERT_NE(image, nullptr);
    EXPECT_NEAR(s.arcRadius(made.value()[2]), s.arcRadius(arc), 1e-7);
    EXPECT_NEAR(pos(s, image->center).x, 2 * axisX - pos(s, corner).x, 1e-7);
    EXPECT_NEAR(pos(s, image->end).x, 2 * axisX - pos(s, from).x, 1e-7) << "the image of the arc's start ends it";
}

TEST(SketchEdit, LinearPatternCopiesShapesAndConstraints)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {10, 5}, kOriginId);
    const EntityId hole = s.addCircle(s.addPoint({5, 2.5}), 1.5);
    const std::vector<EntityId> curves{r.edges[0], r.edges[1], r.edges[2], r.edges[3], hole};
    PatternLayout layout;
    layout.step = {15, 0};
    layout.count = 3;
    const auto made = patternCurves(s, curves, layout);
    ASSERT_TRUE(made.ok()) << made.developerMessage();
    EXPECT_EQ(made.value().size(), 10u);
    EXPECT_EQ(s.lines().size(), 12u);
    EXPECT_EQ(s.circles().size(), 3u);
    std::size_t hv = 0, equal = 0;
    for (const auto& [id, c] : s.constraints()) {
        hv += c.kind == ConstraintKind::Horizontal || c.kind == ConstraintKind::Vertical ? 1 : 0;
        equal += c.kind == ConstraintKind::Equal ? 1 : 0;
    }
    EXPECT_EQ(hv, 12u) << "each copy keeps its right angles";
    EXPECT_EQ(equal, 2u) << "the copied holes keep the first hole's size";
    ASSERT_TRUE(solve(s).ok);
    EXPECT_TRUE(s.solveReport().redundant.empty());
    double farthest = 0;
    for (const auto& [id, p] : s.points())
        farthest = std::max(farthest, p.position.x);
    EXPECT_NEAR(farthest, 40.0, 1e-12);
    // The holes follow the first one's diameter.
    s.addConstraint({ConstraintKind::Diameter, hole, kNoEntity, 4.0});
    ASSERT_TRUE(solve(s).ok);
    for (const auto& [id, c] : s.circles())
        EXPECT_NEAR(c.radius, 2.0, 1e-9);
    // A zero step or a single item is refused.
    layout.step = {0, 0};
    EXPECT_FALSE(patternCurves(s, curves, layout).ok());
    layout.step = {1, 0};
    layout.count = 1;
    EXPECT_FALSE(patternCurves(s, curves, layout).ok());
}

TEST(SketchEdit, CircularPatternSharesTheCenterAndSpacesEvenly)
{
    Sketch s;
    const EntityId spoke = s.addLine(kOriginId, s.addPoint({10, 0}));
    const EntityId hole = s.addCircle(s.addPoint({20, 0}), 2);
    PatternLayout layout;
    layout.circular = true;
    layout.center = {0, 0};
    layout.count = 4;
    const auto made = patternCurves(s, {spoke, hole}, layout);
    ASSERT_TRUE(made.ok());
    EXPECT_EQ(s.lines().size(), 4u);
    for (const auto& [id, l] : s.lines())
        EXPECT_EQ(l.start, kOriginId) << "every spoke starts on the shared center";
    std::vector<Vec2> centers;
    for (const auto& [id, c] : s.circles())
        centers.push_back(pos(s, c.center));
    ASSERT_EQ(centers.size(), 4u);
    EXPECT_NEAR((centers[1] - Vec2{0, 20}).length(), 0.0, 1e-9);
    EXPECT_NEAR((centers[2] - Vec2{-20, 0}).length(), 0.0, 1e-9);
    EXPECT_NEAR((centers[3] - Vec2{0, -20}).length(), 0.0, 1e-9);
    ASSERT_TRUE(solve(s).ok);

    // A quarter turn with three items: 0, 45 and 90 degrees.
    Sketch t;
    const EntityId h = t.addCircle(t.addPoint({10, 0}), 1);
    layout.count = 3;
    layout.angle = kPi / 2;
    ASSERT_TRUE(patternCurves(t, {h}, layout).ok());
    std::vector<Vec2> at;
    for (const auto& [id, c] : t.circles())
        at.push_back(pos(t, c.center));
    ASSERT_EQ(at.size(), 3u);
    EXPECT_NEAR((at[1] - Vec2{10 * std::cos(kPi / 4), 10 * std::sin(kPi / 4)}).length(), 0.0, 1e-9);
    EXPECT_NEAR((at[2] - Vec2{0, 10}).length(), 0.0, 1e-9);
    const auto motions = patternMotions(layout);
    ASSERT_EQ(motions.size(), 2u);
    EXPECT_NEAR(motions[1].angle, kPi / 2, 1e-12);
}

TEST(Sketch, AngleBetweenTwoLines)
{
    // A "V" at the origin: a runs towards the corner, b away from it.
    Sketch s;
    const EntityId a = s.addLine(s.addPoint({10, 0}), kOriginId);
    const EntityId b = s.addLine(kOriginId, s.addPoint({5, 8}));
    s.addConstraint({ConstraintKind::Horizontal, a});
    const auto now = lineDirectionAngle(s, a, b);
    ASSERT_TRUE(now.has_value());
    EXPECT_NEAR(*visibleAngle(s, a, b, *now), std::atan2(8.0, 5.0), 1e-12) << "the corner's own angle";
    const auto sixty = directionAngleFor(s, a, b, kPi / 3);
    ASSERT_TRUE(sixty.has_value());
    const EntityId angle = s.addConstraint({ConstraintKind::Angle, a, b, *sixty});
    ASSERT_NE(angle, kNoEntity);
    ASSERT_TRUE(solve(s).ok) << s.solveReport().message;
    const Vec2 tip = pos(s, s.line(b)->end);
    EXPECT_NEAR(std::atan2(tip.y, tip.x), kPi / 3, 1e-9);
    EXPECT_NEAR(*visibleAngle(s, a, b, s.constraint(angle)->value), kPi / 3, 1e-9);
    // Wider than a right angle, then back.
    s.constraint(angle)->value = *directionAngleFor(s, a, b, 2 * kPi / 3);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(std::atan2(pos(s, s.line(b)->end).y, pos(s, s.line(b)->end).x), 2 * kPi / 3, 1e-9);
    // Round trip; parallel lines have no angle.
    auto back = Sketch::fromJson(s.toJson());
    ASSERT_TRUE(back.ok()) << back.developerMessage();
    EXPECT_EQ(back.value().constraint(angle)->kind, ConstraintKind::Angle);
    EXPECT_NEAR(back.value().constraint(angle)->value, s.constraint(angle)->value, 1e-15);
    EXPECT_TRUE(back.value().constraint(angle)->isDimension());
    const EntityId p = line(s, {0, 5}, {10, 5});
    EXPECT_FALSE(directionAngleFor(s, a, p, 1.0).has_value());
    EXPECT_FALSE(lineIntersection(s, a, p).has_value());
    EXPECT_EQ(s.addConstraint({ConstraintKind::Angle, a, a, 1.0}), kNoEntity);
}
