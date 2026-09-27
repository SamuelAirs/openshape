// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Previews off the GUI thread (TD-1): the preview worker (latest request
// wins, results delivered on the caller's thread) and the interaction layer
// in asynchronous mode, as the app runs it. A slow kernel is simulated with
// the worker's job delay.
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/Fasteners.h"
#include "geometry/Holes.h"
#include "geometry/KernelSignals.h"
#include "geometry/Modeling.h"
#include "geometry/Text.h"
#include "interaction/InteractionController.h"
#include "interaction/PreviewWorker.h"
#include "PortableRandom.h"
#include "TestFonts.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <thread>

using namespace os;
using namespace os::interact;
using namespace std::chrono_literals;

namespace {

double msSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// The z extent of a mesh (a preview's height).
double meshHeight(const geom::Mesh& mesh)
{
    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 2; i < mesh.positions.size(); i += 3) {
        lo = std::min(lo, double(mesh.positions[i]));
        hi = std::max(hi, double(mesh.positions[i]));
    }
    return hi - lo;
}

// The volume a closed mesh encloses (divergence theorem).
double meshVolume(const geom::Mesh& mesh)
{
    double v = 0;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
        v += mesh.vertex(mesh.indices[t]).dot(mesh.vertex(mesh.indices[t + 1]).cross(mesh.vertex(mesh.indices[t + 2])));
    return std::abs(v) / 6;
}

double meshWidth(const geom::Mesh& mesh)
{
    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 0; i < mesh.positions.size(); i += 3) {
        lo = std::min(lo, double(mesh.positions[i]));
        hi = std::max(hi, double(mesh.positions[i]));
    }
    return hi - lo;
}

// The interaction layer with previews on the worker, as the app runs it
// (deliveries happen in waitForPreview / deliverPreviews, as the app's
// queued call would).
struct AsyncHarness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::atomic<int> notified{0};
    std::vector<std::string> messages;

    AsyncHarness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
        controller.enableAsyncPreviews([this] { ++notified; });
        EXPECT_TRUE(controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
        controller.fitAll(false);
    }

    PreviewWorker& worker() { return *controller.previewWorker(); }
    // Until the worker has started the job (then it can no longer be dropped
    // before it runs: its result arrives and must be recognized as stale).
    void untilRunning()
    {
        const auto t0 = std::chrono::steady_clock::now();
        while (!worker().running() && msSince(t0) < 5000)
            std::this_thread::yield();
        ASSERT_TRUE(worker().running());
    }
    const doc::Body& body() const { return *document.bodies().front(); }
    double height() const { return geom::boundingBox(body().shape()).size().z; }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }

    static PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left)
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        return e;
    }
    void clickAt(Vec2 p)
    {
        controller.pointerPress(at(p));
        controller.pointerRelease(at(p));
    }
    const PushPullOperation* selectTop()
    {
        clickAt(screen({0, 0, 20}));
        return dynamic_cast<const PushPullOperation*>(controller.operation());
    }
    // What the UI reads back after a state change, and the render scene.
    void uiReads() const
    {
        (void)controller.historyRows();
        (void)controller.contextActions();
        (void)controller.selectionSummary();
        (void)controller.operationValueText();
        (void)controller.renderScene();
    }
};

} // namespace

// ---- The worker ------------------------------------------------------------------------

TEST(PreviewWorker, LatestJobWinsAndResultsComeBackOnTheCallingThread)
{
    std::atomic<int> notified{0};
    std::atomic<std::thread::id> notifyThread{};
    PreviewWorker worker([&] {
        notifyThread = std::this_thread::get_id();
        ++notified;
    });
    std::promise<void> gate;
    const std::shared_future<void> open = gate.get_future().share();
    std::atomic<bool> started{false};
    std::vector<int> delivered;
    std::thread::id deliveryThread;
    auto job = [&](int id, bool block) -> PreviewWorker::Job {
        return [&, id, block]() -> PreviewWorker::Delivery {
            if (block) {
                started = true;
                open.wait();
            }
            return [&, id] {
                delivered.push_back(id);
                deliveryThread = std::this_thread::get_id();
            };
        };
    };
    worker.submit(job(1, true));
    while (!started)
        std::this_thread::yield();
    // Job 1 runs (a slow kernel call). Newer requests do not wait for it, and
    // the newest replaces the one still waiting.
    const auto t0 = std::chrono::steady_clock::now();
    worker.submit(job(2, false));
    worker.submit(job(3, false));
    EXPECT_LT(msSince(t0), 50.0);
    EXPECT_TRUE(worker.computing());
    EXPECT_EQ(worker.deliver(), 0);
    gate.set_value();
    ASSERT_TRUE(worker.waitUntilIdle(5s));
    EXPECT_EQ(worker.deliver(), 2);
    EXPECT_EQ(delivered, (std::vector<int>{1, 3}));
    EXPECT_EQ(worker.jobsRun(), 2u);
    EXPECT_EQ(worker.jobsReplaced(), 1u);
    EXPECT_EQ(notified.load(), 2);
    EXPECT_NE(notifyThread.load(), std::this_thread::get_id()) << "notify runs on the worker";
    EXPECT_EQ(deliveryThread, std::this_thread::get_id()) << "deliveries run on the caller's thread";
    EXPECT_FALSE(worker.busy());
}

TEST(PreviewWorker, WaitingJobCanBeDroppedAndARunningOneEndsWithTheWorker)
{
    std::atomic<int> ran{0};
    std::atomic<bool> started{false};
    std::promise<void> gate;
    const std::shared_future<void> open = gate.get_future().share();
    std::thread opener;
    {
        PreviewWorker worker;
        worker.submit([&]() -> PreviewWorker::Delivery {
            started = true;
            open.wait();
            ++ran;
            return [] {};
        });
        while (!started)
            std::this_thread::yield();
        worker.submit([&]() -> PreviewWorker::Delivery {
            ++ran;
            return [] { ADD_FAILURE() << "a dropped job was delivered"; };
        });
        worker.dropWaiting();
        gate.set_value();
        ASSERT_TRUE(worker.waitUntilIdle(5s));
        EXPECT_EQ(worker.deliver(), 1);
        EXPECT_EQ(ran.load(), 1) << "the dropped job ran";

        // A job still running when the worker ends: it finishes, its result
        // is dropped (the destructor waits for it).
        std::promise<void> second;
        const std::shared_future<void> secondOpen = second.get_future().share();
        started = false;
        worker.submit([&, secondOpen]() -> PreviewWorker::Delivery {
            started = true;
            secondOpen.wait();
            ++ran;
            return [] { ADD_FAILURE() << "delivered after the worker ended"; };
        });
        while (!started)
            std::this_thread::yield();
        opener = std::thread([p = std::move(second)]() mutable {
            std::this_thread::sleep_for(50ms);
            p.set_value();
        });
    }
    opener.join();
    EXPECT_EQ(ran.load(), 2);
}

// ---- The interaction layer -----------------------------------------------------------

// Dragging the push/pull arrow while the kernel takes 300 ms per preview:
// each pointer move (with what the UI reads back) returns at once and makes
// no kernel call on the GUI thread; the preview arrives afterwards.
TEST(AsyncPreview, DragStepsDoNotWaitForTheKernel)
{
    AsyncHarness h;
    const PushPullOperation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    ASSERT_TRUE(op->thickness().has_value());
    h.uiReads(); // the selection's facts are computed once, as the UI shows the selection
    h.worker().setJobDelayForTesting(300ms);

    const LinearManipulator handle = op->handle(0);
    const Vec3 anchor = handle.anchor(op->handleOffset(0));
    const double px = h.controller.camera().pixelSize(anchor);
    const Vec2 grab = h.screen(anchor + handle.direction() * (ArrowStyle{}.totalPx() * 0.8 * px));
    Vec2 up = h.screen(anchor + handle.direction()) - h.screen(anchor);
    up = up * (1.0 / up.length());
    h.controller.pointerPress(AsyncHarness::at(grab));
    const std::uint64_t kernelCalls = geom::kernelCallsOnThisThread();
    double longest = 0;
    for (int i = 1; i <= 6; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        h.controller.pointerMove(AsyncHarness::at(grab + up * (12.0 * i)));
        h.uiReads();
        longest = std::max(longest, msSince(t0));
    }
    EXPECT_LT(longest, 100.0) << "a drag step waited for the (300 ms) kernel";
    EXPECT_EQ(geom::kernelCallsOnThisThread(), kernelCalls) << "the GUI thread made kernel calls while dragging";
    EXPECT_GT(op->value(), 20.0);
    EXPECT_TRUE(op->previewPending());
    EXPECT_TRUE(op->canCommit()) << "a pending preview counts as committable";
    EXPECT_TRUE(h.controller.previewBusy());
    h.controller.pointerRelease(AsyncHarness::at(grab + up * 72.0));

    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_FALSE(op->previewPending());
    ASSERT_TRUE(op->hasPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), op->value(), 1e-4);
    EXPECT_EQ(op->previewMeshBody(), h.body().id());
    EXPECT_NEAR(h.height(), 20.0, 1e-9) << "previews never touch the document";
    const RenderScene scene = h.controller.renderScene();
    ASSERT_EQ(scene.bodies.size(), 1u);
    EXPECT_TRUE(scene.bodies[0].isPreview);
    EXPECT_GE(h.controller.previewsShown(), 1u);
}

// Values typed faster than the kernel: the one computing finishes, the one
// waiting is replaced by the newest; the newest is what stays shown.
TEST(AsyncPreview, OnlyTheLatestValueStaysShown)
{
    AsyncHarness h;
    const PushPullOperation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    h.worker().setJobDelayForTesting(100ms);
    EXPECT_EQ(h.controller.setValueText("25"), "");
    EXPECT_EQ(h.controller.setValueText("30"), "");
    EXPECT_EQ(h.controller.setValueText("35"), "");
    EXPECT_DOUBLE_EQ(op->value(), 35.0);
    EXPECT_TRUE(op->previewPending());
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_GE(h.worker().jobsReplaced(), 1u);
    EXPECT_LE(h.worker().jobsRun(), 2u);
    ASSERT_TRUE(op->hasPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 35.0, 1e-4);
    EXPECT_TRUE(op->error().empty());
    EXPECT_GE(h.notified.load(), 1);
}

// A result computed for a state that is gone is not shown: the value was
// set back (Esc), the operation was replaced, or the document changed (undo).
TEST(AsyncPreview, StaleResultsAreDropped)
{
    AsyncHarness h;
    const PushPullOperation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    h.worker().setJobDelayForTesting(150ms);

    // Esc sets the value back while its preview computes.
    EXPECT_EQ(h.controller.setValueText("30"), "");
    ASSERT_TRUE(op->previewPending());
    h.untilRunning();
    EXPECT_TRUE(h.controller.keyPress(Key::Escape));
    ASSERT_EQ(h.controller.operation(), op);
    EXPECT_DOUBLE_EQ(op->value(), 20.0);
    EXPECT_FALSE(op->previewPending());
    const std::uint64_t dropped = h.controller.previewsDropped();
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_FALSE(op->hasPreview()) << "the preview of the value Esc left was shown";
    EXPECT_EQ(h.controller.previewsDropped(), dropped + 1);
    EXPECT_FALSE(h.controller.renderScene().bodies[0].isPreview);

    // The operation is replaced (Esc again clears the selection) while a
    // preview computes.
    EXPECT_EQ(h.controller.setValueText("30"), "");
    h.untilRunning();
    EXPECT_TRUE(h.controller.keyPress(Key::Escape));
    EXPECT_TRUE(h.controller.keyPress(Key::Escape));
    EXPECT_EQ(h.controller.operation(), nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_FALSE(h.controller.renderScene().bodies[0].isPreview);
    EXPECT_NEAR(h.height(), 20.0, 1e-9);
}

// A refused value comes back like a preview: its error, no mesh, and
// nothing to commit. Typing the same value again (Enter) keeps the verdict.
TEST(AsyncPreview, ErrorsComeBackFromTheWorker)
{
    AsyncHarness h;
    h.clickAt(h.screen({10, -10, 10})); // the front right vertical edge
    const Operation* op = h.controller.operation();
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->title(), "Fillet");
    EXPECT_EQ(h.controller.setValueText("50"), "") << "no verdict before the worker has one";
    EXPECT_TRUE(op->previewPending());
    EXPECT_TRUE(op->error().empty());
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_FALSE(op->previewPending());
    EXPECT_FALSE(op->error().empty());
    EXPECT_NE(op->error().find("too large"), std::string::npos) << op->error();
    EXPECT_FALSE(op->hasPreview());
    EXPECT_FALSE(op->canCommit());
    EXPECT_EQ(h.controller.setValueText("50"), op->error());
    EXPECT_FALSE(h.controller.previewBusy()) << "the same value again started another preview";
    // A value that works clears it.
    EXPECT_EQ(h.controller.setValueText("2"), "");
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_TRUE(op->error().empty());
    EXPECT_TRUE(op->hasPreview());
    EXPECT_TRUE(op->canCommit());
}

// Enter while the preview still computes: the command computes the step
// itself, so the commit does not wait for the preview, whose result is then
// dropped.
TEST(AsyncPreview, CommitWhileThePreviewComputes)
{
    AsyncHarness h;
    ASSERT_NE(h.selectTop(), nullptr);
    h.worker().setJobDelayForTesting(400ms);
    EXPECT_EQ(h.controller.setValueText("30"), "");
    ASSERT_TRUE(h.controller.operation()->previewPending());
    h.untilRunning();
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_LT(msSince(t0), 300.0) << "the commit waited for the preview";
    EXPECT_NEAR(h.height(), 30.0, 1e-6);
    EXPECT_EQ(h.stack.undoLabel(), "Push/Pull");
    ASSERT_TRUE(h.controller.waitForPreview());
    // The face stays selected: a new push/pull without a preview.
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_FALSE(h.controller.operation()->hasPreview());
    EXPECT_GE(h.controller.previewsDropped(), 1u);
    EXPECT_NEAR(h.height(), 30.0, 1e-6);
}

// Undo while a preview computes: the document goes back at once, the
// preview (of an operation on the undone state) is dropped.
TEST(AsyncPreview, UndoWhileThePreviewComputes)
{
    AsyncHarness h;
    ASSERT_NE(h.selectTop(), nullptr);
    EXPECT_EQ(h.controller.setValueText("30"), "");
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_NEAR(h.height(), 30.0, 1e-6);
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_NE(h.controller.operation(), nullptr);
    h.worker().setJobDelayForTesting(200ms);
    EXPECT_EQ(h.controller.setValueText("45"), "");
    ASSERT_TRUE(h.controller.operation()->previewPending());
    h.untilRunning();
    const std::uint64_t dropped = h.controller.previewsDropped();
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.height(), 20.0, 1e-6);
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_EQ(h.controller.previewsDropped(), dropped + 1);
    EXPECT_NEAR(h.height(), 20.0, 1e-6);
    for (const auto& body : h.controller.renderScene().bodies)
        EXPECT_FALSE(body.isPreview);
    ASSERT_TRUE(h.controller.redo());
    EXPECT_NEAR(h.height(), 30.0, 1e-6);
}

// Operations that preview as they are created (Pattern) do so on the
// worker too, and asynchronous previews match synchronous ones.
TEST(AsyncPreview, SameResultsAsSynchronousPreviews)
{
    AsyncHarness h;
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern->previewPending()) << "Pattern's first preview was computed on the GUI thread";
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(pattern->hasPreview());
    // Three copies 25 mm apart (the body's 20 mm plus 5).
    EXPECT_NEAR(meshWidth(*pattern->previewMesh()), 70.0, 1e-4);
    const std::size_t asyncTriangles = pattern->previewMesh()->triangleCount();

    // The same pattern computed synchronously.
    h.controller.disableAsyncPreviews();
    EXPECT_FALSE(h.controller.asyncPreviews());
    h.controller.cancelOperation();
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    const auto* syncPattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(syncPattern, nullptr);
    EXPECT_FALSE(syncPattern->previewPending());
    ASSERT_TRUE(syncPattern->hasPreview());
    EXPECT_NEAR(meshWidth(*syncPattern->previewMesh()), 70.0, 1e-4);
    EXPECT_EQ(syncPattern->previewMesh()->triangleCount(), asyncTriangles);
}

// An extrusion's automatic choice depends on its preview (a join that misses
// the body becomes a new body): committing while that preview computes
// waits for it, so the step is what the preview would have shown.
TEST(AsyncPreview, CommitWaitsWhenAnAutomaticChoiceDependsOnThePreview)
{
    AsyncHarness h;
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    SketchSession& session = *h.controller.sketchSession();
    auto sketchScreen = [&](Vec2 local) { return h.screen(session.sketch().plane().toWorld(local)); };
    // A 10 x 10 rectangle beside the box, on its top face's plane.
    h.controller.setSketchTool(SketchTool::Rectangle);
    h.clickAt(sketchScreen({30, 30}));
    h.controller.pointerMove(AsyncHarness::at(sketchScreen({36, 36}), PointerButton::None));
    EXPECT_EQ(session.typeIntoInput("10"), "");
    session.focusNextInput();
    EXPECT_EQ(session.typeIntoInput("10"), "");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    const Uuid sketchId = session.sketchId();
    h.controller.finishSketch();

    h.clickAt(h.screen(h.document.sketch(sketchId)->plane().toWorld({35, 35})));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(h.controller.operation());
    ASSERT_NE(extrude, nullptr);
    h.worker().setJobDelayForTesting(150ms);
    EXPECT_EQ(h.controller.setValueText("5"), "");
    ASSERT_TRUE(extrude->previewPending());
    EXPECT_TRUE(extrude->commitNeedsPreview());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u) << "the automatic new body was not waited for";
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 8000.0, 1e-3);
    EXPECT_NEAR(geom::volume(h.document.bodies()[1]->shape()), 500.0, 1e-3);
}

// While the newest value computes, an older one that finished is shown if
// it worked (the preview keeps up with a drag), but the failure of a value
// the user has left is not.
TEST(AsyncPreview, OlderResultsKeepUpButTheirErrorsDoNot)
{
    AsyncHarness h;
    const PushPullOperation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    h.worker().setJobDelayForTesting(100ms);
    EXPECT_EQ(h.controller.setValueText("25"), "");
    h.untilRunning();
    EXPECT_EQ(h.controller.setValueText("35"), "");
    // The 25 mm preview finishes first.
    const auto t0 = std::chrono::steady_clock::now();
    while (!h.controller.deliverPreviews() && msSince(t0) < 5000)
        std::this_thread::yield();
    EXPECT_TRUE(op->previewPending()) << "35 still computes";
    ASSERT_TRUE(op->hasPreview());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 25.0, 1e-4) << "the finished older value is shown meanwhile";
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_FALSE(op->previewPending());
    EXPECT_NEAR(meshHeight(*op->previewMesh()), 35.0, 1e-4);

    // A fillet too large for the edge fails on the worker while a size that
    // works is already requested: that error never shows.
    h.controller.cancelOperation();
    h.controller.cancelOperation();
    h.clickAt(h.screen({10, -10, 10}));
    const Operation* fillet = h.controller.operation();
    ASSERT_NE(fillet, nullptr);
    ASSERT_EQ(fillet->title(), "Fillet");
    const std::uint64_t dropped = h.controller.previewsDropped();
    EXPECT_EQ(h.controller.setValueText("50"), "");
    h.untilRunning();
    EXPECT_EQ(h.controller.setValueText("2"), "");
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_TRUE(fillet->error().empty()) << "the error of the value the user left showed: " << fillet->error();
    EXPECT_TRUE(fillet->hasPreview());
    EXPECT_EQ(h.controller.previewsDropped(), dropped + 1);
}

// Enter on a value whose preview has not started yet (queued behind an older
// value's): the commit drops that preview and the command refuses the value.
// The refusal is then the verdict, as a refused preview's would be: shown,
// nothing left pending, and the older value's preview that arrives later is
// not shown in its place.
TEST(AsyncPreview, ValueRefusedByTheCommandBeforeItsPreviewCame)
{
    AsyncHarness h;
    h.clickAt(h.screen({10, -10, 10})); // the front right vertical edge
    const Operation* op = h.controller.operation();
    ASSERT_NE(op, nullptr);
    ASSERT_EQ(op->title(), "Fillet");
    h.worker().setJobDelayForTesting(300ms);
    EXPECT_EQ(h.controller.setValueText("1"), "");
    h.untilRunning();
    EXPECT_EQ(h.controller.setValueText("50"), ""); // waits behind 1
    ASSERT_TRUE(op->previewPending());
    EXPECT_TRUE(op->canCommit()) << "a pending preview counts as committable";
    const std::size_t messages = h.messages.size();
    const std::string lastStep = h.stack.undoLabel();
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.controller.operation(), op) << "the refused fillet stays active";
    EXPECT_EQ(h.stack.undoLabel(), lastStep) << "the refused step was added";
    EXPECT_GT(h.messages.size(), messages) << "the refusal was not reported";
    EXPECT_FALSE(op->previewPending()) << "the preview the commit dropped is still awaited";
    EXPECT_FALSE(op->error().empty());
    EXPECT_FALSE(op->hasPreview());
    EXPECT_FALSE(op->canCommit());

    // The 1 mm preview arrives: not shown for the refused 50 mm, error kept.
    const std::uint64_t dropped = h.controller.previewsDropped();
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_EQ(h.controller.previewsDropped(), dropped + 1);
    EXPECT_FALSE(op->previewPending());
    EXPECT_FALSE(op->hasPreview()) << "another value's preview is shown";
    EXPECT_FALSE(op->error().empty());
    EXPECT_FALSE(op->canCommit());
    // Enter again: the same verdict, without a new preview or command.
    EXPECT_EQ(h.controller.setValueText("50"), op->error());
    EXPECT_FALSE(h.controller.previewBusy());
    EXPECT_FALSE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.stack.undoLabel(), lastStep);
    // A size that works clears it and applies.
    EXPECT_EQ(h.controller.setValueText("2"), "");
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_TRUE(op->error().empty());
    EXPECT_TRUE(op->hasPreview());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.stack.undoLabel(), "Fillet");
}

// A change of anything but the value (here the pattern's count) while an
// older request computes: that result is the old layout, and it is dropped;
// the preview shown before the change stays until the new one arrives.
TEST(AsyncPreview, ParameterChangeDropsEarlierResults)
{
    AsyncHarness h;
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(pattern->hasPreview());
    EXPECT_NEAR(meshWidth(*pattern->previewMesh()), 70.0, 1e-4); // 3 copies 25 mm apart

    h.worker().setJobDelayForTesting(150ms);
    EXPECT_EQ(h.controller.setValueText("30"), ""); // 3 copies 30 mm apart: 80 mm
    h.untilRunning();
    const std::uint64_t jobsBefore = h.worker().jobsRun();
    ASSERT_TRUE(h.controller.triggerAction("more").ok()); // 4 copies
    EXPECT_EQ(pattern->count(), 4);
    ASSERT_TRUE(pattern->previewPending());
    // The 3-copy result comes back while the 4-copy one computes.
    const auto t0 = std::chrono::steady_clock::now();
    while (h.worker().jobsRun() == jobsBefore && msSince(t0) < 5000)
        std::this_thread::yield();
    const std::uint64_t dropped = h.controller.previewsDropped();
    EXPECT_FALSE(h.controller.deliverPreviews()) << "the result for the old count was shown";
    EXPECT_EQ(h.controller.previewsDropped(), dropped + 1);
    EXPECT_TRUE(pattern->previewPending());
    ASSERT_TRUE(pattern->hasPreview());
    EXPECT_NEAR(meshWidth(*pattern->previewMesh()), 70.0, 1e-4) << "the preview shown before the change went away";
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(pattern->hasPreview());
    EXPECT_NEAR(meshWidth(*pattern->previewMesh()), 110.0, 1e-4); // 4 copies 30 mm apart
    EXPECT_TRUE(pattern->error().empty());

    // Value changes still keep up: an older value's finished result shows
    // while the newest computes.
    EXPECT_EQ(h.controller.setValueText("40"), "");
    h.untilRunning();
    EXPECT_EQ(h.controller.setValueText("50"), "");
    const auto t1 = std::chrono::steady_clock::now();
    while (!h.controller.deliverPreviews() && msSince(t1) < 5000)
        std::this_thread::yield();
    EXPECT_TRUE(pattern->previewPending());
    EXPECT_NEAR(meshWidth(*pattern->previewMesh()), 140.0, 1e-4); // 4 copies 40 mm apart
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_NEAR(meshWidth(*pattern->previewMesh()), 170.0, 1e-4);
}

// Import and Duplicate apply a pending value first, as a click elsewhere
// does: a value the command refuses while its preview is still computing is
// dropped (it would have shown as refused and never applied), and the action
// goes on - as it does with synchronous previews.
TEST(AsyncPreview, ActionsGoOnWhenThePendingValueIsRefused)
{
    AsyncHarness h;
    const auto fillet = [&h] {
        h.clickAt(h.screen({10, -10, 10})); // the front right vertical edge
        const Operation* op = h.controller.operation();
        EXPECT_NE(op, nullptr);
        h.worker().setJobDelayForTesting(300ms);
        EXPECT_EQ(h.controller.setValueText("1"), "");
        h.untilRunning();
        EXPECT_EQ(h.controller.setValueText("50"), ""); // too large; waits behind 1
        EXPECT_TRUE(op && op->previewPending() && op->canCommit());
    };
    fillet();
    ASSERT_TRUE(h.controller.duplicateBody(h.body().id()).ok());
    EXPECT_EQ(h.document.bodies().size(), 2u);
    EXPECT_EQ(h.stack.undoLabel(), "Duplicate");
    EXPECT_NEAR(geom::volume(h.body().shape()), 8000.0, 1e-6) << "the refused fillet changed the box";
    ASSERT_TRUE(h.controller.waitForPreview());

    h.controller.cancelOperation();
    h.controller.cancelOperation();
    fillet();
    const auto box = geom::makeBox({40, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(box.ok());
    ASSERT_TRUE(h.controller.importBodies({{"Part", box.value()}}, "part.step").ok());
    EXPECT_EQ(h.document.bodies().size(), 3u);
    EXPECT_NEAR(geom::volume(h.body().shape()), 8000.0, 1e-6);
    ASSERT_TRUE(h.controller.waitForPreview());
}

// Apply refused while the preview is still pending (Mirror does not wait for
// its preview; the command finds that the box lands on itself): the tool
// stays Mirror, so the next rebuild (the body picked again) opens Mirror, not
// Move - as with synchronous previews, where the refused preview keeps Apply
// off. An accepted mirror still ends the tool (one-shot).
TEST(AsyncPreview, RefusedApplyKeepsTheBodyTool)
{
    AsyncHarness h; // a 20 mm box, (-10,-10,0) .. (10,10,20): symmetric about YZ
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    const auto* mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    h.worker().setJobDelayForTesting(300ms);
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok()); // across YZ
    // Joined, chosen by hand (Separate bodies on, then off): Apply does not
    // wait for the preview, since no automatic choice depends on it.
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    ASSERT_TRUE(h.controller.triggerAction("separate").ok());
    ASSERT_EQ(h.controller.operation(), mirror);
    ASSERT_FALSE(mirror->separate());
    ASSERT_TRUE(mirror->previewPending());
    ASSERT_TRUE(mirror->canCommit()) << "a pending preview counts as committable";
    ASSERT_FALSE(mirror->commitNeedsPreview());
    const std::string lastStep = h.stack.undoLabel();
    EXPECT_FALSE(h.controller.triggerAction("apply").ok());
    EXPECT_EQ(h.stack.undoLabel(), lastStep) << "the mirror that changes nothing was applied";
    ASSERT_EQ(h.controller.operation(), mirror) << "the refused mirror ended";
    EXPECT_FALSE(mirror->error().empty());
    ASSERT_TRUE(h.controller.waitForPreview());
    h.worker().setJobDelayForTesting(0ms);

    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr) << "the refused apply switched the tool to "
                               << (h.controller.operation() ? h.controller.operation()->title() : std::string("nothing"));

    ASSERT_TRUE(h.controller.triggerAction("plane:2").ok()); // across XY: doubles the box downwards
    EXPECT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_EQ(h.stack.undoLabel(), "Mirror");
    EXPECT_NEAR(geom::volume(h.body().shape()), 16000.0, 1e-6);
    EXPECT_NE(dynamic_cast<const MoveOperation*>(h.controller.operation()), nullptr) << "Mirror stayed after it was applied";
    ASSERT_TRUE(h.controller.waitForPreview());
}

// Tab to the next field confirms the typed value (confirmValueText): it waits
// for the verdict of a preview still computing, so a hole typed off the face
// keeps the X field with the message, as with synchronous previews. A
// keystroke (setValueText) never waits.
TEST(AsyncPreview, ConfirmingAValueWaitsForItsVerdict)
{
    AsyncHarness h; // a 20 mm box, (-10,-10,0) .. (10,10,20)
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_TRUE(h.controller.triggerAction("hole").ok());
    const auto* hole = dynamic_cast<const HoleOperation*>(h.controller.operation());
    ASSERT_NE(hole, nullptr);
    h.clickAt(h.screen({0.3, 0.2, 20})); // snaps to the face's center
    ASSERT_EQ(hole->positions().size(), 1u);
    ASSERT_TRUE(h.controller.triggerAction("field:x").ok());
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_EQ(hole->field(), HoleOperation::Field::X);

    h.worker().setJobDelayForTesting(200ms);
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(h.controller.setValueText("40"), "");
    EXPECT_LT(msSince(t0), 150.0) << "a keystroke waited for the preview";
    ASSERT_TRUE(hole->previewPending());
    const std::string error = h.controller.confirmValueText("40");
    EXPECT_NE(error.find("off the face"), std::string::npos) << error;
    EXPECT_FALSE(hole->previewPending());
    EXPECT_EQ(hole->error(), error);
    EXPECT_EQ(hole->field(), HoleOperation::Field::X);

    EXPECT_EQ(h.controller.confirmValueText("12"), "");
    EXPECT_FALSE(hole->previewPending());
    EXPECT_TRUE(hole->hasPreview());
    ASSERT_TRUE(h.controller.triggerAction("nextField").ok());
    EXPECT_EQ(hole->field(), HoleOperation::Field::Y);
    ASSERT_TRUE(h.controller.waitForPreview());
}

// The Hole tool previews on the worker. Its Apply waits for a pending
// preview, which says which hole is off the face (the step's own refusal is
// worded for upstream changes). A counterbore on the hole's rim previews on
// the worker from the start.
TEST(AsyncPreview, HoleToolAndCounterborePreviewOnTheWorker)
{
    AsyncHarness h; // a 20 mm box, (-10,-10,0) .. (10,10,20)
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_TRUE(h.controller.triggerAction("hole").ok());
    const auto* hole = dynamic_cast<const HoleOperation*>(h.controller.operation());
    ASSERT_NE(hole, nullptr);
    h.clickAt(h.screen({0.3, 0.2, 20})); // snaps to the face's center
    ASSERT_EQ(hole->positions().size(), 1u);
    EXPECT_TRUE(hole->previewPending()) << "the hole was previewed on the GUI thread";
    EXPECT_TRUE(hole->canCommit()) << "a pending preview counts as committable";
    // Hovering on while it computes: where the next hole would snap to is
    // found without the kernel (which the worker may hold).
    const std::uint64_t kernelCalls = geom::kernelCallsOnThisThread();
    for (int i = 0; i < 5; ++i)
        h.controller.pointerMove(AsyncHarness::at(h.screen({-6.0 + 3 * i, 4, 20}), PointerButton::None));
    EXPECT_EQ(geom::kernelCallsOnThisThread(), kernelCalls) << "hovering in the Hole tool made kernel calls";
    EXPECT_TRUE(hole->hover().has_value());
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(hole->hasPreview());
    EXPECT_TRUE(hole->error().empty()) << hole->error();
    const double d = hole->diameter();
    const double drilled = 8000.0 - kPi * d * d / 4 * 20; // through all
    EXPECT_NEAR(meshVolume(*hole->previewMesh()), drilled, 2.0);

    // X typed off the face; Enter while its preview computes waits for it.
    ASSERT_TRUE(h.controller.triggerAction("field:x").ok());
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_EQ(hole->field(), HoleOperation::Field::X);
    EXPECT_NEAR(hole->value(), 10.0, 1e-9) << "the center, 10 mm from the corner";
    h.worker().setJobDelayForTesting(150ms);
    EXPECT_EQ(h.controller.setValueText("40"), "");
    ASSERT_TRUE(hole->previewPending());
    EXPECT_TRUE(hole->commitNeedsPreview());
    const std::string lastStep = h.stack.undoLabel();
    (void)h.controller.keyPress(Key::Enter);
    ASSERT_EQ(h.controller.operation(), hole) << "the tool ended";
    EXPECT_EQ(h.stack.undoLabel(), lastStep) << "a hole off the face was applied";
    EXPECT_FALSE(hole->previewPending()) << "Apply did not wait for the preview";
    EXPECT_NE(hole->error().find("off the face"), std::string::npos) << hole->error();
    EXPECT_FALSE(hole->canCommit());

    // Back to the center, applied while its preview computes.
    EXPECT_EQ(h.controller.setValueText("10"), "");
    ASSERT_TRUE(hole->previewPending());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.stack.undoLabel(), "Hole");
    EXPECT_NEAR(geom::volume(h.body().shape()), drilled, 1e-3);
    ASSERT_TRUE(h.controller.waitForPreview());
    h.worker().setJobDelayForTesting(0ms);

    // A counterbore on the hole's top rim.
    h.clickAt(h.screen({d / 2, 0, 20}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Edge);
    ASSERT_TRUE(h.controller.waitForPreview()); // the fillet's
    ASSERT_TRUE(h.controller.triggerAction("counterbore").ok());
    const auto* head = dynamic_cast<const HeadOperation*>(h.controller.operation());
    ASSERT_NE(head, nullptr);
    EXPECT_TRUE(head->previewPending()) << "the preset's preview was computed on the GUI thread";
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(head->hasPreview());
    EXPECT_TRUE(head->error().empty()) << head->error();
    const doc::ScrewSize& m4 = doc::metricScrews()[3];
    ASSERT_TRUE(h.controller.triggerAction("preset:3").ok());
    EXPECT_TRUE(head->previewPending());
    geom::HoleCut cut;
    cut.diameter = d;
    cut.drillShaft = false;
    cut.throughAll = true;
    cut.head = geom::HoleHead::Counterbore;
    // The preset's seat plus the print allowance (the default: 0.2 mm).
    cut.headDiameter = doc::counterboreDiameterFor(m4, doc::kDefaultHoleAllowance);
    cut.headDepth = m4.counterboreDepth;
    const double counterbored = drilled - geom::headVolume(cut);
    ASSERT_TRUE(h.controller.waitForPreview());
    EXPECT_NEAR(meshVolume(*head->previewMesh()), counterbored, 3.0);
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    const auto rows = h.controller.historyRows();
    EXPECT_TRUE(std::any_of(rows.begin(), rows.end(), [](const HistoryRow& r) { return r.name == "Counterbore"; }))
        << "no Counterbore step";
    EXPECT_NEAR(geom::volume(h.body().shape()), counterbored, 1e-3);
}

// Extrude with a draft on the worker: the copy computes the draft the chip
// edits, and Enter while the preview computes applies the typed angle.
TEST(AsyncPreview, ExtrudeDraftPreviewsOnTheWorker)
{
    AsyncHarness h;
    ASSERT_TRUE(h.controller.startSketch().ok()); // nothing selected: the XY plane
    h.controller.skipAnimation();
    SketchSession& session = *h.controller.sketchSession();
    auto sketchScreen = [&](Vec2 local) { return h.screen(session.sketch().plane().toWorld(local)); };
    h.controller.setSketchTool(SketchTool::Rectangle);
    h.clickAt(sketchScreen({30, 30}));
    h.controller.pointerMove(AsyncHarness::at(sketchScreen({36, 36}), PointerButton::None));
    EXPECT_EQ(session.typeIntoInput("10"), "");
    session.focusNextInput();
    EXPECT_EQ(session.typeIntoInput("10"), "");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    const Uuid sketchId = session.sketchId();
    h.controller.finishSketch();
    const sketch::Plane plane = h.document.sketch(sketchId)->plane();

    h.clickAt(h.screen(plane.toWorld({35, 35})));
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(h.controller.operation());
    ASSERT_NE(extrude, nullptr);
    EXPECT_EQ(h.controller.setValueText("10"), "");
    ASSERT_TRUE(h.controller.triggerAction("draft").ok());
    ASSERT_TRUE(extrude->editingDraft());
    auto frustum = [](double degrees) {
        const double t = 10 * std::tan(degrees * kPi / 180), top = (10 - 2 * t) * (10 - 2 * t);
        return 10.0 / 3 * (100 + top + std::sqrt(100 * top));
    };
    EXPECT_EQ(h.controller.setValueText("5"), "");
    EXPECT_TRUE(extrude->previewPending());
    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(extrude->hasPreview());
    EXPECT_NEAR(meshVolume(*extrude->previewMesh()), frustum(5), 0.05);

    h.worker().setJobDelayForTesting(200ms);
    EXPECT_EQ(h.controller.setValueText("8"), "");
    ASSERT_TRUE(extrude->previewPending());
    EXPECT_FALSE(extrude->commitNeedsPreview()) << "no body under the sketch: no automatic choice";
    EXPECT_TRUE(extrude->canCommit()) << "a pending preview counts as committable";
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[1]->shape()), frustum(8), 1e-3);
    ASSERT_TRUE(h.controller.waitForPreview());
}

// Hovering sketch profiles while a preview computes: the pick tests the
// profile's mesh, so the GUI thread makes no kernel call (it would wait for
// the worker's).
TEST(AsyncPreview, HoveringProfilesMakesNoKernelCall)
{
    AsyncHarness h;
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Top).ok());
    h.controller.skipAnimation();
    SketchSession& session = *h.controller.sketchSession();
    auto sketchScreen = [&](Vec2 local) { return h.screen(session.sketch().plane().toWorld(local)); };
    h.controller.setSketchTool(SketchTool::Circle);
    h.clickAt(sketchScreen({30, 0}));
    h.controller.pointerMove(AsyncHarness::at(sketchScreen({33, 0}), PointerButton::None));
    EXPECT_EQ(session.typeIntoInput("8"), ""); // diameter
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.finishSketch();
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);

    ASSERT_NE(h.selectTop(), nullptr);
    h.worker().setJobDelayForTesting(200ms);
    EXPECT_EQ(h.controller.setValueText("30"), "");
    ASSERT_TRUE(h.controller.operation()->previewPending());
    const std::uint64_t kernelCalls = geom::kernelCallsOnThisThread();
    const auto inside = h.controller.pickAt(h.screen({32.5, 0, 0}), InputProfile{});
    const auto rim = h.controller.pickAt(h.screen({33.8, 0, 0}), InputProfile{});
    const auto outside = h.controller.pickAt(h.screen({34.5, 0, 0}), InputProfile{});
    h.controller.pointerMove(AsyncHarness::at(h.screen({30, 1, 0}), PointerButton::None));
    EXPECT_EQ(geom::kernelCallsOnThisThread(), kernelCalls) << "hovering a profile called the kernel";
    EXPECT_EQ(inside.kind, sel::PickKind::Profile);
    EXPECT_EQ(rim.kind, sel::PickKind::Profile);
    EXPECT_NE(outside.kind, sel::PickKind::Profile);
    EXPECT_EQ(h.controller.hover().kind, sel::PickKind::Profile);
    ASSERT_TRUE(h.controller.waitForPreview());
}

namespace {

// One side of a random session: the same UI actions with previews on the
// worker or on the calling thread.
struct Session {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};

    explicit Session(bool async)
    {
        controller.setViewportSize({1200, 800});
        if (async)
            controller.enableAsyncPreviews({});
        EXPECT_TRUE(controller.createBox(20).ok());
        controller.fitAll(false);
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    void clickAt(Vec2 p)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    // The first body's box (undo may have removed it: then an empty one at the origin).
    geom::BoundingBox box() const
    {
        return document.bodies().empty() ? geom::BoundingBox{} : geom::boundingBox(document.bodies().front()->shape());
    }
};

// What must be the same on both sides.
std::string describe(const Session& s)
{
    std::string text = std::to_string(s.document.bodies().size()) + " bodies;";
    for (const auto& body : s.document.bodies()) {
        const geom::BoundingBox box = geom::boundingBox(body->shape());
        char line[160];
        std::snprintf(line, sizeof line, " %zu steps, %.6f mm3, z %.6f..%.6f;", body->features().size(),
                      geom::volume(body->shape()), box.min.z, box.max.z);
        text += line;
    }
    text += " undo " + s.stack.undoLabel() + " / redo " + s.stack.redoLabel();
    text += s.controller.operation() ? " / " + s.controller.operation()->title() : std::string(" / no operation");
    return text;
}

} // namespace

// Random sessions through the UI entry points (clicks on the top face and an
// edge, typed values, Enter, Esc, undo, redo, a click elsewhere that applies),
// once with previews on the worker - random kernel delays, results delivered
// at random moments - and once synchronously: the documents and operations
// are the same after every step, and so are the previews once the worker is
// done. (A click after a refused value whose preview had not come back yet
// selects, as with the refusal shown: the command refuses the value first.)
TEST(AsyncPreview, RandomSessionsMatchSynchronousOnes)
{
    // OPENSHAPE_STRESS_SEEDS=N: N other seeds instead (a longer hunt).
    std::vector<std::uint32_t> seeds{11, 12, 13, 14};
    if (const char* count = std::getenv("OPENSHAPE_STRESS_SEEDS")) {
        seeds.clear();
        for (int i = 0; i < std::atoi(count); ++i)
            seeds.push_back(5000u + std::uint32_t(i));
    }
    for (const std::uint32_t seed : seeds) {
        SCOPED_TRACE("seed " + std::to_string(seed));
        Session sync(false), async(true);
        test::PortableRandom rng(seed);
        std::vector<std::string> log;
        for (int step = 0; step < 36; ++step) {
            const int action = rng.integer(0, 7);
            char text[32];
            std::snprintf(text, sizeof text, "%.1f", action == 1 ? rng.uniform(4, 40) : rng.uniform(0.5, 1.5));
            if (rng.unit() < 0.3)
                async.controller.previewWorker()->setJobDelayForTesting(std::chrono::milliseconds(rng.integer(0, 25)));
            log.push_back(std::to_string(action) + (action == 1 || action == 6 ? std::string(" ") + text : std::string()));
            for (Session* s : {&sync, &async}) {
                const geom::BoundingBox box = s->box();
                switch (action) {
                case 0: s->clickAt(s->screen({box.center().x, box.center().y, box.max.z})); break;
                case 1:
                case 6:
                    if (action == 6)
                        s->clickAt(s->screen({box.max.x, box.min.y, box.center().z})); // a vertical edge: a fillet
                    (void)s->controller.setValueText(text);
                    break;
                case 2: (void)s->controller.keyPress(Key::Enter); break;
                case 3: (void)s->controller.keyPress(Key::Escape); break;
                case 4: (void)s->controller.undo(); break;
                case 5: (void)s->controller.redo(); break;
                case 7: s->clickAt({30, 30}); break;
                }
            }
            if (rng.unit() < 0.5)
                (void)async.controller.deliverPreviews();
            else if (rng.unit() < 0.3)
                std::this_thread::sleep_for(std::chrono::milliseconds(rng.integer(1, 20)));
            ASSERT_EQ(describe(async), describe(sync)) << "after steps " << ::testing::PrintToString(log);
        }
        ASSERT_TRUE(async.controller.waitForPreview());
        const Operation* a = async.controller.operation();
        const Operation* b = sync.controller.operation();
        ASSERT_EQ(a != nullptr, b != nullptr);
        if (a && b) {
            EXPECT_EQ(a->error(), b->error());
            EXPECT_EQ(a->hasPreview(), b->hasPreview());
            if (a->hasPreview() && b->hasPreview()) {
                EXPECT_EQ(a->previewMesh()->triangleCount(), b->previewMesh()->triangleCount());
            }
        }
    }
}

// Mirror and Pattern decide automatically whether copies become separate
// bodies from the preview (copies that would not touch the body). With the
// preview on the worker, that choice is made on a clone: it must come back
// with the preview, and an Apply before the preview arrives must wait for it.
TEST(AsyncPreview, PatternChoosesSeparateBodiesOnTheWorker)
{
    AsyncHarness h;
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    // Apply at once: the default spacing leaves gaps, so the copies must come
    // out as bodies of their own even though no preview has arrived yet.
    const int copies = pattern->count() - 1;
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_EQ(h.document.bodies().size(), std::size_t(copies + 1));
    for (const auto& body : h.document.bodies())
        EXPECT_NEAR(geom::volume(body->shape()), 8000.0, 1e-6);
}

TEST(AsyncPreview, PatternSeparateChoiceReachesTheShownOperation)
{
    AsyncHarness h;
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    (void)h.controller.waitForPreview();
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern->separate()) << "the worker's automatic choice is taken with its preview";
    EXPECT_TRUE(pattern->separateIsAutomatic());
}

// The Text tool previews on the worker: typing does not wait for the kernel,
// hovering the face (where a click would move the words) makes no kernel
// call on the GUI thread, the chip's place beside the words (the letters'
// box) comes with the preview, and Enter while a preview computes waits for
// it, so the step is what the preview shows.
TEST(AsyncPreview, TextToolPreviewsOnTheWorker)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    AsyncHarness h; // a 20 mm box, (-10,-10,0) .. (10,10,20)
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    const auto* text = dynamic_cast<const TextOperation*>(h.controller.operation());
    ASSERT_NE(text, nullptr);
    ASSERT_TRUE(h.controller.waitForPreview());
    h.uiReads(); // the selection's facts are computed once, as the UI shows the selection
    h.worker().setJobDelayForTesting(300ms);

    const std::uint64_t kernelCalls = geom::kernelCallsOnThisThread();
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(h.controller.setOperationText("H"), "");
    EXPECT_EQ(h.controller.setOperationText("Hi"), "");
    for (int i = 0; i < 5; ++i)
        h.controller.pointerMove(AsyncHarness::at(h.screen({-6.0 + 3 * i, 4, 20}), PointerButton::None));
    h.uiReads();
    (void)h.controller.valueLabelPosition();
    EXPECT_LT(msSince(t0), 150.0) << "typing or hovering waited for the (300 ms) kernel";
    EXPECT_EQ(geom::kernelCallsOnThisThread(), kernelCalls) << "the GUI thread made kernel calls while typing or hovering";
    EXPECT_TRUE(text->hover().has_value());
    EXPECT_TRUE(text->previewPending()) << "the words were previewed on the GUI thread";
    EXPECT_TRUE(text->canCommit()) << "a pending preview counts as committable";
    EXPECT_TRUE(text->commitNeedsPreview());
    EXPECT_TRUE(text->textCorners().empty()) << "no letters' box before the words' first preview";

    ASSERT_TRUE(h.controller.waitForPreview());
    ASSERT_TRUE(text->hasPreview());
    EXPECT_TRUE(text->error().empty()) << text->error();
    const auto letters = geom::textFaces({"Hi", doc::kTextFontRegular, 10});
    ASSERT_TRUE(letters.ok()) << letters.developerMessage();
    const double area = geom::surfaceArea(letters.value());
    // Raised 1 mm (the mesh's volume: within its chords of the curves).
    EXPECT_NEAR(meshVolume(*text->previewMesh()), 8000.0 + area, 0.01 * area);
    // The letters' box, measured on the worker: as wide and tall as the letters.
    const geom::BoundingBox box = geom::approximateBoundingBox(letters.value());
    const auto corners = text->textCorners();
    ASSERT_EQ(corners.size(), 4u);
    EXPECT_NEAR((corners[1] - corners[0]).length(), box.size().x, 1e-9);
    EXPECT_NEAR((corners[2] - corners[1]).length(), box.size().y, 1e-9);
    EXPECT_NEAR(corners[0].z, 20.0, 1e-9) << "on the top face";

    // The same words again (Enter in the text field sends them once more):
    // the preview shown stays, nothing is computed again, so Apply need not wait.
    const std::uint64_t shown = text->previewKey();
    const std::uint64_t jobs = h.worker().jobsRun();
    EXPECT_EQ(h.controller.setOperationText("Hi"), "");
    EXPECT_FALSE(text->previewPending()) << "the same words were previewed again";
    EXPECT_EQ(text->previewKey(), shown);
    EXPECT_FALSE(h.worker().busy());
    EXPECT_EQ(h.worker().jobsRun(), jobs);
    EXPECT_TRUE(text->canCommit());

    // Enter while the next words' preview computes: it waits, and applies them.
    EXPECT_EQ(h.controller.setOperationText("Hi!"), "");
    ASSERT_TRUE(text->previewPending());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_EQ(h.stack.undoLabel(), "Text");
    const auto more = geom::textFaces({"Hi!", doc::kTextFontRegular, 10});
    ASSERT_TRUE(more.ok());
    const double moreArea = geom::surfaceArea(more.value());
    EXPECT_NEAR(geom::volume(h.body().shape()), 8000.0 + moreArea, 1e-6 * moreArea);
    h.worker().setJobDelayForTesting(0ms);
}
