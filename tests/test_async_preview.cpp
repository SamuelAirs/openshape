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
#include "geometry/KernelSignals.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/PreviewWorker.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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
