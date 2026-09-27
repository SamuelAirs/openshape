// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Dragging sketch geometry in the solver: several pulls at once (a line, a
// whole shape), a circle's or arc's rim, what can still move, and the edits
// behind "drop a point on another one" and "select the connected chain".
#include "sketch/Sketch.h"
#include "sketch/SketchEdit.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace os;
using namespace os::sketch;

namespace {

Vec2 pos(const Sketch& s, EntityId id)
{
    return s.point(id)->position;
}

// Pulls every point of `ids` by `delta` (a curve or shape moved as a whole).
std::vector<DragTarget> moved(const Sketch& s, const std::vector<EntityId>& ids, Vec2 delta)
{
    std::vector<DragTarget> targets;
    for (EntityId id : ids)
        targets.push_back(DragTarget::point(id, pos(s, id) + delta));
    return targets;
}

std::size_t count(const Sketch& s, ConstraintKind kind)
{
    return std::size_t(std::count_if(s.constraints().begin(), s.constraints().end(),
                                     [&](const auto& c) { return c.second.kind == kind; }));
}

} // namespace

TEST(SketchDrag, AFreeLineTranslates)
{
    Sketch s;
    const EntityId a = s.addPoint({0, 0}), b = s.addPoint({10, 0});
    const EntityId line = s.addLine(a, b);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_TRUE(s.solveReport().canMove(line));
    ASSERT_TRUE(solveDragging(s, moved(s, {a, b}, {5, 3})).ok);
    EXPECT_NEAR(pos(s, a).x, 5, 1e-9);
    EXPECT_NEAR(pos(s, a).y, 3, 1e-9);
    EXPECT_NEAR(pos(s, b).x, 15, 1e-9);
    EXPECT_NEAR(pos(s, b).y, 3, 1e-9);
}

TEST(SketchDrag, ALineWithALengthKeepsIt)
{
    Sketch s;
    const EntityId a = s.addPoint({0, 0}), b = s.addPoint({10, 0});
    s.addLine(a, b);
    s.addConstraint({ConstraintKind::Distance, a, b, 10.0});
    ASSERT_TRUE(solve(s).ok);
    // The whole line: it moves as it is.
    ASSERT_TRUE(solveDragging(s, moved(s, {a, b}, {2, 7})).ok);
    EXPECT_NEAR(pos(s, a).x, 2, 1e-6);
    EXPECT_NEAR(pos(s, a).y, 7, 1e-6);
    EXPECT_NEAR(pos(s, b).x, 12, 1e-6);
    EXPECT_NEAR(pos(s, b).y, 7, 1e-6);
    // One end, across: it reaches the pointer, the length stays (the line turns).
    ASSERT_TRUE(solveDragging(s, b, {2, 17}).ok);
    EXPECT_NEAR(pos(s, b).x, 2, 1e-6);
    EXPECT_NEAR(pos(s, b).y, 17, 1e-6);
    EXPECT_NEAR((pos(s, b) - pos(s, a)).length(), 10, 1e-9);
}

TEST(SketchDrag, ALineWithAFixedEndPivots)
{
    // From the (fixed) origin, 10 long: a drag turns it about the origin.
    Sketch s;
    const EntityId b = s.addPoint({10, 0});
    const EntityId line = s.addLine(kOriginId, b);
    s.addConstraint({ConstraintKind::Distance, kOriginId, b, 10.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_TRUE(s.solveReport().canMove(line));
    EXPECT_TRUE(s.solveReport().canMove(b));
    EXPECT_FALSE(s.solveReport().canMove(kOriginId));
    // The line pulled up by 5: its free end goes as far as the length allows.
    ASSERT_TRUE(solveDragging(s, moved(s, {kOriginId, b}, {0, 5})).ok);
    EXPECT_EQ(pos(s, kOriginId).x, 0.0);
    EXPECT_EQ(pos(s, kOriginId).y, 0.0);
    EXPECT_NEAR(pos(s, b).length(), 10, 1e-9);
    const Vec2 expected = Vec2{10, 5} * (10 / Vec2{10, 5}.length());
    EXPECT_NEAR(pos(s, b).x, expected.x, 1e-4);
    EXPECT_NEAR(pos(s, b).y, expected.y, 1e-4);
    // Without the length, the free end simply follows.
    Sketch t;
    const EntityId c = t.addPoint({10, 0});
    t.addLine(kOriginId, c);
    ASSERT_TRUE(solveDragging(t, c, {7, 9}).ok);
    EXPECT_NEAR(pos(t, c).x, 7, 1e-9);
    EXPECT_NEAR(pos(t, c).y, 9, 1e-9);
}

TEST(SketchDrag, AFullyConstrainedRectangleDoesNotMove)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {20, 10}, kOriginId);
    s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 20.0});
    s.addConstraint({ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 10.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0);
    EXPECT_TRUE(s.solveReport().movable.empty());
    for (const EntityId e : r.edges)
        EXPECT_FALSE(s.solveReport().canMove(e));
    const Sketch before = s;
    // Its top side dragged up and sideways: nothing gives.
    ASSERT_TRUE(solveDragging(s, moved(s, {r.corners[2], r.corners[3]}, {3, 4})).ok);
    for (const auto& [id, p] : s.points()) {
        EXPECT_NEAR(p.position.x, before.point(id)->position.x, 1e-9);
        EXPECT_NEAR(p.position.y, before.point(id)->position.y, 1e-9);
    }
}

TEST(SketchDrag, HorizontalAndVerticalHoldWhileASideMoves)
{
    // A free rectangle: its top side follows the pointer; the sides stay
    // vertical (so the rectangle shifts sideways with it) and grow.
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {20, 10});
    ASSERT_TRUE(solve(s).ok);
    ASSERT_TRUE(solveDragging(s, moved(s, {r.corners[2], r.corners[3]}, {3, 4})).ok);
    EXPECT_NEAR(pos(s, r.corners[2]).x, 23, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[2]).y, 14, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[3]).x, 3, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[3]).y, 14, 1e-6);
    // Horizontal and vertical hold exactly; the bottom stays where it was.
    EXPECT_NEAR(pos(s, r.corners[0]).y, pos(s, r.corners[1]).y, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[2]).y, pos(s, r.corners[3]).y, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[1]).x, pos(s, r.corners[2]).x, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[0]).x, pos(s, r.corners[3]).x, 1e-9);
    EXPECT_NEAR(pos(s, r.corners[0]).y, 0, 1e-6);
    // The whole rectangle: moved as it is.
    ASSERT_TRUE(solveDragging(s, moved(s, {r.corners[0], r.corners[1], r.corners[2], r.corners[3]}, {-5, 2})).ok);
    EXPECT_NEAR(pos(s, r.corners[0]).x, -2, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[0]).y, 2, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[2]).x, 18, 1e-6);
    EXPECT_NEAR(pos(s, r.corners[2]).y, 16, 1e-6);
}

TEST(SketchDrag, ACircleRimSetsTheRadius)
{
    Sketch s;
    const EntityId center = s.addPoint({4, 2});
    const EntityId circle = s.addCircle(center, 5);
    ASSERT_TRUE(solve(s).ok);
    ASSERT_TRUE(solveDragging(s, {DragTarget::rim(circle, {4, 10})}).ok);
    EXPECT_NEAR(s.circle(circle)->radius, 8, 1e-6);
    EXPECT_NEAR(pos(s, center).x, 4, 1e-6);
    EXPECT_NEAR(pos(s, center).y, 2, 1e-6);

    // With its diameter fixed the rim cannot follow by growing: the circle
    // moves to pass through the pointer instead.
    s.addConstraint({ConstraintKind::Diameter, circle, kNoEntity, 10.0});
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(s.circle(circle)->radius, 5, 1e-9);
    ASSERT_TRUE(solveDragging(s, {DragTarget::rim(circle, {4, 10})}).ok);
    EXPECT_NEAR(s.circle(circle)->radius, 5, 1e-9);
    EXPECT_NEAR(pos(s, center).y, 5, 1e-2);
    EXPECT_NEAR(pos(s, center).x, 4, 1e-6);
}

TEST(SketchDrag, AnArcRimSetsTheRadiusAndItsEndsFollow)
{
    Sketch s;
    const EntityId c = s.addPoint({0, 0}), a = s.addPoint({5, 0}), b = s.addPoint({0, 5});
    const EntityId arc = s.addArc(c, a, b);
    ASSERT_TRUE(solve(s).ok);
    ASSERT_TRUE(solveDragging(s, {DragTarget::rim(arc, Vec2{1, 1} * (8 / std::sqrt(2.0)))}).ok);
    EXPECT_NEAR(s.arcRadius(arc), 8, 1e-6);
    EXPECT_NEAR(pos(s, c).length(), 0, 1e-6);
    // The ends move out along their radii (the arc keeps its angles).
    EXPECT_NEAR(pos(s, a).x, 8, 1e-3);
    EXPECT_NEAR(pos(s, a).y, 0, 1e-3);
    EXPECT_NEAR(pos(s, b).x, 0, 1e-3);
    EXPECT_NEAR(pos(s, b).y, 8, 1e-3);
}

TEST(SketchDrag, WhatCanMoveItemByItem)
{
    // A rectangle anchored at the origin with only its width fixed: every
    // corner can still move up or down except the origin; the bottom side
    // (at the origin, horizontal, width fixed) cannot move at all.
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {20, 10}, kOriginId);
    s.addConstraint({ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 20.0});
    // A separate, free circle.
    const EntityId circle = s.addCircle(s.addPoint({40, 0}), 3);
    ASSERT_TRUE(solve(s).ok);
    const SolveReport& report = s.solveReport();
    EXPECT_EQ(report.degreesOfFreedom, 4); // the height, the circle's center and radius
    EXPECT_FALSE(report.canMove(kOriginId));
    EXPECT_FALSE(report.canMove(r.corners[1])) << "on the horizontal line through the origin, 20 away";
    EXPECT_FALSE(report.canMove(r.edges[0])) << "the bottom side";
    EXPECT_TRUE(report.canMove(r.corners[2]));
    EXPECT_TRUE(report.canMove(r.edges[2])) << "the top side (the height is free)";
    EXPECT_TRUE(report.canMove(r.edges[1])) << "the right side (it can grow)";
    EXPECT_TRUE(report.canMove(circle));
    // Its height fixed too: only the circle can move.
    s.addConstraint({ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 10.0});
    ASSERT_TRUE(solve(s).ok);
    for (const EntityId e : r.edges)
        EXPECT_FALSE(s.solveReport().canMove(e));
    EXPECT_TRUE(s.solveReport().canMove(circle));
    // Nothing constrained at all: everything but the origin moves.
    Sketch free;
    const EntityId p = free.addPoint({1, 1});
    const EntityId line = free.addLine(p, free.addPoint({5, 5}));
    ASSERT_TRUE(solve(free).ok);
    EXPECT_TRUE(free.solveReport().canMove(p));
    EXPECT_TRUE(free.solveReport().canMove(line));
    EXPECT_FALSE(free.solveReport().canMove(kOriginId));
}

TEST(SketchDrag, MergingAPointJoinsTheCurves)
{
    // Two separate lines; the end of the second is dropped on the end of the first.
    Sketch s;
    const EntityId a = s.addPoint({0, 0}), b = s.addPoint({10, 0});
    const EntityId c = s.addPoint({10.2, 0.1}), d = s.addPoint({10, 8});
    const EntityId first = s.addLine(a, b), second = s.addLine(c, d);
    s.addConstraint({ConstraintKind::Vertical, second});
    s.addConstraint({ConstraintKind::Coincident, c, b}); // becomes meaningless: goes
    const std::size_t points = s.points().size();
    ASSERT_TRUE(mergePoints(s, c, b).ok());
    EXPECT_EQ(s.points().size(), points - 1);
    EXPECT_EQ(s.point(c), nullptr);
    EXPECT_EQ(s.line(second)->start, b);
    EXPECT_EQ(count(s, ConstraintKind::Coincident), 0u);
    EXPECT_EQ(count(s, ConstraintKind::Vertical), 1u);
    ASSERT_TRUE(solve(s).ok);
    EXPECT_NEAR(pos(s, d).x, pos(s, b).x, 1e-9);
    EXPECT_EQ(connectedCurves(s, first), (std::vector<EntityId>{first, second}));
    // A line's own ends are never merged; a fixed point is never merged away.
    EXPECT_FALSE(mergePoints(s, d, b).ok());
    EXPECT_FALSE(mergePoints(s, kOriginId, a).ok());
    EXPECT_NE(s.line(second), nullptr);
}

TEST(SketchDrag, ConnectedChains)
{
    Sketch s;
    const auto r = addRectangle(s, {0, 0}, {20, 10});
    const EntityId circle = s.addCircle(s.addPoint({40, 0}), 3);
    // A line joined to the rectangle only by a Coincident constraint.
    const EntityId e = s.addPoint({20, 10}), f = s.addPoint({30, 20});
    const EntityId tail = s.addLine(e, f);
    s.addConstraint({ConstraintKind::Coincident, e, r.corners[2]});
    // And one on its own.
    const EntityId alone = s.addLine(s.addPoint({50, 50}), s.addPoint({60, 50}));
    std::vector<EntityId> rectangle(std::begin(r.edges), std::end(r.edges));
    rectangle.push_back(tail);
    std::sort(rectangle.begin(), rectangle.end());
    EXPECT_EQ(connectedCurves(s, r.edges[2]), rectangle);
    EXPECT_EQ(connectedCurves(s, tail), rectangle);
    EXPECT_EQ(connectedCurves(s, circle), std::vector<EntityId>{circle});
    EXPECT_EQ(connectedCurves(s, alone), std::vector<EntityId>{alone});
    EXPECT_TRUE(connectedCurves(s, r.corners[0]).empty()) << "a point is no curve";
    EXPECT_EQ(curvePoints(s, r.edges[0]), (std::vector<EntityId>{r.corners[0], r.corners[1]}));
    EXPECT_EQ(curvePoints(s, circle).size(), 1u);
}
