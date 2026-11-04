// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Measure's design hooks (docs/MEASURE.md) through the interaction layer:
// the 1:1 view, the sketch's Trace tool, and geometry made from
// measurements (an outline or a circle sketch, a sized box).
#include "commands/Command.h"
#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "measure/OutlineProfile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace os;
using namespace os::interact;

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;

    Harness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
        controller.fitAll(false);
    }

    SketchSession& session() { return *controller.sketchSession(); }
    const sketch::Sketch& sketch() { return controller.sketchSession()->sketch(); }

    void startSketch()
    {
        ASSERT_TRUE(controller.startSketch().ok());
        controller.skipAnimation();
    }

    Vec2 sketchScreen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }

    static PointerEvent at(Vec2 p, PointerDevice device = PointerDevice::Mouse)
    {
        PointerEvent e;
        e.device = device;
        e.button = PointerButton::Left;
        e.position = p;
        return e;
    }

    // A stroke through sketch points (screen positions taken now), with a
    // little hand tremor (+-0.4 px, deterministic).
    void stroke(const std::vector<Vec2>& local, PointerDevice device = PointerDevice::Mouse)
    {
        std::vector<Vec2> screen;
        for (std::size_t i = 0; i < local.size(); ++i) {
            const double k = double(i);
            screen.push_back(sketchScreen(local[i]) + Vec2{0.4 * std::sin(k * 1.7), 0.4 * std::cos(k * 2.3)});
        }
        controller.pointerPress(at(screen.front(), device));
        for (std::size_t i = 1; i < screen.size(); ++i)
            controller.pointerMove(at(screen[i], device));
        controller.pointerRelease(at(screen.back(), device));
    }

    std::size_t constraints(sketch::ConstraintKind kind)
    {
        std::size_t n = 0;
        for (const auto& [id, c] : sketch().constraints())
            n += c.kind == kind ? 1 : 0;
        return n;
    }
};

// Points every `step` mm along a polyline (both ends included).
std::vector<Vec2> along(const std::vector<Vec2>& corners, double step)
{
    std::vector<Vec2> out{corners.front()};
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const Vec2 a = corners[i - 1], b = corners[i];
        const int n = std::max(1, int(std::ceil((b - a).length() / step)));
        for (int k = 1; k <= n; ++k)
            out.push_back(a + (b - a) * (double(k) / n));
    }
    return out;
}

bool hasPointNear(const sketch::Sketch& s, Vec2 p, double tolerance)
{
    for (const auto& [id, q] : s.points())
        if (id != sketch::kOriginId && (q.position - p).length() <= tolerance)
            return true;
    return false;
}

} // namespace

// ---- 1:1 -------------------------------------------------------------------------------

TEST(ActualSize, OneMillimeterIsOneMillimeterOnTheScreen)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.skipAnimation();
    const double ppm = 160.0 / 25.4; // a phone's 160 points per inch
    h.controller.setScreenScale(ppm);
    const Camera before = h.controller.camera();
    ASSERT_EQ(before.projection, Camera::Projection::Perspective);
    h.controller.setActualSize(true);
    const Camera& c = h.controller.camera();
    EXPECT_TRUE(h.controller.actualSize());
    EXPECT_EQ(c.projection, Camera::Projection::Orthographic);
    EXPECT_NEAR(c.pixelSize(c.target), 1.0 / ppm, 1e-12);
    EXPECT_NEAR(c.orthoHeight, 800 / ppm, 1e-9);
    // The same direction and center.
    EXPECT_NEAR((c.backward() - before.backward()).length(), 0, 1e-12);
    EXPECT_NEAR((c.target - before.target).length(), 0, 1e-12);
    EXPECT_NEAR((c.project(before.target) - Vec2{600, 400}).length(), 0, 1e-9);

    // A new scale or window size keeps it at 1:1.
    h.controller.setScreenScale(5.0);
    EXPECT_NEAR(h.controller.camera().pixelSize(c.target), 0.2, 1e-12);
    h.controller.setViewportSize({500, 900});
    EXPECT_NEAR(h.controller.camera().pixelSize(c.target), 0.2, 1e-12);
    h.controller.setViewportSize({1200, 800});
    // Nonsense scales are ignored.
    h.controller.setScreenScale(0);
    h.controller.setScreenScale(std::nan(""));
    EXPECT_DOUBLE_EQ(h.controller.screenScale(), 5.0);

    // A standard view turns the view without zooming: still 1:1.
    h.controller.setStandardView(StandardView::Top);
    h.controller.skipAnimation();
    EXPECT_TRUE(h.controller.actualSize());
    EXPECT_NEAR(h.controller.camera().pixelSize(c.target), 0.2, 1e-12);

    // Off: the projection and zoom from before.
    h.controller.setActualSize(false);
    EXPECT_FALSE(h.controller.actualSize());
    EXPECT_EQ(h.controller.camera().projection, Camera::Projection::Perspective);
    EXPECT_DOUBLE_EQ(h.controller.camera().distance, before.distance);
}

TEST(ActualSize, AnyZoomLeavesIt)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.skipAnimation();
    h.controller.setScreenScale(4.0);

    // The wheel: perspective comes back at the size shown, then the zoom.
    h.controller.setActualSize(true);
    h.controller.wheel({600, 400}, 1.0);
    EXPECT_FALSE(h.controller.actualSize());
    EXPECT_EQ(h.controller.camera().projection, Camera::Projection::Perspective);

    // A pinch.
    h.controller.setActualSize(true);
    h.controller.pinch({600, 400}, 1.2);
    EXPECT_FALSE(h.controller.actualSize());

    // Fit (animated or not).
    h.controller.setActualSize(true);
    h.controller.fitAll(true);
    h.controller.skipAnimation();
    EXPECT_FALSE(h.controller.actualSize());
    EXPECT_EQ(h.controller.camera().projection, Camera::Projection::Perspective);
    h.controller.setActualSize(true);
    h.controller.fitAll(false);
    EXPECT_FALSE(h.controller.actualSize());

    // Choosing perspective.
    h.controller.setActualSize(true);
    h.controller.setProjection(Camera::Projection::Perspective);
    EXPECT_FALSE(h.controller.actualSize());

    // From orthographic, a zoom stays orthographic.
    h.controller.setProjection(Camera::Projection::Orthographic);
    h.controller.setActualSize(true);
    h.controller.wheel({600, 400}, -1.0);
    EXPECT_FALSE(h.controller.actualSize());
    EXPECT_EQ(h.controller.camera().projection, Camera::Projection::Orthographic);
    EXPECT_GT(h.controller.camera().pixelSize({}), 0.25 + 1e-6); // zoomed out from 1:1
}

TEST(ActualSize, WorksInASketch)
{
    Harness h;
    h.startSketch();
    h.controller.setScreenScale(6.0);
    const Vec3 facing = h.controller.camera().backward();
    h.controller.setActualSize(true);
    EXPECT_NEAR(h.controller.camera().pixelSize({}), 1.0 / 6.0, 1e-12);
    EXPECT_NEAR((h.controller.camera().backward() - facing).length(), 0, 1e-12) << "still face-on";
    // A traced stroke at 1:1 is fitted within 2.5 px = 0.42 mm (never under 0.2 mm).
    EXPECT_NEAR(h.session().traceTolerance(h.controller.camera()), 2.5 / 6.0, 1e-12);
    h.controller.setScreenScale(20.0);
    EXPECT_NEAR(h.session().traceTolerance(h.controller.camera()), 0.2, 1e-12);
    EXPECT_NEAR(h.session().traceCloseGap(h.controller.camera()), 14.0 / 20.0, 1e-12);
    // Finishing the sketch turns the view back to the 3D direction it came
    // from without zooming: still 1:1.
    h.controller.finishSketch();
    h.controller.skipAnimation();
    EXPECT_TRUE(h.controller.actualSize());
    EXPECT_LT(h.controller.camera().backward().z, 0.999);
    EXPECT_NEAR(h.controller.camera().pixelSize({}), 1.0 / 20.0, 1e-12);
}

// ---- Trace -----------------------------------------------------------------------------

TEST(Trace, ARectangleStrokeBecomesFourJoinedLines)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    ASSERT_EQ(h.session().tool(), SketchTool::Trace);
    const std::vector<Vec2> corners{{-20, -10}, {20, -10}, {20, 10}, {-20, 10}, {-20, -10}};
    h.stroke(along(corners, 0.4));
    const auto& s = h.sketch();
    ASSERT_EQ(s.lines().size(), 4u) << (h.messages.empty() ? "" : h.messages.back());
    EXPECT_EQ(s.points().size(), 5u) << "four corners, shared by the lines (and the origin)";
    for (const Vec2& c : corners)
        EXPECT_TRUE(hasPointNear(s, c, 0.4)) << c.x << ", " << c.y;
    EXPECT_EQ(h.session().regions().size(), 1u) << "a closed profile, ready to extrude";
    EXPECT_EQ(h.stack.undoLabel(), "Trace");
    EXPECT_FALSE(h.session().isTracing());
}

TEST(Trace, AStrokeBegunAlongASideStillGivesFourLines)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    const std::vector<Vec2> corners{{0, -10}, {20, -10}, {20, 10}, {-20, 10}, {-20, -10}, {-1, -10}};
    h.stroke(along(corners, 0.4));
    EXPECT_EQ(h.sketch().lines().size(), 4u);
    EXPECT_EQ(h.session().regions().size(), 1u);
}

TEST(Trace, ARoundStrokeBecomesACircle)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    std::vector<Vec2> round;
    for (int i = 0; i <= 240; ++i) {
        const double a = 2 * kPi * i / 236.0; // a little past the start, as a hand does
        round.push_back(Vec2{5, 3} + Vec2{std::cos(a), std::sin(a)} * 15.0);
    }
    h.stroke(round);
    const auto& s = h.sketch();
    ASSERT_EQ(s.circles().size(), 1u);
    EXPECT_TRUE(s.lines().empty());
    const auto& circle = s.circles().begin()->second;
    EXPECT_NEAR(circle.radius, 15.0, 0.1);
    EXPECT_NEAR((s.point(circle.center)->position - Vec2{5, 3}).length(), 0, 0.1);
}

TEST(Trace, AnOpenStrokeBecomesAChainOfLines)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    h.stroke(along({{-20, 0}, {0, 0}, {0, 20}}, 0.4));
    const auto& s = h.sketch();
    EXPECT_EQ(s.lines().size(), 2u);
    EXPECT_EQ(s.points().size(), 4u) << "three points and the origin: the two lines share the corner";
    EXPECT_TRUE(hasPointNear(s, {0, 0}, 0.4));
    EXPECT_TRUE(h.session().regions().empty());
}

TEST(Trace, CurvedRunsBecomeArcs)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    // At 1:1 on a 254 dpi screen (10 px/mm): fitted within 0.25 mm.
    h.controller.setScreenScale(10.0);
    h.controller.setActualSize(true);
    // A "D": a straight side, then a half circle back to the start.
    std::vector<Vec2> d = along({{0, -15}, {0, 15}}, 0.2);
    for (int i = 1; i <= 240; ++i) {
        const double a = kPi / 2 - kPi * i / 240.0;
        d.push_back(Vec2{std::cos(a), std::sin(a)} * 15.0);
    }
    h.stroke(d);
    const auto& s = h.sketch();
    EXPECT_EQ(s.lines().size(), 1u);
    // (The fitter may split the half circle into arcs that meet end to end.)
    ASSERT_GE(s.arcs().size(), 1u);
    EXPECT_LE(s.arcs().size(), 2u);
    for (const auto& [id, arc] : s.arcs()) {
        EXPECT_NEAR(s.arcRadius(id), 15.0, 0.3);
        EXPECT_NEAR(s.point(arc.center)->position.length(), 0, 0.3);
    }
    EXPECT_EQ(h.session().regions().size(), 1u) << "the arcs bulge the right way: a closed D";
}

TEST(Trace, UndoRemovesTheWholeTraceAndATapAddsNothing)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    const std::size_t steps = h.stack.index();
    // A click without a stroke.
    const Vec2 p = h.sketchScreen({3, 4});
    h.controller.pointerPress(Harness::at(p));
    h.controller.pointerRelease(Harness::at(p));
    EXPECT_FALSE(h.sketch().hasGeometry());
    EXPECT_EQ(h.stack.index(), steps);

    h.stroke(along({{-20, -10}, {20, -10}, {20, 10}, {-20, 10}, {-20, -10}}, 0.4));
    ASSERT_EQ(h.sketch().lines().size(), 4u);
    EXPECT_EQ(h.stack.index(), steps + 1) << "one step";
    EXPECT_TRUE(h.controller.undo());
    EXPECT_FALSE(h.sketch().hasGeometry());
    EXPECT_EQ(h.session().tool(), SketchTool::Trace);
}

TEST(Trace, InPenModeOnlyThePenTraces)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    h.controller.setPenMode(true);
    const Camera before = h.controller.camera();
    h.stroke(along({{-10, 0}, {10, 0}}, 0.4), PointerDevice::Touch);
    EXPECT_FALSE(h.sketch().hasGeometry()) << "a finger moves the view";
    const Camera& after = h.controller.camera();
    EXPECT_TRUE((after.project({}) - before.project({})).length() > 1.0 || (after.backward() - before.backward()).length() > 1e-6);
    h.controller.faceSketch();
    h.controller.skipAnimation();
    h.stroke(along({{-10, -10}, {10, -10}, {10, 10}, {-10, 10}, {-10, -10}}, 0.4), PointerDevice::Pen);
    EXPECT_EQ(h.sketch().lines().size(), 4u) << "the pen traces";
}

TEST(Trace, ACancelledStrokeAddsNothing)
{
    Harness h;
    h.startSketch();
    h.controller.setSketchTool(SketchTool::Trace);
    const auto pts = along({{-20, 0}, {20, 0}}, 0.5);
    h.controller.pointerPress(Harness::at(h.sketchScreen(pts.front()), PointerDevice::Touch));
    for (std::size_t i = 1; i < pts.size(); ++i)
        h.controller.pointerMove(Harness::at(h.sketchScreen(pts[i]), PointerDevice::Touch));
    EXPECT_TRUE(h.session().isTracing());
    h.controller.cancelPointer(); // a second finger: a pinch
    EXPECT_FALSE(h.session().isTracing());
    EXPECT_FALSE(h.sketch().hasGeometry());
}

// ---- Geometry from measurements ------------------------------------------------------------

TEST(MeasuredGeometry, AnOutlineBecomesAnOpenSketchReadyToExtrude)
{
    Harness h;
    const std::size_t steps = h.stack.index();
    ASSERT_TRUE(h.controller.createOutlineSketch({{-30, -20}, {30, -20}, {30, 20}, {-10, 25}}, true).ok());
    EXPECT_EQ(h.stack.index(), steps + 1);
    ASSERT_EQ(h.controller.mode(), InteractionController::Mode::Sketch);
    h.controller.skipAnimation();
    const auto& s = h.sketch();
    EXPECT_EQ(s.lines().size(), 4u);
    EXPECT_EQ(s.points().size(), 5u);
    EXPECT_NEAR(s.plane().normal().z, 1.0, 1e-12) << "on the ground plane";
    EXPECT_TRUE(hasPointNear(s, {-10, 25}, 1e-9));
    EXPECT_EQ(h.session().tool(), SketchTool::Select);
    EXPECT_EQ(h.session().regions().size(), 1u);
    // Facing it, and framed: the outline fills a good part of the view.
    EXPECT_GT(h.controller.camera().backward().z, 0.999);
    const Vec2 a = h.sketchScreen({-30, -20}), b = h.sketchScreen({30, 25});
    EXPECT_GT(std::abs(b.x - a.x), 300);
    EXPECT_LT(std::abs(b.x - a.x), 1200);
    // A tap inside (away from the origin's point) selects the shape:
    // Extrude is offered.
    h.controller.pointerPress(Harness::at(h.sketchScreen({8, 6})));
    h.controller.pointerRelease(Harness::at(h.sketchScreen({8, 6})));
    EXPECT_TRUE(h.session().selectedRegion().has_value());
    const auto actions = h.controller.contextActions();
    EXPECT_TRUE(std::any_of(actions.begin(), actions.end(), [](const ContextAction& action) { return action.id == "extrude"; }));

    // Undo: the sketch is gone in one step.
    h.controller.finishSketch();
    EXPECT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.document.sketches().empty());
}

TEST(MeasuredGeometry, AnOpenOutlineAndRefusedOnes)
{
    Harness h;
    ASSERT_TRUE(h.controller.createOutlineSketch({{0, 0}, {40, 0}, {40, 30}}, false).ok());
    EXPECT_EQ(h.sketch().lines().size(), 2u);
    EXPECT_TRUE(h.session().regions().empty());
    // Closed on the first point: it is not drawn twice.
    ASSERT_TRUE(h.controller.createOutlineSketch({{0, 0}, {10, 0}, {10, 10}, {0, 0}}, true).ok());
    EXPECT_EQ(h.sketch().lines().size(), 3u);
    EXPECT_EQ(h.document.sketches().size(), 2u);

    const std::size_t steps = h.stack.index();
    h.messages.clear();
    EXPECT_FALSE(h.controller.createOutlineSketch({{5, 5}}, false).ok());
    EXPECT_FALSE(h.controller.createOutlineSketch({{5, 5}, {5, 5}}, false).ok());
    EXPECT_FALSE(h.controller.createOutlineSketch({{0, 0}, {10, 0}, {20, 0}}, true).ok()) << "no area";
    EXPECT_FALSE(h.controller.createOutlineSketch({{0, 0}, {std::nan(""), 0}}, false).ok());
    EXPECT_EQ(h.messages.size(), 4u) << "each refusal says why";
    EXPECT_EQ(h.stack.index(), steps);
}

TEST(MeasuredGeometry, ACircleSketchHasItsMeasuredDiameter)
{
    Harness h;
    ASSERT_TRUE(h.controller.createCircleSketch({4, -2}, 24.26).ok()); // a coin
    const auto& s = h.sketch();
    ASSERT_EQ(s.circles().size(), 1u);
    EXPECT_NEAR(s.circles().begin()->second.radius, 12.13, 1e-9);
    EXPECT_EQ(h.constraints(sketch::ConstraintKind::Diameter), 1u);
    EXPECT_TRUE(hasPointNear(s, {4, -2}, 1e-9));
    EXPECT_EQ(h.session().regions().size(), 1u);
    // Framed on the whole circle (not only its center point).
    h.controller.skipAnimation();
    const double across = (h.sketchScreen({4 + 12.13, -2}) - h.sketchScreen({4 - 12.13, -2})).length();
    EXPECT_GT(across, 250) << "the circle fills a good part of the view";
    EXPECT_LT(across, 800) << "and all of it is in view";
    EXPECT_FALSE(h.controller.createCircleSketch({0, 0}, 0).ok());
    EXPECT_FALSE(h.controller.createCircleSketch({0, 0}, -3).ok());
    h.controller.finishSketch();
    EXPECT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.document.sketches().empty());
}

TEST(MeasuredGeometry, AFoundOutlineWithAnArcAndAHoleIsOneSketch)
{
    Harness h;
    // A 40 x 20 plate with a round right end (an arc through (50, 10)) and a
    // 6 mm hole, as Find outline hands it over.
    measure::ProfileLoop plate;
    plate.points = {{0, 0}, {40, 0}, {50, 10}, {40, 20}, {0, 20}};
    plate.through = {false, false, true, false, false};
    measure::ProfileLoop hole;
    for (int i = 0; i < 4; ++i) {
        const double a = i * kPi / 2;
        hole.points.push_back(Vec2{10, 10} + Vec2{std::cos(a), std::sin(a)} * 3.0);
        hole.through.push_back(i % 2 == 1);
    }
    const std::size_t steps = h.stack.index();
    ASSERT_TRUE(h.controller.createProfileSketch({plate, hole}).ok());
    EXPECT_EQ(h.stack.index(), steps + 1) << "one undo step";
    ASSERT_EQ(h.controller.mode(), InteractionController::Mode::Sketch);
    const auto& s = h.sketch();
    EXPECT_EQ(s.lines().size(), 3u);
    EXPECT_EQ(s.arcs().size(), 1u);
    ASSERT_EQ(s.circles().size(), 1u) << "the hole's two half arcs are one circle";
    EXPECT_NEAR(s.circles().begin()->second.radius, 3, 1e-9);
    EXPECT_EQ(h.constraints(sketch::ConstraintKind::Diameter), 1u);
    // The plate with its hole is a region (and the hole's disc another).
    ASSERT_EQ(h.session().regions().size(), 2u);
    const double plateArea = 40 * 20 + kPi * 10 * 10 / 2 - kPi * 9;
    EXPECT_NEAR(h.session().regions()[0].area, plateArea, 1e-6);
    EXPECT_FALSE(h.controller.createProfileSketch({}).ok());
    measure::ProfileLoop line;
    line.points = {{0, 0}, {10, 0}, {20, 0}};
    line.through = {false, false, false};
    EXPECT_FALSE(h.controller.createProfileSketch({line}).ok()) << "no area";
    EXPECT_EQ(h.stack.index(), steps + 1);
}

TEST(MeasuredGeometry, ASizedBoxIsSelectedAndOneUndoStep)
{
    Harness h;
    ASSERT_TRUE(h.controller.createSizedBox(60, 35, 12.5).ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies().front()->shape()), 60 * 35 * 12.5, 1e-6);
    const auto bb = geom::boundingBox(h.document.bodies().front()->shape());
    EXPECT_NEAR(bb.max.x - bb.min.x, 60, 1e-6);
    EXPECT_NEAR(bb.max.y - bb.min.y, 35, 1e-6);
    EXPECT_NEAR(bb.max.z - bb.min.z, 12.5, 1e-6);
    EXPECT_NEAR(bb.min.x + bb.max.x, 0, 1e-6) << "the first one centered on the origin";
    EXPECT_EQ(h.controller.selection().singleBody(), h.document.bodies().front()->id());
    // A second one goes beside it.
    ASSERT_TRUE(h.controller.createSizedBox(10, 10, 10).ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_GT(geom::boundingBox(h.document.bodies().back()->shape()).min.x, 30 - 1e-6);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
    EXPECT_FALSE(h.controller.createSizedBox(10, 0, 10).ok());
    EXPECT_FALSE(h.controller.createSizedBox(10, 10, std::nan("")).ok());
    EXPECT_EQ(h.document.bodies().size(), 1u);
}
