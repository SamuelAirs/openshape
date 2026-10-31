// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Apple Pencil without an iPad: palm rejection driven by fake timestamps,
// and pen, palm and finger events fed straight into the interaction
// controller at the iPad Air 11" size (1180 x 820). The owner's Pencil
// (USB-C) has tilt only: nothing here relies on pressure, hover (the iPhone
// has none) or Pencil gestures; hover is checked as the iPad's bonus.
#include "commands/Command.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "interaction/PalmRejection.h"
#include "interaction/TouchGestures.h"
#include "interaction/TouchWording.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

using namespace os;
using namespace os::interact;

namespace {

using State = TouchPoint::State;
using Kind = TouchIntent::Kind;

bool has(const std::vector<TouchIntent>& intents, Kind kind)
{
    return std::any_of(intents.begin(), intents.end(), [kind](const TouchIntent& i) { return i.kind == kind; });
}

// Palm rejection in front of the gesture recognizer, as ViewportItem has them.
struct Touches {
    PalmRejection palm;
    TouchGestureRecognizer gestures;
    std::vector<TouchIntent> frame(const std::vector<TouchPoint>& points, double t)
    {
        return gestures.update(palm.filter(points, t), t);
    }
    std::vector<TouchIntent> penDown(double t)
    {
        palm.penDown(t);
        return gestures.abandon();
    }
};

struct Ipad {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    TypingPause::Clock::time_point now{std::chrono::hours(1)};

    Ipad()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setClockForTesting([this] { return now; });
        controller.setViewportSize({1180, 820});
        controller.setTouchLayout(true);
    }
    static PointerEvent at(PointerDevice device, Vec2 p, PointerButton button = PointerButton::Left)
    {
        PointerEvent e;
        e.device = device;
        e.position = p;
        e.button = button;
        return e;
    }
    void tap(PointerDevice device, Vec2 p)
    {
        controller.pointerPress(at(device, p));
        controller.pointerRelease(at(device, p));
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    Vec2 sketchScreen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }
    void box()
    {
        ASSERT_TRUE(controller.createBox(20).ok());
        controller.fitAll(false);
    }
    void sketchOnTop()
    {
        tap(PointerDevice::Pen, screen({0, 0, 20}));
        ASSERT_TRUE(controller.triggerAction("sketch").ok());
        controller.skipAnimation();
        ASSERT_EQ(controller.mode(), InteractionController::Mode::Sketch);
        ASSERT_EQ(controller.sketchSession()->tool(), SketchTool::Rectangle);
    }
    bool hasPoint(Vec2 p)
    {
        for (const auto& [id, q] : controller.sketchSession()->sketch().points())
            if (id != sketch::kOriginId && (q.position - p).length() < 1e-9)
                return true;
        return false;
    }
    std::size_t pointCount() { return controller.sketchSession()->sketch().points().size() - 1; }
    // The camera, as where two model points appear.
    std::pair<Vec2, Vec2> view() const { return {screen({0, 0, 0}), screen({10, 20, 30})}; }
};

bool sameView(const std::pair<Vec2, Vec2>& a, const std::pair<Vec2, Vec2>& b)
{
    return (a.first - b.first).length() < 1e-9 && (a.second - b.second).length() < 1e-9;
}

} // namespace

// ---- Palm rejection -------------------------------------------------------------------

TEST(Pencil, TouchesBeginningWhileThePenIsDownAreIgnoredForTheirWholeLife)
{
    Touches t;
    t.penDown(1.0);
    EXPECT_TRUE(t.frame({{1, {500, 500}, State::Pressed}}, 1.1).empty());
    EXPECT_TRUE(t.frame({{1, {600, 520}, State::Moved}}, 1.2).empty()) << "a sliding palm does not drag";
    t.palm.penUp(1.3);
    // Still the palm after the pen lifted, and after the grace period.
    EXPECT_TRUE(t.frame({{1, {700, 540}, State::Moved}}, 2.5).empty());
    EXPECT_TRUE(t.frame({{1, {700, 540}, State::Released}}, 2.6).empty());
    EXPECT_FALSE(t.palm.ignores(1)) << "forgotten once it lifted";
}

TEST(Pencil, TheGracePeriodAfterThePenLifts)
{
    Touches t;
    t.penDown(1.0);
    t.palm.penUp(2.0);
    // A palm landing just after the pen lifted.
    EXPECT_TRUE(t.frame({{1, {500, 500}, State::Pressed}}, 2.0 + PalmRejection::kGrace - 0.01).empty());
    EXPECT_TRUE(t.frame({{1, {540, 500}, State::Moved}}, 2.0 + PalmRejection::kGrace + 0.2).empty());
    EXPECT_TRUE(t.frame({{1, {540, 500}, State::Released}}, 2.0 + PalmRejection::kGrace + 0.3).empty());
    // A finger after the grace period points again.
    const auto press = t.frame({{2, {300, 300}, State::Pressed}}, 2.0 + PalmRejection::kGrace + 0.01);
    EXPECT_TRUE(has(press, Kind::PointerPress));
    EXPECT_TRUE(has(t.frame({{2, {360, 300}, State::Moved}}, 2.7), Kind::PointerMove));
    EXPECT_TRUE(has(t.frame({{2, {360, 300}, State::Released}}, 2.8), Kind::PointerRelease));
}

TEST(Pencil, APalmMakesNoGesturesNorUndoTaps)
{
    Touches t;
    t.penDown(0.0);
    // Two contacts of the hand, still and quick: would be a two-finger undo tap.
    EXPECT_TRUE(t.frame({{1, {500, 500}, State::Pressed}, {2, {560, 520}, State::Pressed}}, 0.10).empty());
    EXPECT_TRUE(t.frame({{1, {500, 500}, State::Released}, {2, {560, 520}, State::Released}}, 0.15).empty());
    // Three, spreading and sliding: would pinch and pan, then redo.
    EXPECT_TRUE(t.frame({{3, {500, 500}, State::Pressed}, {4, {560, 520}, State::Pressed}, {5, {620, 540}, State::Pressed}}, 0.2).empty());
    EXPECT_TRUE(t.frame({{3, {450, 500}, State::Moved}, {4, {600, 560}, State::Moved}, {5, {700, 540}, State::Moved}}, 0.3).empty());
    t.palm.penUp(0.35);
    EXPECT_TRUE(t.frame({{3, {450, 500}, State::Released}, {4, {600, 560}, State::Released}, {5, {700, 540}, State::Released}}, 0.4)
                    .empty());
}

TEST(Pencil, FingersAlreadyNavigatingEndCleanlyWhenThePenComesDown)
{
    // One finger pressing (it may become a tap that selects): cancelled.
    {
        Touches t;
        ASSERT_TRUE(has(t.frame({{1, {500, 500}, State::Pressed}}, 0.0), Kind::PointerPress));
        const auto down = t.penDown(0.1);
        EXPECT_TRUE(has(down, Kind::PointerCancel));
        EXPECT_TRUE(t.frame({{1, {560, 500}, State::Moved}}, 0.2).empty());
        t.palm.penUp(0.3);
        EXPECT_TRUE(t.frame({{1, {560, 500}, State::Released}}, 1.5).empty()) << "no release, so no click";
    }
    // Two fingers panning: the pan stops; no undo when they lift.
    {
        Touches t;
        t.frame({{1, {100, 100}, State::Pressed}, {2, {200, 100}, State::Pressed}}, 0.0);
        ASSERT_TRUE(has(t.frame({{1, {130, 100}, State::Moved}, {2, {230, 100}, State::Moved}}, 0.05), Kind::Pan));
        const auto down = t.penDown(0.1);
        EXPECT_FALSE(has(down, Kind::PointerCancel)) << "no one-finger press was in progress";
        EXPECT_TRUE(t.frame({{1, {160, 100}, State::Moved}, {2, {260, 100}, State::Moved}}, 0.15).empty());
        EXPECT_TRUE(t.frame({{1, {160, 100}, State::Released}, {2, {260, 100}, State::Released}}, 0.2).empty());
        t.palm.penUp(0.3);
        // The next sequence, after the grace period, starts afresh (no stale double-tap).
        EXPECT_TRUE(has(t.frame({{3, {400, 400}, State::Pressed}}, 1.0), Kind::PointerPress));
        const auto up = t.frame({{3, {400, 400}, State::Released}}, 1.05);
        EXPECT_TRUE(has(up, Kind::PointerRelease));
        EXPECT_FALSE(has(up, Kind::DoubleTap));
    }
}

TEST(Pencil, SystemCancelledTouchesAreForgotten)
{
    Touches t;
    EXPECT_FALSE(t.palm.rejecting(0.0));
    t.penDown(1.0);
    t.frame({{1, {300, 300}, State::Pressed}}, 1.1);
    EXPECT_TRUE(t.palm.ignores(1));
    t.palm.penUp(1.2);
    EXPECT_TRUE(t.palm.rejecting(1.2 + PalmRejection::kGrace - 0.01));
    EXPECT_FALSE(t.palm.rejecting(1.2 + PalmRejection::kGrace));
    t.palm.touchesCancelled(); // the touch never sends its release
    EXPECT_FALSE(t.palm.ignores(1));
    // The same id, reused by a finger after the grace period, points.
    EXPECT_TRUE(has(t.frame({{1, {300, 300}, State::Pressed}}, 2.0), Kind::PointerPress));
}

// ---- Pen and fingers in the controller ---------------------------------------------------

// A pen draws a rectangle while a palm touches down, slides and lifts in the
// middle of the stroke: the rectangle is exactly the one drawn and the view
// did not move.
TEST(Pencil, APalmDuringAPenStrokeChangesNothing)
{
    Ipad p;
    p.box();
    p.sketchOnTop();
    const auto before = p.view();
    const Vec2 a = p.sketchScreen({-10, 10}) + Vec2{4, 3};
    const Vec2 b = p.sketchScreen({10, -10}) + Vec2{-3, -4};
    p.controller.pointerPress(Ipad::at(PointerDevice::Pen, a));
    Vec2 palm{900, 650};
    for (int i = 1; i <= 12; ++i) {
        if (i == 3)
            p.controller.pointerPress(Ipad::at(PointerDevice::Touch, palm));
        if (i > 3 && i < 9) {
            palm = palm + Vec2{14, 9};
            p.controller.pointerMove(Ipad::at(PointerDevice::Touch, palm));
        }
        if (i == 6)
            p.controller.pointerDoubleClick(Ipad::at(PointerDevice::Touch, palm));
        if (i == 9)
            p.controller.pointerRelease(Ipad::at(PointerDevice::Touch, palm));
        p.controller.pointerMove(Ipad::at(PointerDevice::Pen, a + (b - a) * (i / 12.0)));
    }
    p.controller.pointerRelease(Ipad::at(PointerDevice::Pen, b));
    EXPECT_TRUE(p.hasPoint({-10, -10}) && p.hasPoint({10, -10}) && p.hasPoint({10, 10}) && p.hasPoint({-10, 10}));
    EXPECT_EQ(p.pointCount(), 4u);
    ASSERT_EQ(p.controller.sketchSession()->regions().size(), 1u);
    EXPECT_NEAR(p.controller.sketchSession()->regions().front().area, 400.0, 1e-6);
    EXPECT_TRUE(sameView(before, p.view())) << "the palm neither panned nor orbited";
    EXPECT_TRUE(p.controller.penMode());
}

// A finger was dragging the view when the pen came down: the drag ends
// there (no click, no stray point) and the finger's later moves do nothing.
TEST(Pencil, ThePenEndsAFingersDrag)
{
    Ipad p;
    p.box();
    p.sketchOnTop();
    p.controller.setPenMode(false); // a finger draws too, until the pen comes down
    const Vec2 finger = p.sketchScreen({-10, 10}) + Vec2{4, 3};
    p.controller.pointerPress(Ipad::at(PointerDevice::Touch, finger)); // a rectangle's first corner
    p.controller.pointerMove(Ipad::at(PointerDevice::Touch, finger + Vec2{2, 1}));
    const auto before = p.view();
    const Vec2 a = p.sketchScreen({-5, 5});
    const Vec2 b = p.sketchScreen({5, -5});
    p.controller.pointerPress(Ipad::at(PointerDevice::Pen, a));
    p.controller.pointerMove(Ipad::at(PointerDevice::Touch, finger + Vec2{80, 60}));
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, (a + b) * 0.5));
    p.controller.pointerRelease(Ipad::at(PointerDevice::Touch, finger + Vec2{80, 60}));
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, b));
    p.controller.pointerRelease(Ipad::at(PointerDevice::Pen, b));
    EXPECT_TRUE(sameView(before, p.view()));
    EXPECT_FALSE(p.hasPoint({-10, 10})) << "the finger's corner was cancelled";
    EXPECT_EQ(p.pointCount(), 4u);
    ASSERT_EQ(p.controller.sketchSession()->regions().size(), 1u);
    EXPECT_NEAR(p.controller.sketchSession()->regions().front().area, 100.0, 1.0); // 10 x 10, to the grid
}

// The system cancelling the touches (UIKit's own palm rejection) must not
// end the pen's stroke.
TEST(Pencil, CancelledTouchesLeaveThePenStroke)
{
    Ipad p;
    p.box();
    p.sketchOnTop();
    const Vec2 a = p.sketchScreen({-10, 10}) + Vec2{4, 3};
    const Vec2 b = p.sketchScreen({10, -10}) + Vec2{-3, -4};
    p.controller.pointerPress(Ipad::at(PointerDevice::Pen, a));
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, (a + b) * 0.5));
    p.controller.cancelPointer(PointerDevice::Touch);
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, b));
    p.controller.pointerRelease(Ipad::at(PointerDevice::Pen, b));
    ASSERT_EQ(p.controller.sketchSession()->regions().size(), 1u);
    EXPECT_NEAR(p.controller.sketchSession()->regions().front().area, 400.0, 1e-6);
}

// ---- Pen hover (the iPad's bonus) ---------------------------------------------------------

TEST(Pencil, PenHoverHighlightsWithThePensTolerances)
{
    Ipad p;
    p.box();
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, p.screen({0, 0, 20}), PointerButton::None));
    EXPECT_EQ(p.controller.hover().kind, sel::PickKind::Face);
    EXPECT_EQ(p.controller.hoverDevice(), PointerDevice::Pen);
    // 8 px inside the top face from its front edge: an edge for the pen
    // (9 px), the face for a mouse (6 px).
    const Vec2 edge = p.screen({0, -10, 20});
    const Vec2 inward = (p.screen({0, 0, 20}) - edge) * (8.0 / (p.screen({0, 0, 20}) - edge).length());
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, edge + inward, PointerButton::None));
    EXPECT_EQ(p.controller.hover().kind, sel::PickKind::Edge);
    p.controller.pointerMove(Ipad::at(PointerDevice::Mouse, edge + inward, PointerButton::None));
    EXPECT_EQ(p.controller.hover().kind, sel::PickKind::Face);
    EXPECT_EQ(p.controller.hoverDevice(), PointerDevice::Mouse);
}

TEST(Pencil, PenHoverInASketchShowsTheSnappedPoint)
{
    Ipad p;
    p.box();
    p.sketchOnTop();
    const auto* s = p.controller.sketchSession();
    EXPECT_FALSE(s->cursorPoint());
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, p.sketchScreen({10, 10}) + Vec2{-5, 6}, PointerButton::None));
    ASSERT_TRUE(s->cursorPoint());
    EXPECT_NEAR((*s->cursorPoint() - Vec2{10, 10}).length(), 0.0, 1e-9) << "snapped to the face's corner";
    EXPECT_EQ(s->pointerDevice(), PointerDevice::Pen);
    EXPECT_FALSE(p.controller.sketchLoupe().visible) << "a hover never shows the loupe";
    // A hover re-sent while the pen is down is not a move of the stroke.
    const Vec2 a = p.sketchScreen({-10, 10}) + Vec2{4, 3};
    const Vec2 b = p.sketchScreen({10, -10}) + Vec2{-3, -4};
    p.controller.pointerPress(Ipad::at(PointerDevice::Pen, a));
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, b));
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, a + Vec2{30, 0}, PointerButton::None));
    p.controller.pointerRelease(Ipad::at(PointerDevice::Pen, b));
    ASSERT_EQ(p.controller.sketchSession()->regions().size(), 1u);
    EXPECT_NEAR(p.controller.sketchSession()->regions().front().area, 400.0, 1e-6);
}

// ---- The loupe is the finger's ------------------------------------------------------------

TEST(Pencil, ThePenNeverShowsTheLoupe)
{
    Ipad p;
    p.box();
    p.sketchOnTop();
    const Vec2 corner = p.sketchScreen({10, 10});
    p.controller.pointerPress(Ipad::at(PointerDevice::Pen, corner + Vec2{-5, 6}));
    EXPECT_TRUE(p.controller.sketchSession()->snapInfo(p.controller.camera()).active);
    EXPECT_FALSE(p.controller.sketchLoupe().visible);
    p.controller.pointerMove(Ipad::at(PointerDevice::Pen, corner + Vec2{-60, 50}));
    EXPECT_FALSE(p.controller.sketchLoupe().visible);
    p.controller.pointerRelease(Ipad::at(PointerDevice::Pen, corner + Vec2{-60, 50}));
    // A finger (pen mode off) does show it.
    p.controller.setPenMode(false);
    p.controller.pointerPress(Ipad::at(PointerDevice::Touch, corner + Vec2{-5, 6}));
    EXPECT_TRUE(p.controller.sketchLoupe().visible);
    p.controller.pointerRelease(Ipad::at(PointerDevice::Touch, corner + Vec2{-5, 6}));
}

// ---- The hint for a finger tap pen mode ignores --------------------------------------------

TEST(Pencil, AFingerTapInPenModeSaysHowToSelectWithAFinger)
{
    Ipad p;
    p.box();
    p.controller.setPenMode(true);
    const Vec2 top = p.screen({0, 0, 20});
    p.tap(PointerDevice::Touch, top);
    EXPECT_TRUE(p.controller.selection().empty()) << "fingers only move the view";
    ASSERT_EQ(p.messages.size(), 1u);
    EXPECT_EQ(p.messages.back(), InteractionController::kPenFingerHint);
    EXPECT_FALSE(mentionsMouseOrKeyboard(p.messages.back()));
    // Not again right away (a few taps in a row).
    p.now += std::chrono::seconds(2);
    p.tap(PointerDevice::Touch, top);
    EXPECT_EQ(p.messages.size(), 1u);
    // A drag only navigates: no hint; nor a tap on empty space.
    p.now += std::chrono::seconds(20);
    p.controller.pointerPress(Ipad::at(PointerDevice::Touch, top));
    p.controller.pointerMove(Ipad::at(PointerDevice::Touch, top + Vec2{60, 0}));
    p.controller.pointerRelease(Ipad::at(PointerDevice::Touch, top + Vec2{60, 0}));
    p.tap(PointerDevice::Touch, {20, 20});
    EXPECT_EQ(p.messages.size(), 1u);
    // Some time later, a tap says it again.
    p.tap(PointerDevice::Touch, p.screen({0, 0, 20}));
    EXPECT_EQ(p.messages.size(), 2u);
    EXPECT_TRUE(p.controller.selection().empty());
    // Pen off: the finger selects.
    p.controller.setPenMode(false);
    p.tap(PointerDevice::Touch, p.screen({0, 0, 20}));
    EXPECT_EQ(p.controller.selection().size(), 1u);
    EXPECT_EQ(p.messages.size(), 2u);
}

TEST(Pencil, AFingerTapInASketchInPenModeHintsToo)
{
    Ipad p;
    p.box();
    p.sketchOnTop();
    p.messages.clear();
    p.tap(PointerDevice::Touch, p.sketchScreen({3, 3}));
    EXPECT_EQ(p.pointCount(), 0u) << "the finger placed nothing";
    ASSERT_EQ(p.messages.size(), 1u);
    EXPECT_EQ(p.messages.back(), InteractionController::kPenFingerHint);
}
