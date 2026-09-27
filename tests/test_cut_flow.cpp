// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrude and cut on a phone (the owner, 2026-09-27: "select a face, sketch
// the shape to cut out, select it, extrude it into the body and it should
// subtract"), driven through the interaction layer with TOUCH input at the
// iPhone 16 Pro size (402 x 874, safe area 62/0/34/0) exactly as the phone's
// viewport forwards taps and one-finger drags, and with a mouse on a desktop
// window. Every result is an exact volume of a 20 mm cube.
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/Manipulator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

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

    explicit Phone(Vec2 size = {402, 874}, bool touch = true)
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize(size);
        controller.setTouchLayout(touch);
        if (touch) {
            controller.setSafeInsets({62, 0, 34, 0});
            // What Main.qml reports in the compact layout while sketching:
            // the top bar and the Finish bar, the hint and the tool strip.
            controller.setFrameInsets({126, 0, 128, 0});
        }
        device = touch ? PointerDevice::Touch : PointerDevice::Mouse;
    }

    PointerEvent at(Vec2 p) const
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        e.device = device;
        return e;
    }
    // A tap as the phone's viewport forwards it (press and release, no hover);
    // a mouse moves there first.
    void tap(Vec2 p)
    {
        if (device == PointerDevice::Mouse) {
            PointerEvent hover = at(p);
            hover.button = PointerButton::None;
            controller.pointerMove(hover);
        }
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
    double volume(std::size_t i = 0) const { return geom::volume(document.bodies()[i]->shape()); }
    const ExtrudeOperation* extrude() const { return dynamic_cast<const ExtrudeOperation*>(controller.operation()); }
    std::vector<std::string> actionIds() const
    {
        std::vector<std::string> ids;
        for (const auto& a : controller.contextActions())
            ids.push_back(a.id);
        return ids;
    }
    bool offers(const std::string& id) const
    {
        const auto ids = actionIds();
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    void box()
    {
        ASSERT_TRUE(controller.createBox(20).ok());
        controller.fitAll(false);
    }
    // Taps the top face in its middle and starts a sketch there with the
    // face's Sketch action (the value box's button on a phone).
    void sketchOnTop()
    {
        tap(screen({0, 0, 20}));
        ASSERT_TRUE(controller.triggerAction("sketch").ok());
        controller.skipAnimation();
        ASSERT_EQ(controller.mode(), InteractionController::Mode::Sketch);
    }
    // The rectangle drawn by one drag from local a to b: its corners.
    Vec2 rectMin{1e9, 1e9}, rectMax{-1e9, -1e9};
    double rectArea() const { return (rectMax.x - rectMin.x) * (rectMax.y - rectMin.y); }
    void rectangle(Vec2 a, Vec2 b)
    {
        controller.setSketchTool(SketchTool::Rectangle);
        const std::size_t before = session().sketch().lines().size();
        drag(sketchScreen(a), sketchScreen(b));
        const auto& s = session().sketch();
        rectMin = {1e9, 1e9};
        rectMax = {-1e9, -1e9};
        std::size_t k = 0;
        for (const auto& [id, l] : s.lines()) {
            if (k++ < before)
                continue;
            for (auto e : {l.start, l.end}) {
                const Vec2 q = s.point(e)->position;
                rectMin = {std::min(rectMin.x, q.x), std::min(rectMin.y, q.y)};
                rectMax = {std::max(rectMax.x, q.x), std::max(rectMax.y, q.y)};
            }
        }
    }
    void finish()
    {
        controller.finishSketch();
        controller.skipAnimation();
    }
    // Presses on the active arrow's shaft and moves the finger so that the
    // point grabbed travels `delta` mm along the arrow (negative: against
    // it). Returns the value at release.
    double dragArrow(double delta, int steps = 10)
    {
        const auto* op = controller.operation();
        const int i = op->activeHandle();
        const LinearManipulator h = op->handle(i);
        const Vec3 anchor = h.anchor(op->handleOffset(i));
        const ArrowStyle style;
        const double px = controller.camera().pixelSize(anchor);
        const Vec3 grabWorld = anchor + h.direction() * ((style.gapPx + style.shaftPx * 0.6) * px);
        const Vec2 grab = screen(grabWorld);
        controller.pointerPress(at(grab));
        EXPECT_TRUE(controller.manipulatorDragging()) << "the press did not grab the arrow";
        Vec2 last = grab;
        for (int s = 1; s <= steps; ++s) {
            last = screen(grabWorld + h.direction() * (delta * s / steps));
            controller.pointerMove(at(last));
        }
        controller.pointerRelease(at(last));
        return controller.operation() ? controller.operation()->value() : 0.0;
    }
    Status commit() { return controller.commitOperation(); }
    // Looks up from below and taps a profile of a sketch on the ground
    // plane at `local` (it lies on the box's bottom face: it wins there).
    void selectFromBelow(Vec2 local)
    {
        controller.setStandardView(StandardView::Bottom, false);
        controller.fitAll(false);
        tap(screen({local.x, local.y, 0}));
        ASSERT_EQ(controller.selection().size(), 1u);
        ASSERT_EQ(controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
        ASSERT_NE(extrude(), nullptr);
    }
    // What a tap at `p` selects first: P(rofile), E(dge), F(ace) or -.
    char tapped(Vec2 p)
    {
        tap(screen({60, 60, 60})); // empty space: nothing selected
        EXPECT_TRUE(controller.selection().empty());
        tap(p);
        const auto& items = controller.selection().items();
        return items.empty()                                        ? '-'
             : items[0].kind == sel::SelectionKind::SketchProfile ? 'P'
             : items[0].kind == sel::SelectionKind::Edge          ? 'E'
             : items[0].kind == sel::SelectionKind::Face          ? 'F'
                                                                  : '?';
    }
};

} // namespace

// 1. Starting a sketch on a face frames it: on a phone the fitted view has
// 1 mm = 6 px (the grid and the arrow snapped in 2 mm steps); framed in the
// room between the top and bottom controls, 1 mm steps work.
TEST(CutFlow, SketchOnAFaceFramesItOnAPhone)
{
    Phone p;
    p.box();
    const double fitted = 1 / p.controller.camera().pixelSize({0, 0, 20});
    EXPECT_LT(fitted, 8.0) << "px per mm at the fitted phone view";
    p.sketchOnTop();
    const double framed = 1 / p.controller.camera().pixelSize({0, 0, 20});
    EXPECT_GT(framed, 14.0) << "px per mm on the framed face";
    EXPECT_EQ(snapIncrement(1 / framed, 10.0), 1.0) << "the sketch grid snaps in 1 mm";
    // The face is inside the free room, across most of the width.
    const Vec2 a = p.screen({-10, -10, 20}), b = p.screen({10, 10, 20});
    const double left = std::min(a.x, b.x), right = std::max(a.x, b.x);
    const double top = std::min(a.y, b.y), bottom = std::max(a.y, b.y);
    EXPECT_GT(left, 0);
    EXPECT_LT(right, 402);
    EXPECT_GT(right - left, 0.8 * 402);
    EXPECT_GT(top, 126);
    EXPECT_LT(bottom, 874 - 128);
    EXPECT_NEAR((top + bottom) / 2, 126 + (874 - 126 - 128) / 2.0, 2.0) << "centered in the free room";

    // A one-finger drag from (-5,-5) to (5,5) makes exactly that rectangle.
    p.rectangle({-5, -5}, {5, 5});
    EXPECT_NEAR(p.rectMin.x, -5, 1e-9);
    EXPECT_NEAR(p.rectMin.y, -5, 1e-9);
    EXPECT_NEAR(p.rectMax.x, 5, 1e-9);
    EXPECT_NEAR(p.rectMax.y, 5, 1e-9);
    p.finish();
    // The rectangle tapped, its arrow dragged 5 mm into the body: a 5 mm cut.
    p.tap(p.screen({0, 0, 20}));
    ASSERT_NE(p.extrude(), nullptr);
    EXPECT_DOUBLE_EQ(p.dragArrow(-5.0), -5.0);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Cut);
    ASSERT_TRUE(p.commit().ok());
    ASSERT_EQ(p.document.bodies().size(), 1u);
    EXPECT_NEAR(p.volume(), kCube - 100 * 5, 1e-6);
}

// The same flow with a mouse on a desktop window.
TEST(CutFlow, SketchOnAFaceFramesItOnADesktop)
{
    Phone p({1400, 900}, false);
    p.box();
    p.sketchOnTop();
    const double framed = 1 / p.controller.camera().pixelSize({0, 0, 20});
    EXPECT_GT(framed, 14.0);
    const Vec2 a = p.screen({-10, -10, 20}), b = p.screen({10, 10, 20});
    EXPECT_GT(std::min(a.y, b.y), 0);
    EXPECT_LT(std::max(a.y, b.y), 900);
    p.rectangle({-5, -5}, {5, 5});
    EXPECT_NEAR(p.rectArea(), 100, 1e-9);
    p.finish();
    p.tap(p.screen({0, 0, 20}));
    EXPECT_DOUBLE_EQ(p.dragArrow(-5.0), -5.0);
    ASSERT_TRUE(p.commit().ok());
    EXPECT_NEAR(p.volume(), kCube - 500, 1e-6);
}

// A construction plane is framed like a face (its drawn outline).
TEST(CutFlow, SketchOnAConstructionPlaneFramesIt)
{
    Phone p;
    p.box();
    ASSERT_TRUE(p.controller.runTool("plane").ok());
    ASSERT_TRUE(p.controller.triggerAction("datum:origin:0").ok()); // from XY
    EXPECT_EQ(p.controller.setValueText("30"), "");
    ASSERT_TRUE(p.controller.commitOperation().ok());
    p.controller.cancelOperation();
    ASSERT_EQ(p.document.datums().size(), 1u);
    const doc::Datum& plane = *p.document.datums().front();
    ASSERT_TRUE(p.controller.selectDatum(plane.id()).ok());
    ASSERT_TRUE(p.controller.startSketch().ok());
    p.controller.skipAnimation();
    ASSERT_EQ(p.controller.mode(), InteractionController::Mode::Sketch);
    double left = 1e9, right = -1e9;
    for (const Vec3& corner : p.controller.datumShape(plane.geometry(), plane.kind()).corners) {
        const Vec2 s = p.screen(corner);
        left = std::min(left, s.x);
        right = std::max(right, s.x);
    }
    EXPECT_GT(left, -1.0);
    EXPECT_LT(right, 403.0);
    EXPECT_GT(right - left, 0.8 * 402) << "the plane's outline spans most of the phone's width";
}

// 2. Box edges no longer take taps meant for a sketch region: the
// investigation's row of taps across a rectangle over the top face's x = 10
// edge, at the fitted phone view (1 mm = 6 px; a finger reaches 18 px).
TEST(CutFlow, EdgeTakesTapsInsideARegionOnlyWithinAMouseReach)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({4, -4}, {14, 4});
    p.finish();
    p.controller.fitAll(false);
    const double px = 1 / p.controller.camera().pixelSize({10, 0, 20});
    ASSERT_LT(px, 8.0);
    std::string row;
    int edges = 0;
    for (double x = 4.5; x < 14; x += 1.0) {
        const Vec2 at = p.screen({x, 0, 20});
        const char k = p.tapped(at);
        row += k;
        // How far the x = 10 edge is on screen.
        const Vec2 e0 = p.screen({10, -10, 20}), e1 = p.screen({10, 10, 20});
        const Vec2 d = e1 - e0;
        const double t = std::clamp(((at - e0).x * d.x + (at - e0).y * d.y) / (d.x * d.x + d.y * d.y), 0.0, 1.0);
        const double pixels = (at - (e0 + d * t)).length();
        if (pixels > 6.5) {
            EXPECT_EQ(k, 'P') << "x = " << x << " is " << pixels << " px from the edge";
        }
        edges += k == 'E';
    }
    EXPECT_LE(edges, 2) << row;
    // Outside the region the edge keeps a finger's reach: 2 mm (12 px) off it on the face.
    EXPECT_EQ(p.tapped(p.screen({10, 7, 20}) + (p.screen({8, 7, 20}) - p.screen({10, 7, 20})) * 0.9), 'E');
}

// After the cut, a tap on the pocket's floor selects the floor, not the
// pocket's edges a finger's reach (18 px) around it: a 5 mm slot seen from
// above at the fitted phone view (1 mm = 6 px), its edges 15 px from its middle.
TEST(CutFlow, PocketFloorTapSelectsTheFloor)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({-3, -6}, {2, 5}); // drawn at 1 mm steps, moved below
    p.finish();
    p.tap(p.screen({0, 0, 20}));
    EXPECT_EQ(p.controller.setValueText("-5"), "");
    ASSERT_TRUE(p.commit().ok());
    EXPECT_NEAR(p.volume(), kCube - p.rectArea() * 5, 1e-6);
    p.controller.setStandardView(StandardView::Top, false);
    p.controller.fitAll(false);
    const Vec2 middle = p.screen({(p.rectMin.x + p.rectMax.x) / 2, (p.rectMin.y + p.rectMax.y) / 2, 15});
    const double px = 1 / p.controller.camera().pixelSize({0, 0, 15});
    ASSERT_LT(px * (p.rectMax.x - p.rectMin.x) / 2, InputProfile::forDevice(PointerDevice::Touch).pickTolerance)
        << "the slot's sides are within a finger's reach of its middle";
    EXPECT_EQ(p.tapped(middle), 'F');
    ASSERT_FALSE(p.controller.selection().empty());
    const auto info = geom::faceInfo(p.document.bodies()[0]->shape(), p.controller.selection().items().front().index);
    ASSERT_TRUE(info);
    EXPECT_NEAR(info->planeOrigin.z, 15, 1e-9) << "the floor";
    // Right at a side (within a mouse's reach) the edge is still taken.
    EXPECT_EQ(p.tapped(p.screen({p.rectMax.x, 0, 20}) - Vec2{2, 0}), 'E');
}

// 3. Cut with a positive value used to be a dead end. For a sketch on a face
// Cut means into the body, Join and New body out of it; Flip turns it round.
TEST(CutFlow, CutGoesIntoTheBodyJoinOutOfIt)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({-5, -5}, {5, 5});
    p.finish();
    p.tap(p.screen({0, 0, 20}));
    EXPECT_EQ(p.controller.setValueText("5"), "");
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Join);
    EXPECT_EQ(p.controller.operation()->valueLabel(), "Height");
    EXPECT_TRUE(p.offers("flip"));
    ASSERT_TRUE(p.controller.triggerAction("mode:cut").ok());
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Cut);
    EXPECT_DOUBLE_EQ(p.extrude()->distance(), -5.0);
    EXPECT_EQ(p.controller.operation()->valueLabel(), "Cut depth");
    EXPECT_TRUE(p.controller.operation()->error().empty()) << p.controller.operation()->error();
    EXPECT_TRUE(p.controller.operation()->canCommit());
    // With Cut chosen the arrow points into the body: dragging it the way
    // it points deepens the cut.
    EXPECT_LT(p.controller.operation()->handle(0).direction().z, -0.99);
    EXPECT_DOUBLE_EQ(p.dragArrow(2.0), -7.0);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Cut);
    // Join turns it back out.
    ASSERT_TRUE(p.controller.triggerAction("mode:join").ok());
    EXPECT_DOUBLE_EQ(p.extrude()->distance(), 7.0);
    EXPECT_EQ(p.controller.operation()->valueLabel(), "Height");
    ASSERT_TRUE(p.controller.triggerAction("mode:cut").ok());
    EXPECT_EQ(p.controller.setValueText("5"), ""); // a depth typed while cutting: into the body
    EXPECT_DOUBLE_EQ(p.extrude()->distance(), -5.0);
    ASSERT_TRUE(p.commit().ok());
    EXPECT_NEAR(p.volume(), kCube - 500, 1e-6);
}

TEST(CutFlow, FlipTurnsTheExtrusionRound)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({-5, -5}, {5, 5});
    p.finish();
    p.tap(p.screen({0, 0, 20}));
    EXPECT_FALSE(p.offers("flip")) << "nothing to flip at 0";
    EXPECT_EQ(p.controller.setValueText("5"), "");
    ASSERT_TRUE(p.controller.triggerAction("flip").ok());
    EXPECT_DOUBLE_EQ(p.extrude()->distance(), -5.0);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Cut);
    ASSERT_TRUE(p.controller.triggerAction("flip").ok());
    EXPECT_DOUBLE_EQ(p.extrude()->distance(), 5.0);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Join);
    ASSERT_TRUE(p.commit().ok());
    EXPECT_NEAR(p.volume(), kCube + 500, 1e-6);
}

TEST(CutFlow, NewBodyAndJoinTurnAnInwardValueOutward)
{
    for (const char* mode : {"mode:new", "mode:join"}) {
        Phone p;
        p.box();
        p.sketchOnTop();
        p.rectangle({-5, -5}, {5, 5});
        p.finish();
        p.tap(p.screen({0, 0, 20}));
        EXPECT_EQ(p.controller.setValueText("-5"), "");
        ASSERT_TRUE(p.controller.triggerAction(mode).ok());
        EXPECT_DOUBLE_EQ(p.extrude()->distance(), 5.0) << mode;
        ASSERT_TRUE(p.commit().ok()) << mode;
        if (std::string(mode) == "mode:new") {
            ASSERT_EQ(p.document.bodies().size(), 2u);
            EXPECT_NEAR(p.volume(1), 500, 1e-6);
            EXPECT_EQ(p.extrude(), nullptr);
        } else {
            ASSERT_EQ(p.document.bodies().size(), 1u);
            EXPECT_NEAR(p.volume(), kCube + 500, 1e-6);
        }
    }
}

// Join chosen and then pushed in used to commit a step that changed nothing.
TEST(CutFlow, JoinPushedInIsRefusedWithAReason)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({-4, -4}, {4, 4});
    p.finish();
    p.tap(p.screen({0, 0, 20}));
    ASSERT_TRUE(p.controller.triggerAction("mode:join").ok());
    p.dragArrow(-6.0, 6);
    ASSERT_NE(p.extrude(), nullptr);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Join);
    EXPECT_LT(p.extrude()->distance(), 0);
    EXPECT_EQ(p.controller.operation()->error(),
              "This extrusion stays inside the body, so nothing would be added. Pull it outward, or choose Cut.");
    EXPECT_FALSE(p.controller.operation()->canCommit());
    const std::size_t steps = p.stack.size();
    EXPECT_FALSE(p.commit().ok());
    EXPECT_EQ(p.stack.size(), steps);
    EXPECT_NEAR(p.volume(), kCube, 1e-6);
}

// 4. The value says what it makes; the compact row starts with the modes.
TEST(CutFlow, ValueLabelAndActionOrderFollowTheMode)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({-5, -5}, {5, 5});
    p.finish();
    p.tap(p.screen({0, 0, 20}));
    auto ids = p.actionIds();
    ASSERT_GE(ids.size(), 5u);
    EXPECT_EQ(ids[0], "mode:new");
    EXPECT_EQ(ids[1], "mode:join");
    EXPECT_EQ(ids[2], "mode:cut");
    EXPECT_EQ(ids.back(), "editSketch");
    EXPECT_EQ(ids[ids.size() - 2], "revolve");
    EXPECT_EQ(p.controller.setValueText("-3"), "");
    EXPECT_EQ(p.controller.operation()->valueLabel(), "Cut depth");
    ids = p.actionIds();
    EXPECT_EQ(ids[3], "throughAll") << "Through all next to Cut while cutting";
    EXPECT_EQ(p.controller.setValueText("3"), "");
    EXPECT_EQ(p.controller.operation()->valueLabel(), "Height");
    ASSERT_TRUE(p.controller.triggerAction("mode:new").ok());
    EXPECT_EQ(p.controller.operation()->valueLabel(), "New body");
    ASSERT_TRUE(p.controller.triggerAction("symmetric").ok());
    EXPECT_EQ(p.controller.operation()->valueLabel(), "Thickness");
}

// 5. A finger dragging the selected profile drags its arrow (it used to
// orbit); the arrow starts where the profile was tapped.
TEST(CutFlow, DraggingTheSelectedProfileDragsItsArrow)
{
    Phone p;
    p.box();
    p.sketchOnTop();
    p.rectangle({-8, -8}, {8, 8});
    p.finish();
    p.tap(p.screen({5, -5, 20}));
    ASSERT_NE(p.extrude(), nullptr);
    const Vec3 base = p.extrude()->manipulator().base();
    EXPECT_NEAR((base - Vec3{5, -5, 20}).length(), 0, 0.2) << "the arrow starts under the finger";
    const Camera before = p.controller.camera();
    // From another point of the region (beside the arrow on screen), 40 px
    // down the screen: into the body.
    const Vec2 from = p.screen({-5, -5, 20});
    ASSERT_GT((from - p.screen(base)).length(), 60);
    p.drag(from, from + Vec2{0, 40});
    EXPECT_NEAR(p.controller.camera().yaw, before.yaw, 1e-12) << "no orbit";
    EXPECT_NEAR(p.controller.camera().pitch, before.pitch, 1e-12) << "no orbit";
    ASSERT_NE(p.extrude(), nullptr);
    EXPECT_LT(p.extrude()->distance(), 0);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Cut);
    // A drag starting off the profile still orbits.
    const Vec2 off = p.screen({40, 40, 0});
    p.drag(off, off + Vec2{40, 0});
    EXPECT_GT(std::abs(p.controller.camera().yaw - before.yaw), 1e-3);
    // A mouse drag on the profile orbits (a mouse has the arrow and a hover).
    Phone m({1400, 900}, false);
    m.box();
    m.sketchOnTop();
    m.rectangle({-8, -8}, {8, 8});
    m.finish();
    m.tap(m.screen({0, 0, 20}));
    const double yaw = m.controller.camera().yaw;
    const Vec2 at = m.screen({5, 5, 20}); // beside the arrow on screen
    m.drag(at, at + Vec2{60, 0});
    EXPECT_GT(std::abs(m.controller.camera().yaw - yaw), 1e-3);
    EXPECT_DOUBLE_EQ(m.extrude()->distance(), 0.0);
}

TEST(CutFlow, DraggingTheSelectedFacePushesIt)
{
    Phone p;
    p.box();
    p.tap(p.screen({0, 0, 20}));
    ASSERT_NE(p.controller.operation(), nullptr);
    ASSERT_EQ(p.controller.operation()->featureKind(), doc::FeatureKind::PushPull);
    const Camera before = p.controller.camera();
    const Vec2 from = p.screen({6, 6, 20});
    p.drag(from, from + Vec2{0, -40});
    EXPECT_NEAR(p.controller.camera().yaw, before.yaw, 1e-12);
    ASSERT_TRUE(p.commit().ok());
    const double top = geom::boundingBox(p.document.bodies()[0]->shape()).max.z;
    EXPECT_GT(top, 20.5) << "pulled up";
    EXPECT_NEAR(p.volume(), 400 * top, 1e-6);
}

// 6. A stray tap adds a second face on a touch screen: Sketch still works,
// on the flat face tapped last.
TEST(CutFlow, SeveralFacesSketchOnTheLastOneTapped)
{
    for (bool topLast : {true, false}) {
        Phone p;
        p.box();
        if (topLast) {
            p.tap(p.screen({0, -10, 10}));
            p.tap(p.screen({0, 0, 20}));
        } else {
            p.tap(p.screen({0, 0, 20}));
            p.tap(p.screen({0, -10, 10}));
        }
        ASSERT_EQ(p.controller.selection().size(), 2u);
        EXPECT_TRUE(p.offers("sketch"));
        ASSERT_TRUE(p.controller.triggerAction("sketch").ok());
        p.controller.skipAnimation();
        ASSERT_EQ(p.controller.mode(), InteractionController::Mode::Sketch);
        const Vec3 n = p.session().sketch().plane().normal();
        if (topLast)
            EXPECT_NEAR(n.z, 1.0, 1e-9);
        else
            EXPECT_NEAR(n.y, -1.0, 1e-9);
        ASSERT_TRUE(p.session().sketch().hostBody().has_value());
    }
}

// 7. A sketch on no body (the ground plane) pushed into a body cuts it; one
// that only touches it joins; one beside it is a new body.
TEST(CutFlow, SketchOnNoBodyCutsOrJoinsTheBodyItGoesInto)
{
    struct Case {
        Vec2 a, b;
        double distance;
        doc::ExtrudeMode mode;
        std::size_t bodies;
        double change; // of the first body's volume, per mm2 of the rectangle
    };
    const Case cases[] = {
        {{-5, -5}, {5, 5}, 5, doc::ExtrudeMode::Cut, 1, -5},      // up into the box
        {{-5, -5}, {5, 5}, -5, doc::ExtrudeMode::Join, 1, 5},     // down from its bottom
        {{20, -5}, {30, 5}, 5, doc::ExtrudeMode::NewBody, 2, 0},  // beside it
    };
    for (const Case& c : cases) {
        Phone p;
        p.box();
        ASSERT_TRUE(p.controller.startSketch().ok()); // nothing selected: the ground plane
        p.controller.skipAnimation();
        ASSERT_FALSE(p.session().sketch().hostBody().has_value());
        p.rectangle(c.a, c.b); // at the phone's fitted zoom: roughly
        EXPECT_GT(p.rectArea(), 50);
        p.finish();
        // Tapped from below (from above the box hides it).
        p.selectFromBelow((p.rectMin + p.rectMax) * 0.5);
        ASSERT_NE(p.extrude(), nullptr);
        EXPECT_TRUE(p.offers("mode:new") && p.offers("mode:join") && p.offers("mode:cut")) << "always offered";
        EXPECT_EQ(p.controller.setValueText(std::to_string(c.distance)), "");
        EXPECT_EQ(p.extrude()->mode(), c.mode);
        EXPECT_EQ(p.controller.operation()->valueLabel(),
                  c.mode == doc::ExtrudeMode::Cut ? "Cut depth" : c.mode == doc::ExtrudeMode::Join ? "Height" : "New body");
        ASSERT_TRUE(p.commit().ok());
        ASSERT_EQ(p.document.bodies().size(), c.bodies);
        const double expected = kCube + c.change * p.rectArea();
        EXPECT_NEAR(p.volume(), expected, 1e-6);
        if (c.bodies == 2) {
            EXPECT_NEAR(p.volume(1), 5 * p.rectArea(), 1e-6);
        }
        // Undo and redo keep it.
        ASSERT_TRUE(p.controller.undo());
        EXPECT_NEAR(p.volume(), kCube, 1e-6);
        ASSERT_TRUE(p.controller.redo());
        EXPECT_NEAR(p.volume(), expected, 1e-6);
    }
}

// Cut chosen for a sketch on no body that misses every body says so.
TEST(CutFlow, SketchOnNoBodyCutChosenBesideTheBodyIsRefused)
{
    Phone p;
    p.box();
    ASSERT_TRUE(p.controller.startSketch().ok());
    p.controller.skipAnimation();
    p.rectangle({20, -5}, {30, 5});
    p.finish();
    p.selectFromBelow((p.rectMin + p.rectMax) * 0.5);
    ASSERT_TRUE(p.controller.triggerAction("mode:cut").ok());
    const std::string refusal = "This cut does not reach a body, so nothing would be removed. Extrude into a body, or further.";
    EXPECT_EQ(p.controller.setValueText("5"), refusal);
    EXPECT_EQ(p.extrude()->mode(), doc::ExtrudeMode::Cut);
    EXPECT_FALSE(p.controller.operation()->canCommit());
    EXPECT_EQ(p.controller.operation()->error(), refusal);
    ASSERT_TRUE(p.controller.triggerAction("mode:new").ok());
    EXPECT_TRUE(p.controller.operation()->canCommit());
    ASSERT_TRUE(p.commit().ok());
    EXPECT_EQ(p.document.bodies().size(), 2u);
}
