// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sketching on a phone (owner, from an iPhone, 2026-09-27: "It's difficult
// to sketch on iPhone"; "if I start to draw on an object, I should have quick
// lock on points. Like if I have a cube and draw on one face, the four
// corners of the square face I'm drawing on should be apparent as lock on
// points"). Driven through the interaction layer with finger input at the
// iPhone 16 Pro size (402 x 874, safe area 62/0/34/0) and with a mouse; every
// check measures coordinates, constraint kinds, areas or volumes.
#include "commands/Command.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchGestures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace os;
using namespace os::interact;

namespace {

constexpr double kCube = 8000; // the 20 mm cube: (-10,-10,0) .. (10,10,20)

struct Phone {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    PointerDevice device = PointerDevice::Touch;
    std::vector<std::string> messages;

    explicit Phone(bool touch = true)
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize(touch ? Vec2{402, 874} : Vec2{1200, 800});
        controller.setTouchLayout(touch);
        if (touch) {
            controller.setSafeInsets({62, 0, 34, 0});
            controller.setFrameInsets({126, 0, 128, 0}); // Main.qml's compact sketch controls
        }
        device = touch ? PointerDevice::Touch : PointerDevice::Mouse;
    }

    PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left) const
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        e.device = device;
        return e;
    }
    void tap(Vec2 p)
    {
        if (device == PointerDevice::Mouse)
            controller.pointerMove(at(p, PointerButton::None));
        controller.pointerPress(at(p));
        controller.pointerRelease(at(p));
    }
    void drag(Vec2 from, Vec2 to, int steps = 12)
    {
        controller.pointerPress(at(from));
        for (int i = 1; i <= steps; ++i)
            controller.pointerMove(at(from + (to - from) * (double(i) / steps)));
        controller.pointerRelease(at(to));
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    Vec2 sketchScreen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }
    SketchSession& session() { return *controller.sketchSession(); }
    const sketch::Sketch& sketch() { return session().sketch(); }
    double volume(std::size_t i = 0) const { return geom::volume(document.bodies()[i]->shape()); }

    void box()
    {
        ASSERT_TRUE(controller.createBox(20).ok());
        controller.fitAll(false);
    }
    void sketchOnTop(Vec2 where = {0, 0})
    {
        tap(screen({where.x, where.y, 20}));
        ASSERT_TRUE(controller.triggerAction("sketch").ok());
        controller.skipAnimation();
        ASSERT_EQ(controller.mode(), InteractionController::Mode::Sketch);
    }
    void finish()
    {
        controller.finishSketch();
        controller.skipAnimation();
    }
    std::string hint()
    {
        for (const auto& l : session().labels(controller.camera()))
            if (l.kind == SketchLabel::Kind::Hint)
                return l.text;
        return {};
    }
    // The positions of the sketch's points other than the origin.
    std::vector<Vec2> points()
    {
        std::vector<Vec2> out;
        for (const auto& [id, p] : sketch().points())
            if (id != sketch::kOriginId)
                out.push_back(p.position);
        return out;
    }
    bool hasPoint(Vec2 p, double tolerance = 1e-9)
    {
        const auto all = points();
        return std::any_of(all.begin(), all.end(), [&](Vec2 q) { return (q - p).length() <= tolerance; });
    }
    int constraintsOfKind(sketch::ConstraintKind kind)
    {
        int n = 0;
        for (const auto& [id, c] : sketch().constraints())
            n += c.kind == kind ? 1 : 0;
        return n;
    }
};

using K = sketch::ConstraintKind;

} // namespace

// The face's outline is reference geometry: 4 edges, 4 corners, 4 edge
// middles and the center; drawn faintly, never a profile.
TEST(SketchPhone, TheFaceOutlineIsReferenceGeometry)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    const SketchReferences& refs = p.session().references();
    ASSERT_EQ(refs.curves.size(), 4u);
    int corners = 0, middles = 0, centers = 0;
    for (const auto& r : refs.points) {
        corners += r.kind == SketchReferences::PointKind::Corner;
        middles += r.kind == SketchReferences::PointKind::Midpoint;
        centers += r.kind == SketchReferences::PointKind::Center;
    }
    EXPECT_EQ(corners, 4);
    EXPECT_EQ(middles, 4);
    EXPECT_EQ(centers, 1);
    for (const Vec2 c : {Vec2{-10, -10}, Vec2{10, -10}, Vec2{10, 10}, Vec2{-10, 10}})
        EXPECT_TRUE(std::any_of(refs.points.begin(), refs.points.end(),
                                [&](const auto& r) { return (r.position - c).length() < 1e-9; }));
    // Drawn as faint marks; not a region, not saved in the sketch.
    const RenderSketch drawn = p.session().renderData(p.controller.camera());
    const auto faint = std::count_if(drawn.lines.begin(), drawn.lines.end(),
                                     [](const RenderSketchLine& l) { return l.style == SketchStyle::FaceReference; });
    EXPECT_EQ(faint, 4);
    EXPECT_TRUE(p.session().regions().empty());
    EXPECT_TRUE(p.sketch().lines().empty());
    EXPECT_EQ(p.sketch().points().size(), 1u) << "only the origin";
}

// The owner's case: a rectangle drawn with two finger taps near two opposite
// corners of the top face is exactly the face (20 x 20), and cutting it 5 mm
// deep removes 2000 mm3.
TEST(SketchPhone, RectangleTappedOnTwoOppositeCornersIsTheFace)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    ASSERT_EQ(p.session().tool(), SketchTool::Rectangle);
    // A finger lands off target: 9 px and 7 px beside the corners.
    p.tap(p.sketchScreen({-10, -10}) + Vec2{9, -7});
    ASSERT_TRUE(p.session().isDrawing());
    p.tap(p.sketchScreen({10, 10}) + Vec2{-8, 6});
    EXPECT_FALSE(p.session().isDrawing());
    ASSERT_EQ(p.sketch().lines().size(), 4u);
    for (const Vec2 c : {Vec2{-10, -10}, Vec2{10, -10}, Vec2{10, 10}, Vec2{-10, 10}})
        EXPECT_TRUE(p.hasPoint(c)) << c.x << ", " << c.y;
    ASSERT_EQ(p.session().regions().size(), 1u);
    EXPECT_NEAR(p.session().regions().front().area, 400.0, 1e-6);
    // A snapped point gets its position, no constraint to the body.
    EXPECT_EQ(p.constraintsOfKind(K::Coincident), 0);
    EXPECT_EQ(p.constraintsOfKind(K::PointOnLine), 0);
    EXPECT_EQ(p.constraintsOfKind(K::Midpoint), 0);
    EXPECT_GT(p.sketch().solveReport().degreesOfFreedom, 0);

    p.finish();
    p.tap(p.screen({0, 0, 20}));
    ASSERT_NE(p.controller.operation(), nullptr);
    EXPECT_EQ(p.controller.setValueText("-5"), "");
    ASSERT_TRUE(p.controller.commitOperation().ok());
    EXPECT_NEAR(p.volume(), kCube - 2000, 1e-6);
}

TEST(SketchPhone, MouseClicksNearTheCornersAlsoSnap)
{
    Phone p(false);
    p.box();
    p.sketchOnTop();
    p.tap(p.sketchScreen({10, -10}) + Vec2{4, 3});
    p.tap(p.sketchScreen({-10, 10}) + Vec2{-3, -4});
    ASSERT_EQ(p.sketch().lines().size(), 4u);
    EXPECT_TRUE(p.hasPoint({10, -10}));
    EXPECT_TRUE(p.hasPoint({-10, 10}));
    ASSERT_EQ(p.session().regions().size(), 1u);
    EXPECT_NEAR(p.session().regions().front().area, 400.0, 1e-6);
}

// Edge middles and points along the edges; the hint says which.
TEST(SketchPhone, EdgeMiddlesAndEdgesSnapWithAHint)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.controller.setSketchTool(SketchTool::Line);
    // A finger pressed 8 px from the middle of the right edge: (10, 0).
    p.controller.pointerPress(p.at(p.sketchScreen({10, 0}) + Vec2{-6, 5}));
    EXPECT_EQ(p.hint(), "Midpoint");
    const SketchSession::SnapInfo info = p.session().snapInfo(p.controller.camera());
    EXPECT_TRUE(info.active);
    EXPECT_EQ(info.hint, "Midpoint");
    EXPECT_NEAR((info.screen - p.sketchScreen({10, 0})).length(), 0.0, 1e-6);
    p.controller.pointerRelease(p.at(p.sketchScreen({10, 0}) + Vec2{-6, 5}));
    // Then a tap near the middle of the top edge: (0, 10).
    p.tap(p.sketchScreen({0, 10}) + Vec2{5, 7});
    ASSERT_EQ(p.sketch().lines().size(), 1u);
    EXPECT_TRUE(p.hasPoint({10, 0}));
    EXPECT_TRUE(p.hasPoint({0, 10}));
    p.controller.keyPress(Key::Escape);

    // Along an edge, away from its middle and corners: on the edge exactly.
    const Vec2 nearEdge = p.sketchScreen({4.3, -10}) + Vec2{0, -9}; // 9 px inside the face
    p.controller.pointerPress(p.at(nearEdge));
    EXPECT_EQ(p.hint(), "On edge");
    p.controller.pointerRelease(p.at(nearEdge));
    ASSERT_TRUE(p.session().isDrawing());
    p.tap(p.sketchScreen({4.3, 5}));
    ASSERT_EQ(p.sketch().lines().size(), 2u);
    bool onEdge = false;
    for (const Vec2 q : p.points())
        onEdge = onEdge || (std::abs(q.y + 10) < 1e-9 && std::abs(q.x - 4.3) < 0.2);
    EXPECT_TRUE(onEdge) << "a point on the face's edge y = -10";
    EXPECT_EQ(p.constraintsOfKind(K::PointOnLine), 0) << "nothing ties it to the body";
}

// The hole in a face: its center is a snap target ("Center").
TEST(SketchPhone, AHoleCenterIsASnapTarget)
{
    Phone p(false);
    p.box();
    p.sketchOnTop();
    p.controller.setSketchTool(SketchTool::Circle);
    p.tap(p.sketchScreen({3, 4}));
    p.tap(p.sketchScreen({6, 4}));
    ASSERT_EQ(p.sketch().circles().size(), 1u);
    p.finish();
    p.tap(p.screen({3, 4, 20}));
    ASSERT_NE(p.controller.operation(), nullptr);
    EXPECT_EQ(p.controller.setValueText("-20"), "");
    ASSERT_TRUE(p.controller.commitOperation().ok());
    EXPECT_NEAR(p.volume(), kCube - kPi * 9 * 20, 1e-3);

    // A new sketch on the drilled face (the old one is continued: same plane).
    p.tap(p.screen({-6, -6, 20}));
    ASSERT_TRUE(p.controller.triggerAction("sketch").ok());
    p.controller.skipAnimation();
    ASSERT_EQ(p.controller.mode(), InteractionController::Mode::Sketch);
    const auto& refs = p.session().references();
    EXPECT_TRUE(std::any_of(refs.curves.begin(), refs.curves.end(),
                            [](const auto& c) { return c.kind == SketchReferences::Curve::Kind::Circle && std::abs(c.radius - 3) < 1e-9; }));
    p.controller.setSketchTool(SketchTool::Line);
    // The sketch's own circle center (3, 4) sits on the hole's center: the
    // sketch point wins and reads "Center".
    p.controller.pointerMove(p.at(p.sketchScreen({3, 4}) + Vec2{3, 2}, PointerButton::None));
    EXPECT_EQ(p.hint(), "Center");
}

// Snapping onto the sketch's own curves while drawing holds the new point
// there (On line, On circle, Midpoint): it moves with them.
TEST(SketchPhone, PointsDrawnOnSketchCurvesAreHeldThere)
{
    Phone p(false);
    ASSERT_TRUE(p.controller.startSketch().ok());
    p.controller.skipAnimation();
    p.controller.setSketchTool(SketchTool::Line);
    p.tap(p.sketchScreen({0, 0}) + Vec2{200, 0}); // somewhere right of the origin
    p.tap(p.sketchScreen({0, 0}) + Vec2{200, -160});
    p.controller.keyPress(Key::Escape);
    ASSERT_EQ(p.sketch().lines().size(), 1u);
    const auto [lineId, line] = *p.sketch().lines().begin();
    const Vec2 a = p.sketch().point(line.start)->position, b = p.sketch().point(line.end)->position;
    // A line from a point on it (a third of the way), and one from its middle.
    const Vec2 third = a + (b - a) * (1.0 / 3.0);
    p.tap(p.sketchScreen(third) + Vec2{4, 0});
    p.tap(p.sketchScreen(third) + Vec2{-120, 0});
    p.controller.keyPress(Key::Escape);
    EXPECT_EQ(p.constraintsOfKind(K::PointOnLine), 1);
    p.tap(p.sketchScreen((a + b) * 0.5) + Vec2{3, 0});
    p.tap(p.sketchScreen((a + b) * 0.5) + Vec2{150, 0});
    p.controller.keyPress(Key::Escape);
    EXPECT_EQ(p.constraintsOfKind(K::Midpoint), 1);
    // A circle's center on the line.
    p.controller.setSketchTool(SketchTool::Circle);
    const Vec2 twoThirds = a + (b - a) * (2.0 / 3.0);
    p.tap(p.sketchScreen(twoThirds) + Vec2{3, 1});
    p.tap(p.sketchScreen(twoThirds) + Vec2{40, 0});
    EXPECT_EQ(p.constraintsOfKind(K::PointOnLine), 2);
    // A line ending on the circle.
    p.controller.setSketchTool(SketchTool::Line);
    const auto& [circleId, circle] = *p.sketch().circles().begin();
    const Vec2 center = p.sketch().point(circle.center)->position;
    const Vec2 rim = center + Vec2{circle.radius, 0};
    p.tap(p.sketchScreen(rim) + Vec2{100, 80});
    p.tap(p.sketchScreen(rim) + Vec2{3, 0});
    EXPECT_EQ(p.constraintsOfKind(K::PointOnCircle), 1);

    // Dragging the first line moves what is held on it.
    const Vec2 before = p.sketch().point(line.start)->position;
    p.controller.setSketchTool(SketchTool::Select);
    p.drag(p.sketchScreen(a + (b - a) * 0.25), p.sketchScreen(a + (b - a) * 0.25) + Vec2{60, 0});
    EXPECT_GT((p.sketch().point(line.start)->position - before).length(), 1.0);
    const sketch::SolveReport report = p.sketch().solveReport();
    EXPECT_TRUE(report.ok);
    for (const auto& [id, c] : p.sketch().constraints())
        if (c.kind == K::PointOnLine) {
            const Vec2 q = p.sketch().point(c.a)->position;
            const auto* l = p.sketch().line(c.b);
            const Vec2 s = p.sketch().point(l->start)->position, e = p.sketch().point(l->end)->position;
            const Vec2 d = e - s;
            EXPECT_NEAR(std::abs(d.x * (q.y - s.y) - d.y * (q.x - s.x)) / d.length(), 0.0, 1e-6) << "still on the line";
        }
}

// Dragging a point onto a face corner puts it there exactly, joined to nothing.
TEST(SketchPhone, ADraggedPointSnapsToAFaceCorner)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.tap(p.sketchScreen({-5, -5}));
    p.tap(p.sketchScreen({5, 5}));
    ASSERT_EQ(p.sketch().lines().size(), 4u);
    p.controller.setSketchTool(SketchTool::Select);
    const Vec2 from = p.sketchScreen({5, 5});
    p.controller.pointerPress(p.at(from));
    const Vec2 to = p.sketchScreen({10, 10}) + Vec2{-7, 8};
    for (int i = 1; i <= 10; ++i)
        p.controller.pointerMove(p.at(from + (to - from) * (i / 10.0)));
    EXPECT_EQ(p.hint(), "Corner");
    const SketchSession::SnapInfo info = p.session().snapInfo(p.controller.camera());
    EXPECT_TRUE(info.active);
    EXPECT_EQ(info.hint, "Corner");
    p.controller.pointerRelease(p.at(to));
    EXPECT_TRUE(p.hasPoint({10, 10}));
    EXPECT_EQ(p.stack.undoLabel(), "Move point");
    EXPECT_EQ(p.constraintsOfKind(K::Coincident), 0);
}

/// The reference geometry follows the face: the box made taller and wider in
// its history step, the sketch on its top face (edited again) sees the face
// where it is now. It is recomputed from the body, never stored.
TEST(SketchPhone, ReferencesFollowTheFace)
{
    Phone p(false);
    p.box();
    p.sketchOnTop();
    p.tap(p.sketchScreen({-5, -5}));
    p.tap(p.sketchScreen({5, 5}));
    p.finish();
    const Uuid boxStep = p.document.bodies().front()->features()[0]->id();
    ASSERT_TRUE(p.controller.setFeatureParameter(boxStep, "height", "25").ok());
    ASSERT_TRUE(p.controller.setFeatureParameter(boxStep, "width", "30").ok());
    EXPECT_NEAR(p.volume(), 30 * 20 * 25, 1e-6);
    ASSERT_EQ(p.document.sketches().size(), 1u);
    ASSERT_TRUE(p.controller.editSketch(p.document.sketches().front()->id()).ok());
    p.controller.skipAnimation();
    EXPECT_NEAR(p.sketch().plane().origin.z, 25.0, 1e-9);
    const auto& refs = p.session().references();
    EXPECT_EQ(refs.curves.size(), 4u);
    for (const Vec2 corner : {Vec2{-10, -10}, Vec2{20, -10}, Vec2{20, 10}, Vec2{-10, 10}})
        EXPECT_TRUE(std::any_of(refs.points.begin(), refs.points.end(), [&](const auto& r) {
            return r.kind == SketchReferences::PointKind::Corner && (r.position - corner).length() < 1e-9;
        })) << corner.x << ", " << corner.y;
    // Undo (in the sketch) takes the change back: the references follow.
    p.controller.finishSketch();
    ASSERT_TRUE(p.controller.undo());
    ASSERT_TRUE(p.controller.editSketch(p.document.sketches().front()->id()).ok());
    EXPECT_TRUE(std::any_of(p.session().references().points.begin(), p.session().references().points.end(),
                            [](const auto& r) { return (r.position - Vec2{10, 10}).length() < 1e-9; }));
}

// Guides: a point placed level with another point lines up with it exactly
// (the grid off, so nothing else rounds it).
TEST(SketchPhone, GuidesLineUpWithOtherPoints)
{
    Phone p(false);
    p.controller.setSketchGridSnap(false);
    ASSERT_TRUE(p.controller.startSketch().ok());
    p.controller.skipAnimation();
    p.tap(p.sketchScreen({0, 0}) + Vec2{40, -30});
    p.tap(p.sketchScreen({0, 0}) + Vec2{237, -113});
    ASSERT_EQ(p.sketch().lines().size(), 4u);
    double top = -1e9, right = -1e9;
    for (const Vec2 q : p.points()) {
        top = std::max(top, q.y);
        right = std::max(right, q.x);
    }
    // A line whose first point is 2 px off the rectangle's top, well to the right.
    p.controller.setSketchTool(SketchTool::Line);
    const Vec2 level = p.sketchScreen({right, top}) + Vec2{90, 2};
    p.controller.pointerMove(p.at(level, PointerButton::None));
    EXPECT_EQ(p.hint(), "Horizontal");
    p.tap(level);
    ASSERT_TRUE(p.session().isDrawing());
    // Its end straight below the rectangle's right side, 2 px off.
    const Vec2 plumb = p.sketchScreen({right, top}) + Vec2{-2, 140};
    p.tap(plumb);
    p.controller.keyPress(Key::Escape);
    ASSERT_EQ(p.sketch().lines().size(), 5u);
    bool levelled = false, plumbed = false;
    for (const Vec2 q : p.points()) {
        levelled = levelled || (std::abs(q.y - top) < 1e-9 && q.x > right + 1);
        plumbed = plumbed || (std::abs(q.x - right) < 1e-9 && q.y < top - 1);
    }
    EXPECT_TRUE(levelled);
    EXPECT_TRUE(plumbed);
}

// A finger's reach in a sketch: a 44 px target; snapping a little further.
TEST(SketchPhone, FingerReach)
{
    EXPECT_EQ(InputProfile::forDevice(PointerDevice::Touch).pickTolerance, 20.0);
    EXPECT_EQ(SketchSession::pickReach(PointerDevice::Touch), 22.0);
    EXPECT_GE(SketchSession::snapReach(PointerDevice::Touch), 22.0);
}

// One finger dragging empty space in a sketch pans (Select and Trim); the
// view never tilts off the sketch's plane. A mouse still orbits.
TEST(SketchPhone, OneFingerOnEmptySpacePansWithoutTilting)
{
    for (const SketchTool tool : {SketchTool::Select, SketchTool::Trim}) {
        Phone p;
        p.box();
        p.sketchOnTop();
        p.controller.setSketchTool(tool);
        const Camera before = p.controller.camera();
        // From beside the face (empty space) 120 px across and down.
        const Vec2 from = p.sketchScreen({-14, 0});
        p.drag(from, from + Vec2{120, 80});
        const Camera after = p.controller.camera();
        EXPECT_NEAR(after.pitch, before.pitch, 1e-12);
        EXPECT_NEAR(after.yaw, before.yaw, 1e-12);
        EXPECT_GT((after.target - before.target).length(), 1.0) << "it panned";
        // The point under the finger moved with it.
        EXPECT_NEAR((p.sketchScreen({-14, 0}) - (from + Vec2{120, 80})).length(), 0.0, 1.0);
        EXPECT_TRUE(p.sketch().lines().empty());
    }
    // A mouse orbits as before.
    Phone m(false);
    m.box();
    m.sketchOnTop();
    m.controller.setSketchTool(SketchTool::Select);
    const Camera mouseBefore = m.controller.camera();
    const Vec2 from = m.sketchScreen({-16, 0});
    m.drag(from, from + Vec2{120, 80});
    EXPECT_GT(std::abs(m.controller.camera().yaw - mouseBefore.yaw) + std::abs(m.controller.camera().pitch - mouseBefore.pitch), 1e-3);
}

// "Face on" turns the view back to facing the sketch after an orbit, and
// frames the face.
TEST(SketchPhone, FaceOnFacesTheSketchAgain)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    const Camera faced = p.controller.camera();
    EXPECT_NEAR(faced.forward().z, -1.0, 1e-9);
    p.controller.setViewAngles(faced.yaw + 0.5, faced.pitch - 0.4, false);
    EXPECT_GT(std::abs(p.controller.camera().forward().z + 1.0), 1e-3);
    p.controller.faceSketch();
    p.controller.skipAnimation();
    const Camera again = p.controller.camera();
    EXPECT_NEAR(again.forward().z, -1.0, 1e-9);
    EXPECT_NEAR(again.yaw, faced.yaw, 1e-9);
    // The face spans most of the phone's width again (1.15 margin).
    const double across = (p.sketchScreen({10, 0}) - p.sketchScreen({-10, 0})).length();
    EXPECT_GT(across, 0.6 * 402);
    EXPECT_LT(across, 402);
}

// A sketch opened again on a face is framed on the face (not left small).
TEST(SketchPhone, EditingAFaceSketchFramesTheFace)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.tap(p.sketchScreen({-5, -5}));
    p.tap(p.sketchScreen({5, 5}));
    p.finish();
    p.controller.fitAll(false);
    ASSERT_TRUE(p.controller.editSketch(p.document.sketches().front()->id()).ok());
    p.controller.skipAnimation();
    EXPECT_EQ(p.session().tool(), SketchTool::Select) << "an existing sketch opens in Select";
    const double across = (p.sketchScreen({10, 0}) - p.sketchScreen({-10, 0})).length();
    EXPECT_GT(across, 0.6 * 402);
}

// Live values: no "0" values (a finger's first tap): none while the finger
// is down, places to tap without a number once it lifts (the keypad types
// them); W and H on a rectangle's; no snapped cursor left where a finger lifted.
TEST(SketchPhone, LiveValuesAndTheCursorOnTouch)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    auto inputs = [&] {
        std::vector<SketchLabel> out;
        for (const auto& l : p.session().labels(p.controller.camera()))
            if (l.kind == SketchLabel::Kind::Input)
                out.push_back(l);
        return out;
    };
    auto previewPoints = [&] {
        int n = 0;
        for (const auto& pt : p.session().renderData(p.controller.camera()).points)
            n += pt.style == SketchStyle::Preview || pt.style == SketchStyle::Hovered;
        return n;
    };
    const Vec2 corner = p.sketchScreen({-3, -3});
    p.controller.pointerPress(p.at(corner));
    ASSERT_TRUE(p.session().isDrawing());
    EXPECT_TRUE(inputs().empty()) << "no 0 x 0 values under the finger";
    p.controller.pointerRelease(p.at(corner));
    ASSERT_TRUE(p.session().isDrawing());
    const auto waiting = inputs();
    ASSERT_EQ(waiting.size(), 2u) << "after the first tap: the width and height, to tap and type";
    EXPECT_EQ(waiting[0].key, "width");
    EXPECT_EQ(waiting[1].key, "height");
    EXPECT_TRUE(waiting[0].text.empty() && waiting[1].text.empty()) << "no numbers: nothing is measured yet";
    EXPECT_EQ(previewPoints(), 1) << "the first corner only: no cursor where the finger lifted";
    // The finger down again and moving: W and H.
    const Vec2 from = p.sketchScreen({4, 5});
    p.controller.pointerPress(p.at(from));
    p.controller.pointerMove(p.at(from + Vec2{3, 2}));
    const auto live = inputs();
    ASSERT_EQ(live.size(), 2u);
    EXPECT_EQ(live[0].prefix, "W");
    EXPECT_EQ(live[1].prefix, "H");
    p.controller.pointerRelease(p.at(from + Vec2{3, 2}));
    EXPECT_FALSE(p.session().isDrawing());
    EXPECT_EQ(previewPoints(), 0);
}

// The keypad after a finger's first tap: a live value tapped and typed shows
// its number, and the rectangle it sizes is previewed though no finger is down.
TEST(SketchPhone, KeypadTypesALiveValueAfterTheFirstTap)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.controller.setSketchTool(SketchTool::Rectangle);
    auto bandLines = [&] {
        int n = 0;
        for (const auto& line : p.session().renderData(p.controller.camera()).lines)
            n += line.style == SketchStyle::Preview;
        return n;
    };
    p.tap(p.sketchScreen({-3, -3}));
    ASSERT_TRUE(p.session().isDrawing());
    EXPECT_EQ(bandLines(), 0) << "nothing typed: no rectangle yet";
    ASSERT_TRUE(p.session().focusInput("width"));
    EXPECT_EQ(p.session().typeIntoInput("12"), "");
    ASSERT_TRUE(p.session().focusInput("height"));
    EXPECT_EQ(p.session().typeIntoInput("8"), "");
    std::vector<std::string> shown;
    for (const auto& l : p.session().labels(p.controller.camera()))
        if (l.kind == SketchLabel::Kind::Input)
            shown.push_back(l.text);
    EXPECT_EQ(shown, (std::vector<std::string>{"12", "8"}));
    EXPECT_EQ(bandLines(), 4) << "the 12 x 8 rectangle typed is previewed";
}

// Plain words on touch: the status, the zero-size message, and "taps add".
TEST(SketchPhone, PlainWordsOnTouch)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    EXPECT_EQ(p.session().statusText(), "Empty sketch");
    // Two taps straight above each other: no width.
    p.tap(p.sketchScreen({-3, -3}));
    p.tap(p.sketchScreen({-3, 4}));
    ASSERT_FALSE(p.messages.empty());
    EXPECT_EQ(p.messages.back(), "Tap farther from the first corner.");
    p.controller.keyPress(Key::Escape);
    p.tap(p.sketchScreen({-3, -3}));
    p.tap(p.sketchScreen({4, 4}));
    EXPECT_EQ(p.session().statusText(), "Not fully sized");
    // A mouse layout keeps the count.
    Phone m(false);
    ASSERT_TRUE(m.controller.startSketch().ok());
    m.controller.skipAnimation();
    m.tap(m.sketchScreen({0, 0}) + Vec2{60, -40});
    m.tap(m.sketchScreen({0, 0}) + Vec2{160, -120});
    EXPECT_EQ(m.session().statusText(), "4 degrees of freedom");

    // Touch: a second tap adds to the selection: said once.
    p.controller.setSketchTool(SketchTool::Select);
    p.messages.clear();
    p.tap(p.sketchScreen({0.5, -3}));
    ASSERT_EQ(p.session().selection().size(), 1u);
    EXPECT_NE(p.session().hintText().find("click empty space to start a new selection"), std::string::npos);
    p.tap(p.sketchScreen({0.5, 4}));
    ASSERT_EQ(p.session().selection().size(), 2u);
    ASSERT_EQ(p.messages.size(), 1u);
    EXPECT_EQ(p.messages.back(), "Taps add to the selection: tap empty space to start a new selection.");
    p.tap(p.sketchScreen({-3, 0.5}));
    EXPECT_EQ(p.messages.size(), 1u) << "once";
    // Delete comes first in the actions.
    ASSERT_FALSE(p.session().contextActions().empty());
    EXPECT_EQ(p.session().contextActions().front().id, "delete");
}

// The loupe: placed beside the finger, whole inside the safe area, clear of
// the finger and of the controls.
TEST(SketchPhone, LoupePlacement)
{
    LoupeInput in;
    in.area = {0, 62, 402, 840};
    in.avoid = {{16, 62, 300, 110}, {320, 62, 392, 250}, {16, 700, 392, 840}}; // top bar, right column, bottom stack
    // In the middle of the view: straight above the finger.
    in.finger = {200, 400};
    LoupePlacement placed = placeLoupe(in);
    EXPECT_TRUE(placed.clear);
    EXPECT_NEAR(placed.center.x, 200, 1e-9);
    EXPECT_NEAR(placed.center.y, 320, 1e-9);
    // Just below the top bar: no room above, so beside it, never over the finger.
    for (const Vec2 finger : {Vec2{150, 140}, Vec2{30, 150}, Vec2{380, 300}, Vec2{200, 680}, Vec2{10, 690}}) {
        in.finger = finger;
        placed = placeLoupe(in);
        const ScreenRect box{placed.center.x - in.size / 2, placed.center.y - in.size / 2, placed.center.x + in.size / 2,
                             placed.center.y + in.size / 2};
        EXPECT_TRUE(in.area.contains(box)) << finger.x << ", " << finger.y;
        EXPECT_FALSE(box.inflated(-0.5).intersects(ScreenRect::around(finger).inflated(kLoupeFingerClearance)))
            << finger.x << ", " << finger.y;
        if (placed.clear) {
            for (const auto& o : in.avoid)
                EXPECT_FALSE(box.intersects(o)) << finger.x << ", " << finger.y;
        }
    }
    in.finger = {150, 140};
    EXPECT_TRUE(placeLoupe(in).clear) << "a clear spot beside the finger";
}

// The loupe shows while a finger places or drags a point, not with a mouse.
TEST(SketchPhone, TheLoupeFollowsAFingerPlacingAPoint)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    EXPECT_FALSE(p.controller.sketchLoupe().visible);
    const Vec2 corner = p.sketchScreen({10, 10});
    p.controller.pointerPress(p.at(corner + Vec2{-6, 7}));
    InteractionController::SketchLoupe loupe = p.controller.sketchLoupe();
    ASSERT_TRUE(loupe.visible);
    EXPECT_EQ(loupe.hint, "Corner");
    EXPECT_NEAR((loupe.subject - corner).length(), 0.0, 1e-6) << "it magnifies the snapped corner";
    EXPECT_GT((loupe.center - (corner + Vec2{-6, 7})).length(), loupe.size / 2) << "not over the finger";
    p.controller.pointerRelease(p.at(corner + Vec2{-6, 7}));
    EXPECT_FALSE(p.controller.sketchLoupe().visible) << "gone when the finger lifts";

    // Live values keep off the controls: with the controls given, a value
    // moved out from under the finger never lands on one.
    p.controller.setSketchObstacles({{0, 62, 402, 200}});
    const Vec2 from = p.sketchScreen({-8, -8});
    p.controller.pointerPress(p.at(from));
    p.controller.pointerMove(p.at(from + Vec2{0, -150}));
    for (const auto& l : p.session().labels(p.controller.camera()))
        if (l.kind == SketchLabel::Kind::Input || l.kind == SketchLabel::Kind::Hint) {
            const ScreenRect box = ScreenRect::at(l.screen - SketchSession::kLiveLabelSize * 0.5, SketchSession::kLiveLabelSize);
            EXPECT_FALSE(box.intersects({0, 62, 402, 200})) << l.text;
        }
    p.controller.pointerRelease(p.at(from + Vec2{0, -150}));

    // A mouse: no loupe.
    Phone m(false);
    m.box();
    m.sketchOnTop();
    m.controller.pointerPress(m.at(m.sketchScreen({10, 10})));
    EXPECT_FALSE(m.controller.sketchLoupe().visible);
    m.controller.pointerRelease(m.at(m.sketchScreen({10, 10})));
}

// A mouse hover while a finger drags (Qt re-sends a stationary mouse's
// position each frame the scene changes under it) never moves the drag.
TEST(SketchPhone, AMouseHoverNeverMovesAFingersDrag)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.tap(p.sketchScreen({-5, -5}));
    p.tap(p.sketchScreen({5, 5}));
    p.controller.setSketchTool(SketchTool::Select);
    p.controller.pointerPress(p.at(p.sketchScreen({5, 5})));
    p.controller.pointerMove(p.at(p.sketchScreen({6, 6})));
    PointerEvent hover;
    hover.device = PointerDevice::Mouse;
    hover.button = PointerButton::None;
    hover.position = p.sketchScreen({-8, 8});
    p.controller.pointerMove(hover);
    EXPECT_TRUE(p.controller.sketchLoupe().visible) << "still the finger's loupe";
    p.controller.pointerMove(p.at(p.sketchScreen({7, 7})));
    p.controller.pointerRelease(p.at(p.sketchScreen({7, 7})));
    EXPECT_TRUE(p.hasPoint({7, 7})) << "the finger's drop, not the mouse's position";
}
