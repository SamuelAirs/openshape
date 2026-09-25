// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sketching driven through the interaction layer with synthetic input: the
// same code paths the UI uses.
#include "commands/Command.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

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

    static PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left)
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        return e;
    }

    // Screen position of a point in the active sketch's 2D coordinates.
    Vec2 sketchScreen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }

    void move(Vec2 p) { controller.pointerMove(at(p, PointerButton::None)); }
    void click(Vec2 p, bool shift = false)
    {
        auto e = at(p);
        e.modifiers.shift = shift;
        controller.pointerMove(at(p, PointerButton::None));
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    void drag(Vec2 from, Vec2 to)
    {
        controller.pointerPress(at(from));
        for (int i = 1; i <= 6; ++i)
            controller.pointerMove(at(from + (to - from) * (i / 6.0)));
        controller.pointerRelease(at(to));
    }
    void type(const std::string& text)
    {
        EXPECT_EQ(session().typeIntoInput(text), "");
    }

    std::size_t count(sketch::ConstraintKind kind) const
    {
        std::size_t n = 0;
        for (const auto& [id, c] : controller.sketchSession()->sketch().constraints())
            n += c.kind == kind ? 1 : 0;
        return n;
    }
};

} // namespace

// Milestone 1 workflow: sketch on XY, rectangle with typed 60 x 40, finish,
// select the profile, extrude 20 mm, fillet the four vertical edges 3 mm.
TEST(SketchInteraction, RectangleTypedDimensionsThenExtrude)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    ASSERT_EQ(h.controller.mode(), InteractionController::Mode::Sketch);
    EXPECT_EQ(h.session().tool(), SketchTool::Rectangle);
    // Top view of the XY plane.
    EXPECT_NEAR(h.controller.camera().forward().z, -1.0, 1e-9);

    // Click the first corner on the origin, move, type 60 Tab 40 Enter.
    h.click(h.sketchScreen({0, 0}));
    EXPECT_TRUE(h.session().isDrawing());
    h.move(h.sketchScreen({35, 22}));
    h.type("60");
    h.session().focusNextInput();
    h.type("40");
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_FALSE(h.session().isDrawing());

    const sketch::Sketch& s = h.session().sketch();
    EXPECT_EQ(s.lines().size(), 4u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Horizontal), 2u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Vertical), 2u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::HorizontalDistance), 1u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::VerticalDistance), 1u);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0) << "anchored at the origin and dimensioned";
    EXPECT_EQ(h.session().statusText(), "Fully defined");
    bool found = false;
    for (const auto& [id, p] : s.points())
        found = found || (std::abs(p.position.x - 60) < 1e-9 && std::abs(p.position.y - 40) < 1e-9);
    EXPECT_TRUE(found) << "opposite corner at (60, 40)";

    // Dimension labels are offered for editing.
    int dimensions = 0;
    for (const auto& label : h.session().labels(h.controller.camera()))
        dimensions += label.kind == SketchLabel::Kind::Dimension ? 1 : 0;
    EXPECT_EQ(dimensions, 2);

    const Uuid sketchId = h.session().sketchId();
    h.controller.finishSketch();
    EXPECT_EQ(h.controller.mode(), InteractionController::Mode::Model);
    ASSERT_NE(h.document.sketch(sketchId), nullptr);

    // Select the profile: click inside the rectangle.
    const Vec2 inside = h.controller.camera().project({30, 20, 0});
    h.click(inside);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Extrude");

    EXPECT_EQ(h.controller.setValueText("20"), "");
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    const auto bb = geom::boundingBox(h.document.bodies()[0]->shape());
    EXPECT_NEAR(bb.size().x, 60, 1e-6);
    EXPECT_NEAR(bb.size().y, 40, 1e-6);
    EXPECT_NEAR(bb.size().z, 20, 1e-6);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 48000, 1e-4);
    EXPECT_TRUE(h.messages.empty());
}

TEST(SketchInteraction, DragToDrawRectangleAndCircle)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.drag(h.sketchScreen({-10, -10}), h.sketchScreen({10, 10}));
    EXPECT_EQ(h.session().sketch().lines().size(), 4u);

    h.controller.setSketchTool(SketchTool::Circle);
    h.click(h.sketchScreen({0, 0}));      // center on the origin
    h.move(h.sketchScreen({3, 0}));
    h.type("6");                           // diameter
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.session().sketch().circles().size(), 1u);
    EXPECT_NEAR(h.session().sketch().circles().begin()->second.radius, 3.0, 1e-9);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Diameter), 1u);

    // Two regions: square with hole, and the disc.
    h.controller.finishSketch();
    const auto* sk = h.document.sketches().front().get();
    ASSERT_NE(sk, nullptr);
    h.click(h.controller.camera().project({6, 6, 0}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_NE(h.controller.selectionSummary().find("Profile"), std::string::npos);
}

TEST(SketchInteraction, LineToolInfersHorizontalAndClosesLoop)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({20, 0.3})); // nearly horizontal -> inferred
    EXPECT_EQ(h.count(sketch::ConstraintKind::Horizontal), 1u);
    h.click(h.sketchScreen({20, 15}));
    EXPECT_EQ(h.count(sketch::ConstraintKind::Vertical), 1u);
    h.click(h.sketchScreen({0, 0}));    // back on the start point: closes the loop
    EXPECT_FALSE(h.session().isDrawing());
    EXPECT_EQ(h.session().sketch().lines().size(), 3u);
    // A closed triangle is one region.
    h.controller.finishSketch();
    h.click(h.controller.camera().project({15, 5, 0}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
}

// Right-click ends a line chain like Esc (segments stay); a second right-click
// leaves the tool. Dragging with the right button still orbits.
TEST(SketchInteraction, RightClickEndsLineChain)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({20, 0}));
    h.click(h.sketchScreen({20, 15}));
    ASSERT_TRUE(h.session().isDrawing());
    const auto rightClick = [&](Vec2 p) {
        h.controller.pointerPress(Harness::at(p, PointerButton::Right));
        h.controller.pointerRelease(Harness::at(p, PointerButton::Right));
    };
    rightClick(h.sketchScreen({5, 10}));
    EXPECT_FALSE(h.session().isDrawing());
    EXPECT_EQ(h.session().sketch().lines().size(), 2u);
    EXPECT_EQ(h.session().tool(), SketchTool::Line);
    rightClick(h.sketchScreen({5, 10}));
    EXPECT_EQ(h.session().tool(), SketchTool::Select);

    const double yaw = h.controller.camera().yaw;
    h.controller.pointerPress(Harness::at({600, 400}, PointerButton::Right));
    for (int i = 1; i <= 6; ++i)
        h.controller.pointerMove(Harness::at({600.0 + 20 * i, 400}, PointerButton::Right));
    h.controller.pointerRelease(Harness::at({720, 400}, PointerButton::Right));
    EXPECT_NE(h.controller.camera().yaw, yaw);
    EXPECT_EQ(h.session().sketch().lines().size(), 2u);
}

TEST(SketchInteraction, ConstraintsFromSelectionAndDimensionEdit)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({18, 7}));   // free direction: no inference
    EXPECT_EQ(h.count(sketch::ConstraintKind::Horizontal), 0u);
    ASSERT_TRUE(h.controller.keyPress(Key::Escape)); // end the chain
    ASSERT_TRUE(h.controller.keyPress(Key::Escape)); // back to Select
    EXPECT_EQ(h.session().tool(), SketchTool::Select);

    // Select the line and make it horizontal, then give it a length.
    h.click(h.sketchScreen({9, 3.5}));
    ASSERT_EQ(h.session().selection().size(), 1u);
    ASSERT_TRUE(h.controller.triggerAction("horizontal").ok());
    ASSERT_TRUE(h.controller.triggerAction("length").ok());
    sketch::EntityId dim = sketch::kNoEntity;
    for (const auto& [id, c] : h.session().sketch().constraints())
        if (c.kind == sketch::ConstraintKind::Distance)
            dim = id;
    ASSERT_NE(dim, sketch::kNoEntity);
    const double before = h.session().sketch().constraint(dim)->value;
    EXPECT_EQ(h.session().setDimension(dim, "25"), "");
    const auto& line = h.session().sketch().lines().begin()->second;
    const Vec2 a = h.session().sketch().point(line.start)->position;
    const Vec2 b = h.session().sketch().point(line.end)->position;
    EXPECT_NEAR(a.y, b.y, 1e-9);
    EXPECT_NEAR((b - a).length(), 25.0, 1e-9);
    // Undo the dimension edit.
    EXPECT_TRUE(h.controller.undo());
    const Vec2 a2 = h.session().sketch().point(line.start)->position;
    const Vec2 b2 = h.session().sketch().point(line.end)->position;
    EXPECT_NEAR((b2 - a2).length(), before, 1e-9);
}

TEST(SketchInteraction, SketchOnFaceCutsHole)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // box from (-10,-10,0) to (10,10,20)
    h.controller.fitAll(false);
    h.click(h.controller.camera().project({0, 0, 20}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    EXPECT_NEAR(h.session().sketch().plane().origin.z, 20.0, 1e-9);
    h.controller.setSketchTool(SketchTool::Circle);
    h.click(h.sketchScreen({0, 0}));
    h.move(h.sketchScreen({2, 0}));
    h.type("8");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.finishSketch();

    h.click(h.controller.camera().project({0, 0, 20}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    // Sketch placed on a body: pushing inward cuts.
    EXPECT_EQ(h.controller.setValueText("-20"), "");
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(h.controller.operation());
    ASSERT_NE(extrude, nullptr);
    EXPECT_EQ(extrude->mode(), doc::ExtrudeMode::Cut);
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 8000 - kPi * 16 * 20, 1e-3);
}

namespace {
// Draws a typed w x h rectangle with its first corner at `corner` (sketch coordinates).
void typedRectangle(Harness& h, Vec2 corner, double w, double hgt)
{
    h.controller.setSketchTool(SketchTool::Rectangle);
    h.click(h.sketchScreen(corner));
    h.move(h.sketchScreen(corner + Vec2{w * 0.6, hgt * 0.6}));
    h.type(std::to_string(w));
    h.session().focusNextInput();
    h.type(std::to_string(hgt));
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
}
} // namespace

// A profile drawn on a body's face but off the body: pulling it out would
// "join" a separate piece. It becomes a new body instead (as in Shapr3D).
TEST(SketchInteraction, JoinThatMissesTheBodyMakesANewBody)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
    h.controller.fitAll(false);
    h.click(h.controller.camera().project({0, 0, 20}));
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    typedRectangle(h, {30, 30}, 10, 10);
    const Uuid sketchId = h.session().sketchId();
    h.controller.finishSketch();

    const Vec3 inside = h.document.sketch(sketchId)->plane().toWorld({35, 35});
    h.click(h.controller.camera().project(inside));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    EXPECT_EQ(h.controller.setValueText("5"), "");
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(h.controller.operation());
    ASSERT_NE(extrude, nullptr);
    EXPECT_EQ(extrude->mode(), doc::ExtrudeMode::NewBody);
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 8000.0, 1e-3);
    EXPECT_NEAR(geom::volume(h.document.bodies()[1]->shape()), 500.0, 1e-3);
}

// A cut that splits a body is allowed, but the model panel says so.
TEST(SketchInteraction, CutThatSplitsTheBodyIsFlagged)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.click(h.controller.camera().project({0, 0, 20}));
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    typedRectangle(h, {-1, -15}, 2, 30); // a slot across the whole top face
    const Uuid sketchId = h.session().sketchId();
    h.controller.finishSketch();

    const Vec3 inside = h.document.sketch(sketchId)->plane().toWorld({0, 12});
    h.click(h.controller.camera().project(inside));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.setValueText("-25"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_EQ(h.document.bodies()[0]->shape().solidCount(), 2);
    bool bodyWarned = false, stepWarned = false;
    for (const auto& row : h.controller.historyRows()) {
        if (row.kind == HistoryRow::Kind::Body)
            bodyWarned = row.status == HistoryRow::Status::Warning && row.message.find("2 separate pieces") != std::string::npos;
        if (row.kind == HistoryRow::Kind::Feature && row.name == "Extrude")
            stepWarned = row.status == HistoryRow::Status::Warning;
    }
    EXPECT_TRUE(bodyWarned);
    EXPECT_TRUE(stepWarned);
}

// Sketching on a sketch continues it: with a profile selected, Sketch enters
// the same sketch, and a line across the rectangle splits it in two.
TEST(SketchInteraction, SketchOnASketchContinuesIt)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    typedRectangle(h, {0, 0}, 40, 20);
    const Uuid first = h.session().sketchId();
    h.controller.finishSketch();
    h.click(h.controller.camera().project({10, 10, 0}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    EXPECT_EQ(h.session().sketchId(), first);
    EXPECT_EQ(h.document.sketches().size(), 1u);
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({20, -6}));
    h.click(h.sketchScreen({20, 26}));
    h.controller.keyPress(Key::Escape);
    h.controller.finishSketch();
    const auto regions = doc::sketchRegions(*h.document.sketch(first));
    ASSERT_TRUE(regions.ok());
    EXPECT_EQ(regions.value().size(), 2u);
}

// Starting a sketch on a plane that already has one continues it.
TEST(SketchInteraction, SamePlaneContinuesTheSketch)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    typedRectangle(h, {0, 0}, 40, 20);
    const Uuid first = h.session().sketchId();
    h.controller.finishSketch();
    ASSERT_TRUE(h.controller.startSketch().ok()); // nothing selected: the ground plane again
    EXPECT_EQ(h.session().sketchId(), first);
    EXPECT_EQ(h.document.sketches().size(), 1u);
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("Continuing"), std::string::npos);
    h.controller.finishSketch();
    // Another plane still gets its own sketch.
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Front).ok());
    EXPECT_NE(h.session().sketchId(), first);
}

// Constraint actions offered for a selection, applied through the session.
TEST(SketchInteraction, ParallelAndConstructionFromSelection)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({30, 8}));
    h.controller.keyPress(Key::Escape);
    h.click(h.sketchScreen({0, 15}));
    h.click(h.sketchScreen({25, 30}));
    h.controller.keyPress(Key::Escape);
    h.controller.keyPress(Key::Escape); // back to Select
    h.click(h.sketchScreen({15, 4}));
    h.click(h.sketchScreen({12.5, 22.5}), true);
    ASSERT_EQ(h.session().selection().size(), 2u);
    bool parallel = false, construction = false;
    for (const auto& a : h.session().contextActions()) {
        parallel = parallel || a.id == "parallel";
        construction = construction || a.id == "construction";
    }
    ASSERT_TRUE(parallel && construction);
    ASSERT_TRUE(h.controller.triggerAction("parallel").ok());
    EXPECT_EQ(h.count(sketch::ConstraintKind::Parallel), 1u);
    ASSERT_TRUE(h.controller.triggerAction("construction").ok());
    for (const auto& [id, l] : h.session().sketch().lines())
        EXPECT_TRUE(l.construction);
    EXPECT_TRUE(h.controller.undo()); // normal geometry again
    for (const auto& [id, l] : h.session().sketch().lines())
        EXPECT_FALSE(l.construction);
}

// Arc tool: start, end, then a third click bends it through that point; a
// typed radius gives an exact arc. A line closed by an arc extrudes.
TEST(SketchInteraction, ArcToolThreeClicksAndTypedRadius)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({20, 0}));
    h.controller.keyPress(Key::Escape);
    h.controller.setSketchTool(SketchTool::Arc);
    h.click(h.sketchScreen({20, 0})); // the line's end
    h.click(h.sketchScreen({0, 0}));  // back to the origin
    EXPECT_TRUE(h.session().isDrawing());
    h.click(h.sketchScreen({10, 5})); // bend it up through (10, 5)
    EXPECT_FALSE(h.session().isDrawing());
    ASSERT_EQ(h.session().sketch().arcs().size(), 1u);
    const sketch::EntityId arcId = h.session().sketch().arcs().begin()->first;
    // Circle through (0,0), (20,0), (10,5): center (10,-7.5), radius 12.5.
    EXPECT_NEAR(h.session().sketch().arcRadius(arcId), 12.5, 1e-6);

    const Uuid sketchId = h.session().sketchId();
    h.controller.finishSketch();
    const auto regions = doc::sketchRegions(*h.document.sketch(sketchId));
    ASSERT_TRUE(regions.ok());
    ASSERT_EQ(regions.value().size(), 1u);
    const double half = std::asin(10.0 / 12.5); // half the central angle
    const double segment = 12.5 * 12.5 / 2 * (2 * half - std::sin(2 * half));
    EXPECT_NEAR(regions.value()[0].area, segment, 1e-6);
    h.click(h.controller.camera().project(h.document.sketch(sketchId)->plane().toWorld({10, 2})));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.setValueText("4"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), segment * 4, 1e-3);

    // A typed radius: exact, with a Radius dimension.
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Front).ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Arc);
    h.click(h.sketchScreen({30, 0}));
    h.click(h.sketchScreen({50, 0}));
    h.move(h.sketchScreen({40, 3}));
    h.type("15");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.session().sketch().arcs().size(), 1u);
    EXPECT_NEAR(h.session().sketch().arcRadius(h.session().sketch().arcs().begin()->first), 15.0, 1e-7);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Radius), 1u);
}

namespace {
// A 20 mm cube (centered) with a vertical 8 mm hole cut through its top.
void cubeWithHole(Harness& h)
{
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.click(h.controller.camera().project({0, 0, 20}));
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Circle);
    h.click(h.sketchScreen({0, 0}));
    h.move(h.sketchScreen({2, 0}));
    h.type("8");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.finishSketch();
    h.click(h.controller.camera().project({0, 0, 20}));
    EXPECT_EQ(h.controller.setValueText("-20"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    h.controller.skipAnimation();
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);
}
// The far side of the hole's wall, seen from the default view.
Vec2 holeWall(Harness& h)
{
    return h.controller.camera().project({-2.83, 2.83, 17});
}
} // namespace

// Clicking a hole's wall offers its diameter: type the new one.
TEST(SketchInteraction, ResizeHoleByDiameter)
{
    Harness h;
    cubeWithHole(h);
    h.click(holeWall(h));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    const auto* offset = dynamic_cast<const OffsetFaceOperation*>(h.controller.operation());
    ASSERT_NE(offset, nullptr);
    EXPECT_TRUE(offset->round());
    EXPECT_NEAR(offset->value(), 8.0, 1e-9);
    EXPECT_EQ(offset->valueLabel(), "Diameter");
    EXPECT_EQ(h.controller.setValueText("8.4"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 8000 - kPi * 4.2 * 4.2 * 20, 1e-3);
}

// Delete on a selected hole wall removes the hole.
TEST(SketchInteraction, DeleteKeyRemovesAHole)
{
    Harness h;
    cubeWithHole(h);
    h.click(holeWall(h));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 8000.0, 1e-3);
    EXPECT_EQ(h.stack.undoLabel(), "Delete faces");
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 8000 - kPi * 16 * 20, 1e-3);
}

TEST(SketchInteraction, UndoPastSketchCreationLeavesSketchMode)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.drag(h.sketchScreen({0, 0}), h.sketchScreen({10, 10}));
    EXPECT_TRUE(h.controller.undo()); // rectangle
    EXPECT_EQ(h.controller.mode(), InteractionController::Mode::Sketch);
    EXPECT_TRUE(h.session().sketch().lines().empty());
    EXPECT_TRUE(h.controller.undo()); // sketch creation
    EXPECT_EQ(h.controller.mode(), InteractionController::Mode::Model);
    EXPECT_TRUE(h.document.sketches().empty());
}

TEST(SketchInteraction, EmptySketchIsRemovedOnFinish)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.finishSketch();
    EXPECT_TRUE(h.document.sketches().empty());
}

TEST(SketchInteraction, DraggingAPointReshapes)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.drag(h.sketchScreen({0, 0}), h.sketchScreen({10, 10}));
    h.controller.setSketchTool(SketchTool::Select);
    // Drag the far corner to (16, 12); the rectangle follows and stays square-cornered.
    h.drag(h.sketchScreen({10, 10}), h.sketchScreen({16, 12}));
    bool moved = false;
    for (const auto& [id, p] : h.session().sketch().points())
        moved = moved || ((p.position - Vec2{16, 12}).length() < 0.05);
    EXPECT_TRUE(moved);
    EXPECT_TRUE(h.session().sketch().solveReport().ok);
}

TEST(SketchInteraction, PositionHoleFromOrigin)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Circle);
    h.click(h.sketchScreen({10, 15}));
    h.move(h.sketchScreen({13, 15}));
    h.type("6");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.setSketchTool(SketchTool::Select);

    // Select the origin, then Shift-select the circle center.
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({10, 15}), true);
    ASSERT_EQ(h.session().selection().size(), 2u);
    ASSERT_TRUE(h.controller.triggerAction("hdistance").ok());
    ASSERT_TRUE(h.controller.triggerAction("vdistance").ok());
    EXPECT_EQ(h.session().sketch().solveReport().degreesOfFreedom, 0) << "diameter + two positions";

    sketch::EntityId hdim = sketch::kNoEntity;
    for (const auto& [id, c] : h.session().sketch().constraints())
        if (c.kind == sketch::ConstraintKind::HorizontalDistance)
            hdim = id;
    ASSERT_NE(hdim, sketch::kNoEntity);
    EXPECT_EQ(h.session().setDimension(hdim, "12"), "");
    const auto& circle = h.session().sketch().circles().begin()->second;
    EXPECT_NEAR(h.session().sketch().point(circle.center)->position.x, 12.0, 1e-9);
    EXPECT_NEAR(h.session().sketch().point(circle.center)->position.y, 15.0, 1e-9);
}

TEST(SketchInteraction, OriginPlanesFaceTheViewer)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Front).ok());
    h.controller.skipAnimation();
    const auto& front = h.session().sketch().plane();
    EXPECT_NEAR(front.normal().y, -1.0, 1e-12);
    EXPECT_NEAR(h.controller.camera().forward().y, 1.0, 1e-9) << "looking from the front";
    EXPECT_NEAR(h.controller.camera().right().x, 1.0, 1e-9) << "sketch x to the right";
    // A rectangle drawn on the front plane stands up in Z.
    h.drag(h.sketchScreen({0, 0}), h.sketchScreen({10, 20}));
    h.controller.finishSketch();
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);
    h.click(h.controller.camera().project({5, 0, 10}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.setValueText("-4"), ""); // toward +Y (away from the front viewer)
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const auto bb = geom::boundingBox(h.document.bodies().front()->shape());
    EXPECT_NEAR(bb.size().z, 20.0, 1e-6);
    EXPECT_NEAR(bb.min.y, 0.0, 1e-6);
    EXPECT_NEAR(bb.max.y, 4.0, 1e-6);

    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Right).ok());
    h.controller.skipAnimation();
    EXPECT_NEAR(h.session().sketch().plane().normal().x, 1.0, 1e-12);
    EXPECT_NEAR(h.controller.camera().forward().x, -1.0, 1e-9);
}

// Maker spacer: revolve a rectangle on the front plane around the vertical axis.
TEST(SketchInteraction, RevolveSpacer)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Front).ok());
    h.controller.skipAnimation();
    h.click(h.sketchScreen({5, 0}));
    h.move(h.sketchScreen({8, 10}));
    h.type("3");
    h.session().focusNextInput();
    h.type("10");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.finishSketch();
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);

    h.click(h.controller.camera().project({6.5, 0, 5}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_TRUE(h.controller.triggerAction("revolve").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Revolve");
    EXPECT_EQ(h.controller.operationValueText(), "360.0\xC2\xB0");
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    const doc::Body& body = *h.document.bodies().front();
    EXPECT_NEAR(geom::volume(body.shape()), kPi * (64 - 25) * 10, 1e-3);
    const auto bb = geom::boundingBox(body.shape());
    EXPECT_NEAR(bb.size().z, 10.0, 1e-6);
    EXPECT_NEAR(bb.size().x, 16.0, 1e-6);

    // Half a turn through the history (typed in degrees).
    const Uuid revolveId = body.features().front()->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(revolveId, "angle", "180").ok());
    EXPECT_NEAR(geom::volume(body.shape()), kPi * (64 - 25) * 10 / 2, 1e-3);
    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        listed = listed || (row.name == "Revolve" && row.detail.find("180.0") != std::string::npos);
    EXPECT_TRUE(listed);
}

TEST(SketchInteraction, RevolveTypedAngle)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Front).ok());
    h.controller.skipAnimation();
    h.drag(h.sketchScreen({4, 0}), h.sketchScreen({10, 6}));
    h.controller.finishSketch();
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);
    h.click(h.controller.camera().project({7, 0, 3}));
    ASSERT_TRUE(h.controller.triggerAction("revolve").ok());
    EXPECT_EQ(h.controller.setValueText("90"), "");
    EXPECT_NE(h.controller.setValueText("400"), "") << "more than a full turn is refused";
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 90.0) << "a refused value keeps the previous one";
    EXPECT_EQ(h.controller.setValueText("1rad"), "");
    EXPECT_NEAR(h.controller.operation()->value(), 57.2957795, 1e-6);
}
