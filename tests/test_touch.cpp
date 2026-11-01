// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Touch gestures without a touch screen: frames fed straight into the
// recognizer, and pen/finger roles through the interaction controller.
#include "commands/Command.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchGestures.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace os;
using namespace os::interact;

namespace {

using State = TouchPoint::State;
using Kind = TouchIntent::Kind;

std::vector<Kind> kinds(const std::vector<TouchIntent>& intents)
{
    std::vector<Kind> out;
    for (const auto& i : intents)
        out.push_back(i.kind);
    return out;
}

bool has(const std::vector<TouchIntent>& intents, Kind kind)
{
    return std::any_of(intents.begin(), intents.end(), [kind](const TouchIntent& i) { return i.kind == kind; });
}

} // namespace

TEST(Touch, TapAndDoubleTap)
{
    TouchGestureRecognizer r;
    EXPECT_EQ(kinds(r.update({{1, {100, 100}, State::Pressed}}, 0.00)), std::vector<Kind>{Kind::PointerPress});
    EXPECT_EQ(kinds(r.update({{1, {101, 100}, State::Released}}, 0.08)), std::vector<Kind>{Kind::PointerRelease});
    // A second quick tap nearby is a double-tap.
    r.update({{2, {104, 102}, State::Pressed}}, 0.25);
    const auto second = r.update({{2, {104, 102}, State::Released}}, 0.30);
    EXPECT_TRUE(has(second, Kind::PointerRelease));
    EXPECT_TRUE(has(second, Kind::DoubleTap));
}

TEST(Touch, OneFingerDragPoints)
{
    TouchGestureRecognizer r;
    r.update({{1, {100, 100}, State::Pressed}}, 0.0);
    const auto move = r.update({{1, {160, 100}, State::Moved}}, 0.1);
    ASSERT_EQ(kinds(move), std::vector<Kind>{Kind::PointerMove});
    EXPECT_NEAR(move[0].position.x, 160, 1e-9);
    const auto up = r.update({{1, {200, 100}, State::Released}}, 0.2);
    EXPECT_EQ(kinds(up), std::vector<Kind>{Kind::PointerRelease}); // a drag is never a tap
}

TEST(Touch, TwoFingerTapUndoesThreeFingerTapRedoes)
{
    TouchGestureRecognizer r;
    r.update({{1, {100, 100}, State::Pressed}}, 0.00);
    const auto second = r.update({{1, {100, 100}, State::Stationary}, {2, {180, 110}, State::Pressed}}, 0.03);
    EXPECT_TRUE(has(second, Kind::PointerCancel)); // the first finger's press is not a tap
    EXPECT_FALSE(has(second, Kind::Pan));
    r.update({{1, {101, 100}, State::Released}, {2, {180, 110}, State::Stationary}}, 0.12);
    const auto end = r.update({{2, {180, 111}, State::Released}}, 0.14);
    EXPECT_EQ(kinds(end), std::vector<Kind>{Kind::Undo});

    r.update({{1, {100, 100}, State::Pressed}, {2, {150, 100}, State::Pressed}, {3, {200, 100}, State::Pressed}}, 1.0);
    const auto three = r.update({{1, {100, 100}, State::Released}, {2, {150, 100}, State::Released},
                                 {3, {200, 100}, State::Released}},
                                1.1);
    EXPECT_EQ(kinds(three), std::vector<Kind>{Kind::Redo});
}

TEST(Touch, TwoFingersPanOnlyAfterRealMovement)
{
    TouchGestureRecognizer r;
    r.update({{1, {100, 100}, State::Pressed}, {2, {200, 100}, State::Pressed}}, 0.0);
    // A few pixels of jitter: no navigation yet.
    EXPECT_FALSE(has(r.update({{1, {103, 101}, State::Moved}, {2, {203, 101}, State::Moved}}, 0.05), Kind::Pan));
    // Past the threshold: the pan includes the whole movement so far.
    const auto pan = r.update({{1, {130, 100}, State::Moved}, {2, {230, 100}, State::Moved}}, 0.10);
    ASSERT_TRUE(has(pan, Kind::Pan));
    const auto& p = *std::find_if(pan.begin(), pan.end(), [](const TouchIntent& i) { return i.kind == Kind::Pan; });
    EXPECT_NEAR(p.to.x - p.from.x, 30.0, 1e-9);
    // Lifting after moving is not an undo.
    r.update({{1, {130, 100}, State::Released}, {2, {230, 100}, State::Moved}}, 0.12);
    EXPECT_FALSE(has(r.update({{2, {230, 100}, State::Released}}, 0.13), Kind::Undo));
}

TEST(Touch, PinchZooms)
{
    TouchGestureRecognizer r;
    r.update({{1, {100, 100}, State::Pressed}, {2, {200, 100}, State::Pressed}}, 0.0);
    r.update({{1, {80, 100}, State::Moved}, {2, {220, 100}, State::Moved}}, 0.1); // crosses the threshold
    const auto pinch = r.update({{1, {50, 100}, State::Moved}, {2, {250, 100}, State::Moved}}, 0.2);
    const auto it = std::find_if(pinch.begin(), pinch.end(), [](const TouchIntent& i) { return i.kind == Kind::Pinch; });
    ASSERT_NE(it, pinch.end());
    EXPECT_NEAR(it->scale, 200.0 / 140.0, 1e-9);
}

TEST(Touch, SlowTwoFingerTapDoesNotUndo)
{
    TouchGestureRecognizer r;
    r.update({{1, {100, 100}, State::Pressed}, {2, {200, 100}, State::Pressed}}, 0.0);
    r.update({{1, {100, 100}, State::Released}, {2, {200, 100}, State::Stationary}}, 0.9);
    EXPECT_FALSE(has(r.update({{2, {200, 100}, State::Released}}, 1.0), Kind::Undo));
}

// With a pen around, fingers only navigate: a finger tap on a face does not
// select it, while the pen does.
TEST(Touch, PenMakesFingersNavigateOnly)
{
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    controller.onMessage = [&](const std::string& m) { messages.push_back(m); };
    controller.setViewportSize({1200, 800});
    ASSERT_TRUE(controller.createBox(20).ok());
    controller.fitAll(false);
    const Vec2 top = controller.camera().project({0, 0, 20});
    auto tap = [&](PointerDevice device) {
        PointerEvent e;
        e.device = device;
        e.position = top;
        controller.pointerPress(e);
        controller.pointerRelease(e);
    };
    tap(PointerDevice::Touch);
    EXPECT_EQ(controller.selection().size(), 1u); // no pen yet: fingers select
    controller.cancelOperation();
    controller.cancelOperation();
    ASSERT_TRUE(controller.selection().empty());

    tap(PointerDevice::Pen);
    EXPECT_EQ(controller.selection().size(), 1u);
    EXPECT_TRUE(controller.penMode());
    ASSERT_FALSE(messages.empty()); // the user is told once
    controller.cancelOperation();
    controller.cancelOperation();
    tap(PointerDevice::Touch);
    EXPECT_TRUE(controller.selection().empty()); // fingers navigate only
    controller.setPenMode(false);
    tap(PointerDevice::Touch);
    EXPECT_EQ(controller.selection().size(), 1u);
}

// ---- A pinch with a face selected (the owner's iPhone Duo, 2026-11-03) ----------------
//
// A recorded-style pinch on the Duo's inner display (951 x 669 points): the
// first finger lands a frame before the second, both spread at 120 Hz with
// the midpoint drifting sideways and a little jitter, then lift one after
// the other. Between frames the window does what Main.qml does when the
// value box moves (each zoom step moves it): it asks the view to reveal the
// selection in a room that does not hold it. That reveal made the view jump
// in size and place on the device; during the gesture it must be refused.

namespace {

struct PinchReplay {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    TouchGestureRecognizer recognizer;
    TypingPause::Clock::time_point now{};
    int undos = 0;

    PinchReplay()
    {
        controller.setClockForTesting([this] { return now; });
        controller.setViewportSize({951, 669});
        EXPECT_TRUE(controller.createBox(20).ok());
        controller.fitAll(false);
    }

    void feed(const std::vector<TouchPoint>& points, double seconds)
    {
        now = TypingPause::Clock::time_point{} + std::chrono::microseconds(static_cast<long long>(seconds * 1e6));
        for (const TouchIntent& intent : recognizer.update(points, seconds)) {
            PointerEvent e;
            e.device = PointerDevice::Touch;
            e.button = PointerButton::Left;
            e.position = intent.position;
            switch (intent.kind) {
            case Kind::PointerPress: controller.pointerPress(e); break;
            case Kind::PointerMove: controller.pointerMove(e); break;
            case Kind::PointerRelease: controller.pointerRelease(e); break;
            case Kind::PointerCancel: controller.cancelPointer(PointerDevice::Touch); break;
            case Kind::DoubleTap: controller.pointerDoubleClick(e); break;
            case Kind::Pan: controller.twoFingerPan(intent.from, intent.to); break;
            case Kind::Pinch: controller.pinch(intent.position, intent.scale); break;
            case Kind::Undo:
            case Kind::Redo: ++undos; break;
            }
        }
    }
};

} // namespace

TEST(Touch, ReplayedPinchWithAFaceSelectedZoomsSteadilyAboutTheFingers)
{
    PinchReplay r;
    // Tap the box's top face: Push/Pull with its arrow and value box.
    const Vec2 top = r.controller.camera().project({0, 0, 20});
    r.feed({{1, top, State::Pressed}}, 0.0);
    r.feed({{1, top, State::Released}}, 0.06);
    ASSERT_EQ(r.controller.selection().size(), 1u);
    ASSERT_NE(r.controller.operation(), nullptr);
    const auto selected = r.controller.selection().items();

    // A room the selection is not in (the value box sat over it).
    const ScreenRect room{20, 20, 260, 260};
    ASSERT_TRUE(r.controller.keepClearRect().has_value());
    ASSERT_FALSE(room.contains(*r.controller.keepClearRect()));

    // The pinch, about the box: 0.5 s later, so it is not a double-tap.
    const Vec2 c0 = top + Vec2{40, 30};
    const double t0 = 0.6, dt = 1.0 / 120.0;
    const int frames = 48;
    // Small deterministic jitter, as fingers on glass report.
    const double jitter[] = {0.4, -0.3, 0.6, -0.5, 0.1, 0.3, -0.6, 0.2};
    auto fingers = [&](int i) {
        const double k = double(i) / frames;
        const double half = 50 + 150 * k; // the span grows from 100 to 400 px
        const Vec2 mid = c0 + Vec2{30 * k, -12 * k}; // the midpoint drifts
        const Vec2 a = mid + Vec2{-half, 6 + jitter[i % 8]};
        const Vec2 b = mid + Vec2{half, -6 + jitter[(i + 3) % 8]};
        return std::pair<Vec2, Vec2>{a, b};
    };
    // The world point under the fingers' midpoint when they land.
    const auto startHit = r.controller.pickAt(c0, InputProfile::forDevice(PointerDevice::Touch));
    ASSERT_TRUE(startHit.hit());
    const Vec3 anchor = startHit.point;

    const auto [a0, b0] = fingers(0);
    r.feed({{1, a0, State::Pressed}}, t0); // one finger first...
    r.feed({{1, a0, State::Stationary}, {2, b0, State::Pressed}}, t0 + dt); // ...then the second
    double lastDistance = r.controller.camera().distance;
    int zoomSteps = 0;
    int reveals = 0;
    for (int i = 1; i <= frames; ++i) {
        const auto [a, b] = fingers(i);
        const Vec3 targetBefore = r.controller.camera().target;
        r.feed({{1, a, State::Moved}, {2, b, State::Moved}}, t0 + dt * (i + 1));
        const double distance = r.controller.camera().distance;
        EXPECT_LE(distance, lastDistance + 1e-9) << "frame " << i << ": zooming in never zooms out";
        const bool viewMoved = distance < lastDistance - 1e-12 || (r.controller.camera().target - targetBefore).length() > 1e-12;
        if (distance < lastDistance - 1e-12)
            ++zoomSteps;
        lastDistance = distance;
        // Each step moved the value box; the window then asks to reveal the
        // selection (Main.qml, valueChip.onPlacementChanged): refused.
        if (viewMoved) {
            ++reveals;
            EXPECT_FALSE(r.controller.revealKeepClear(room)) << "frame " << i;
        }
    }
    EXPECT_GT(zoomSteps, frames / 2);
    EXPECT_GT(reveals, frames / 2);
    // The point that was under the fingers' midpoint is under it still.
    const auto [aEnd, bEnd] = fingers(frames);
    const Vec2 midEnd = (aEnd + bEnd) * 0.5;
    const Vec2 anchorNow = r.controller.camera().project(anchor);
    EXPECT_NEAR(anchorNow.x, midEnd.x, 1.5);
    EXPECT_NEAR(anchorNow.y, midEnd.y, 1.5);

    // The fingers lift one after the other: no tap, no undo, the selection kept.
    r.feed({{1, aEnd, State::Released}, {2, bEnd, State::Stationary}}, t0 + dt * (frames + 2));
    r.feed({{2, bEnd, State::Released}}, t0 + dt * (frames + 5));
    EXPECT_EQ(r.undos, 0);
    ASSERT_EQ(r.controller.selection().size(), selected.size());
    for (const auto& item : selected)
        EXPECT_TRUE(r.controller.selection().contains(item));

    // Still quiet just after: the view stays where the fingers left it.
    r.now += std::chrono::milliseconds(200);
    EXPECT_FALSE(r.controller.revealKeepClear(room));
    // Later (the phone folded, say), the selection is brought into sight again.
    r.now += std::chrono::milliseconds(600);
    EXPECT_TRUE(r.controller.revealKeepClear(room));
}
