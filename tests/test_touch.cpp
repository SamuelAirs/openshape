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
