// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Values typed key by key are previewed once typing pauses (owner report
// from an iPad, 2026-09-27: typing 100 over 95 showed the model at 1 mm,
// then 10 mm, then 100 mm). A test clock stands in for the pause; previews
// run on the worker, as in the app, and are counted there.
#include "commands/Command.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/TypingPause.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

using namespace os;
using namespace os::interact;
using namespace std::chrono_literals;

namespace {

using Clock = TypingPause::Clock;

struct TypingHarness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    Clock::time_point now = Clock::time_point{} + 1h;

    explicit TypingHarness(bool async = true)
    {
        controller.setViewportSize({1200, 800});
        controller.setClockForTesting([this] { return now; });
        if (async)
            controller.enableAsyncPreviews({});
        EXPECT_TRUE(controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
        controller.fitAll(false);
    }

    PreviewWorker& worker() { return *controller.previewWorker(); }
    // Preview jobs the worker was given (run, or replaced before they ran).
    std::uint64_t jobs() { return worker().jobsRun() + worker().jobsReplaced(); }
    const doc::Body& body() const { return *document.bodies().front(); }
    double height() const { return geom::boundingBox(body().shape()).size().z; }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }

    static PointerEvent at(Vec2 p)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        return e;
    }
    void click(Vec2 p)
    {
        controller.pointerPress(at(p));
        controller.pointerRelease(at(p));
    }
    const Operation* selectTop()
    {
        click(screen({0, 0, 20}));
        const Operation* op = controller.operation();
        EXPECT_NE(op, nullptr);
        EXPECT_EQ(op ? op->title() : std::string(), "Push/Pull");
        return op;
    }
    // A key typed `after` the one before.
    void key(const std::string& textSoFar, std::chrono::milliseconds after = 100ms)
    {
        now += after;
        controller.typeValue(InteractionController::TypingTarget::OperationValue, textSoFar);
    }
    // The UI's timer: fires at the deadline.
    bool pauseOver()
    {
        now = controller.typingDeadline();
        return controller.advanceTyping();
    }
};

double meshHeight(const geom::Mesh& mesh)
{
    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 2; i < mesh.positions.size(); i += 3) {
        lo = std::min(lo, double(mesh.positions[i]));
        hi = std::max(hi, double(mesh.positions[i]));
    }
    return hi - lo;
}

} // namespace

// ---- The pause itself ---------------------------------------------------------------

TEST(TypingPause, TextIsDueOnlyAfterThePauseSinceTheLastKey)
{
    TypingPause pause;
    const Clock::time_point t0{};
    EXPECT_FALSE(pause.pending());
    EXPECT_FALSE(pause.takeIfDue(t0).has_value());
    pause.type("1", t0);
    pause.type("10", t0 + 150ms);
    pause.type("100", t0 + 300ms);
    EXPECT_TRUE(pause.pending());
    EXPECT_EQ(pause.text(), "100");
    EXPECT_EQ(pause.deadline(), t0 + 300ms + TypingPause::kPause);
    EXPECT_FALSE(pause.takeIfDue(t0 + 300ms + TypingPause::kPause - 1ms).has_value()) << "before the pause is over";
    const auto due = pause.takeIfDue(t0 + 300ms + TypingPause::kPause);
    ASSERT_TRUE(due.has_value());
    EXPECT_EQ(*due, "100");
    EXPECT_FALSE(pause.pending());
    EXPECT_FALSE(pause.take().has_value()) << "taken once";
}

TEST(TypingPause, ConfirmingTakesTheTextAtOnceAndClearingForgetsIt)
{
    TypingPause pause;
    const Clock::time_point t0{};
    pause.type("25", t0);
    const auto taken = pause.take();
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(*taken, "25");
    EXPECT_FALSE(pause.pending());
    pause.type("30", t0);
    pause.clear();
    EXPECT_FALSE(pause.pending());
    EXPECT_FALSE(pause.takeIfDue(t0 + 10s).has_value());
    // An empty text (everything erased) is still a text typed.
    pause.type("", t0);
    const auto empty = pause.takeIfDue(t0 + TypingPause::kPause);
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(*empty, "");
}

TEST(TypingPause, PauseIsLongerThanAKeyGapAndShortEnoughToFollow)
{
    EXPECT_GE(TypingPause::kPause, 500ms);
    EXPECT_LE(TypingPause::kPause, 1000ms);
}

// ---- Typing into the value chip ------------------------------------------------------

// 1, 0, 0 typed quickly over the push/pull's 20: nothing is previewed while
// typing; once typing pauses, the worker gets one job: 100.
TEST(TypingPause, TypingQuicklyPreviewsOnlyTheWholeValue)
{
    TypingHarness h;
    const Operation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    const std::uint64_t before = h.jobs();
    const std::uint64_t shownBefore = h.controller.previewsShown();
    h.key("1");
    h.key("10");
    h.key("100");
    EXPECT_TRUE(h.controller.typingPending());
    EXPECT_EQ(h.jobs(), before) << "a key typed started a preview";
    EXPECT_FALSE(op->previewPending());
    EXPECT_DOUBLE_EQ(op->value(), 20.0) << "the model keeps the last value while typing";
    // Just before the pause is over: still nothing.
    h.now = h.controller.typingDeadline() - 1ms;
    EXPECT_FALSE(h.controller.advanceTyping());
    EXPECT_EQ(h.jobs(), before);
    EXPECT_TRUE(h.pauseOver());
    EXPECT_FALSE(h.controller.typingPending());
    EXPECT_DOUBLE_EQ(op->value(), 100.0);
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_EQ(h.jobs(), before + 1) << "one preview for the three keys";
    EXPECT_EQ(h.controller.previewsShown(), shownBefore + 1);
    ASSERT_TRUE(op->hasPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 100.0, 1e-4);
    EXPECT_NEAR(h.height(), 20.0, 1e-9) << "a preview only";
}

// A pause after the first key previews it; the next keys start a new pause.
TEST(TypingPause, PauseAfterAKeyPreviewsWhatWasTyped)
{
    TypingHarness h;
    const Operation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    const std::uint64_t before = h.jobs();
    h.key("1");
    EXPECT_TRUE(h.pauseOver());
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_DOUBLE_EQ(op->value(), 1.0);
    ASSERT_TRUE(op->hasPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 1.0, 1e-4);
    EXPECT_EQ(h.jobs(), before + 1);
    h.key("10", 900ms);
    h.key("100");
    EXPECT_DOUBLE_EQ(op->value(), 1.0) << "the shown value stays until the next pause";
    EXPECT_TRUE(h.pauseOver());
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 100.0, 1e-4);
    EXPECT_EQ(h.jobs(), before + 2);
}

// Enter right after typing applies the typed value exactly (Enter in the
// value field confirms its text; Enter in the view and the check mark commit
// what was typed too).
TEST(TypingPause, EnterRightAfterTypingAppliesTheTypedValue)
{
    {
        TypingHarness h;
        ASSERT_NE(h.selectTop(), nullptr);
        h.key("1");
        h.key("10");
        h.key("100");
        // The value field's Enter: its whole text, then apply.
        EXPECT_EQ(h.controller.setValueText("100"), "");
        EXPECT_FALSE(h.controller.typingPending());
        ASSERT_TRUE(h.controller.commitOperation().ok());
        EXPECT_NEAR(h.height(), 100.0, 1e-9);
        EXPECT_NEAR(geom::volume(h.body().shape()), 20.0 * 20.0 * 100.0, 1e-6);
    }
    {
        TypingHarness h;
        ASSERT_NE(h.selectTop(), nullptr);
        h.key("4");
        h.key("45");
        EXPECT_TRUE(h.controller.keyPress(Key::Enter)) << "Enter in the view";
        EXPECT_FALSE(h.controller.typingPending());
        EXPECT_NEAR(h.height(), 45.0, 1e-9);
        EXPECT_NEAR(geom::volume(h.body().shape()), 20.0 * 20.0 * 45.0, 1e-6);
    }
    {
        TypingHarness h;
        ASSERT_NE(h.selectTop(), nullptr);
        h.key("3");
        h.key("35");
        ASSERT_TRUE(h.controller.commitOperation().ok()) << "the check mark";
        EXPECT_NEAR(h.height(), 35.0, 1e-9);
    }
}

// A tap elsewhere takes the value typed just now, then applies it (as it
// applies a previewed one) and selects.
TEST(TypingPause, TapElsewhereAppliesTheValueTypedJustNow)
{
    TypingHarness h;
    ASSERT_NE(h.selectTop(), nullptr);
    h.key("3");
    h.key("30");
    h.click({30, 30}); // empty space
    EXPECT_FALSE(h.controller.typingPending());
    EXPECT_NEAR(h.height(), 30.0, 1e-9);
    EXPECT_EQ(h.stack.undoLabel(), "Push/Pull");
}

// Esc (cancel), undo and another selection drop what was typed: nothing is
// previewed after the pause.
TEST(TypingPause, CancellingDropsTheTypedValue)
{
    TypingHarness h;
    const Operation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    const std::uint64_t before = h.jobs();
    h.key("5");
    h.key("50");
    h.controller.cancelOperation();
    EXPECT_FALSE(h.controller.typingPending());
    h.now += 5s;
    EXPECT_FALSE(h.controller.advanceTyping());
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_EQ(h.jobs(), before);
    EXPECT_NEAR(h.height(), 20.0, 1e-9);
}

// A text that is not a value is judged when it is taken: its message then,
// and nothing is previewed or applied.
TEST(TypingPause, RefusedTextIsJudgedWhenTaken)
{
    TypingHarness h;
    const std::string lastStep = h.stack.undoLabel();
    const Operation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    h.key("2");
    h.key("2x");
    EXPECT_TRUE(h.controller.typedValueError().empty()) << "not judged while typing";
    EXPECT_TRUE(h.pauseOver());
    EXPECT_FALSE(h.controller.typedValueError().empty());
    EXPECT_NE(h.controller.typedValueError().find("unit"), std::string::npos) << h.controller.typedValueError();
    EXPECT_DOUBLE_EQ(op->value(), 20.0);
    // The next key clears the message until that text is judged.
    h.key("2");
    EXPECT_TRUE(h.controller.typedValueError().empty());
    // Refused text and Enter: the error, and nothing applied.
    h.key("-");
    const Status status = h.controller.commitOperation();
    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(status.userMessage().empty());
    EXPECT_NEAR(h.height(), 20.0, 1e-9);
    EXPECT_EQ(h.stack.undoLabel(), lastStep);
}

// Synchronous previews (OPENSHAPE_SYNC_PREVIEWS) pause the same way.
TEST(TypingPause, SynchronousPreviewsPauseToo)
{
    TypingHarness h(false);
    const Operation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    h.key("1");
    h.key("10");
    EXPECT_DOUBLE_EQ(op->value(), 20.0);
    EXPECT_FALSE(op->hasPreview());
    EXPECT_TRUE(h.pauseOver());
    EXPECT_DOUBLE_EQ(op->value(), 10.0);
    ASSERT_TRUE(op->hasPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 10.0, 1e-4);
}

// ---- Typing a sketch's live values --------------------------------------------------

// A rectangle's width typed while drawing: the shape takes it after the
// pause, or at once with Tab (the next value) and Enter (done): 30 x 20.
TEST(TypingPause, SketchValuesWaitForThePauseAndTabOrEnterTakeThem)
{
    TypingHarness h(false);
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    SketchSession& session = *h.controller.sketchSession();
    auto sketchScreen = [&](Vec2 local) { return h.controller.camera().project(session.sketch().plane().toWorld(local)); };
    PointerEvent move = TypingHarness::at(sketchScreen({12, 7}));
    move.button = PointerButton::None;
    h.click(sketchScreen({0, 0}));
    ASSERT_TRUE(session.isDrawing());
    h.controller.pointerMove(move);
    auto locked = [&](const std::string& key) {
        for (const auto& label : session.labels(h.controller.camera()))
            if (label.kind == SketchLabel::Kind::Input && label.key == key)
                return label.locked;
        return false;
    };
    ASSERT_TRUE(session.hasInputs());
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "3");
    EXPECT_TRUE(h.pauseOver());
    EXPECT_TRUE(locked("width")) << "the pause over: the shape takes the 3";
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "30");
    h.controller.focusNextSketchInput(); // Tab: takes 30 at once
    EXPECT_FALSE(h.controller.typingPending());
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "2");
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "20");
    EXPECT_FALSE(locked("height")) << "not taken while typing";
    ASSERT_TRUE(h.controller.commitSketchTool().ok()); // Enter
    EXPECT_FALSE(session.isDrawing());
    bool corner = false;
    for (const auto& [id, p] : session.sketch().points())
        corner = corner || (std::abs(p.position.x - 30) < 1e-9 && std::abs(p.position.y - 20) < 1e-9);
    EXPECT_TRUE(corner) << "the opposite corner at (30, 20)";
    EXPECT_EQ(session.sketch().lines().size(), 4u);
}

// A tap that places the next point takes the value typed just now (a
// line's length typed, then the tap that ends it).
TEST(TypingPause, SketchTapTakesTheValueTypedJustNow)
{
    TypingHarness h(false);
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    SketchSession& session = *h.controller.sketchSession();
    auto sketchScreen = [&](Vec2 local) { return h.controller.camera().project(session.sketch().plane().toWorld(local)); };
    h.click(sketchScreen({0, 0}));
    ASSERT_TRUE(session.isDrawing());
    PointerEvent move = TypingHarness::at(sketchScreen({9, 0}));
    move.button = PointerButton::None;
    h.controller.pointerMove(move);
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "2");
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "25");
    h.click(sketchScreen({9, 0}));
    EXPECT_FALSE(h.controller.typingPending());
    bool end = false;
    for (const auto& [id, p] : session.sketch().points())
        end = end || (std::abs(p.position.x - 25) < 1e-6 && std::abs(p.position.y) < 1e-6);
    EXPECT_TRUE(end) << "the line ends 25 mm from the origin";
    // Esc drops a value typed for the next line.
    h.now += 100ms;
    h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "7");
    EXPECT_TRUE(h.controller.keyPress(Key::Escape));
    EXPECT_FALSE(h.controller.typingPending());
}

// A sketch value refused once typing paused keeps its message while the
// shape is drawn, and loses it when the user moves on: Tab to the next
// value, or a tap that finishes the shape; the next shape starts without it.
TEST(TypingPause, SketchRefusalGoesWithTheShape)
{
    TypingHarness h(false);
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    SketchSession& session = *h.controller.sketchSession();
    auto sketchScreen = [&](Vec2 local) { return h.controller.camera().project(session.sketch().plane().toWorld(local)); };
    auto hover = [&](Vec2 local) {
        PointerEvent move = TypingHarness::at(sketchScreen(local));
        move.button = PointerButton::None;
        h.controller.pointerMove(move);
    };
    auto typeRefused = [&] {
        h.now += 100ms;
        h.controller.typeValue(InteractionController::TypingTarget::SketchInput, "abc");
        EXPECT_TRUE(h.pauseOver());
        EXPECT_FALSE(h.controller.typedValueError().empty()) << "abc is not a length";
    };

    // Tab: on to the height, the width's refusal goes.
    h.click(sketchScreen({0, 0}));
    ASSERT_TRUE(session.isDrawing());
    hover({12, 7});
    typeRefused();
    h.controller.focusNextSketchInput();
    EXPECT_TRUE(h.controller.typedValueError().empty());

    // A tap that finishes the rectangle: the refusal goes with it.
    typeRefused();
    h.click(sketchScreen({12, 7}));
    EXPECT_FALSE(session.isDrawing());
    EXPECT_EQ(session.sketch().lines().size(), 4u);
    EXPECT_TRUE(h.controller.typedValueError().empty());
    h.click(sketchScreen({30, 30}));
    ASSERT_TRUE(session.isDrawing());
    EXPECT_TRUE(h.controller.typedValueError().empty()) << "the next shape starts without the old message";

    // Esc drops the shape and the message.
    hover({40, 40});
    typeRefused();
    EXPECT_TRUE(h.controller.keyPress(Key::Escape));
    EXPECT_TRUE(h.controller.typedValueError().empty());
}
