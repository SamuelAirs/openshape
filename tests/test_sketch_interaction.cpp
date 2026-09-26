// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sketching driven through the interaction layer with synthetic input: the
// same code paths the UI uses.
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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

namespace {
// A 60 x 40 rectangle from the origin on the ground, sketch finished, its
// profile selected (the Extrude operation armed).
void rectangleProfileSelected(Harness& h)
{
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.click(h.sketchScreen({0, 0}));
    h.move(h.sketchScreen({35, 22}));
    h.type("60");
    h.session().focusNextInput();
    h.type("40");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.finishSketch();
    h.controller.skipAnimation();
    h.click(h.controller.camera().project({30, 20, 0}));
    ASSERT_NE(h.controller.operation(), nullptr);
    ASSERT_EQ(h.controller.operation()->title(), "Extrude");
}
} // namespace

// Symmetric: the value is the total thickness, centered on the sketch plane.
TEST(SketchInteraction, SymmetricExtrudeIsCenteredOnTheSketch)
{
    Harness h;
    rectangleProfileSelected(h);
    ASSERT_TRUE(h.controller.triggerAction("symmetric").ok());
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Thickness");
    EXPECT_EQ(h.controller.setValueText("10"), "");
    EXPECT_NE(h.controller.setValueText("-4"), "") << "a thickness is never negative";
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    const auto bb = geom::boundingBox(h.document.bodies()[0]->shape());
    EXPECT_NEAR(bb.min.z, -5.0, 1e-6);
    EXPECT_NEAR(bb.max.z, 5.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 60.0 * 40.0 * 10.0, 1e-4);

    // Stored as a symmetric extrusion, shown so in the Model panel, and kept in files.
    const auto& feature = static_cast<const doc::ExtrudeFeature&>(*h.document.bodies()[0]->features().front());
    EXPECT_TRUE(feature.symmetric);
    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        listed = listed || row.detail.find("symmetric") != std::string::npos;
    EXPECT_TRUE(listed);
    nlohmann::json params;
    feature.writeParams(params);
    doc::ExtrudeFeature copy;
    ASSERT_TRUE(copy.readParams(params).ok());
    EXPECT_TRUE(copy.symmetric);
}

// Up to face: the next face click sets the distance so the extrusion ends on it.
TEST(SketchInteraction, ExtrudeUpToAFace)
{
    Harness h;
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = {80, 0, 0};
    box->size = {10, 10, 30};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>("Tower", std::move(box)), h.document).ok());
    h.controller.documentChanged();
    rectangleProfileSelected(h);
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);

    ASSERT_TRUE(h.controller.triggerAction("upToFace").ok());
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(h.controller.operation());
    ASSERT_NE(extrude, nullptr);
    EXPECT_TRUE(extrude->pickingTarget());
    EXPECT_FALSE(extrude->prompt().empty());

    // A face that is not parallel to the sketch is refused; picking goes on.
    h.click(h.controller.camera().project({90, 5, 15})); // the tower's +X side
    EXPECT_FALSE(h.messages.empty());
    EXPECT_TRUE(extrude->pickingTarget());

    h.click(h.controller.camera().project({85, 5, 30})); // the tower's top
    ASSERT_EQ(h.controller.operation(), extrude);
    EXPECT_FALSE(extrude->pickingTarget());
    EXPECT_NEAR(extrude->value(), 30.0, 1e-9);
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    EXPECT_NEAR(geom::boundingBox(h.document.bodies()[1]->shape()).size().z, 30.0, 1e-6);
}

// Esc leaves face picking without dropping the extrusion.
TEST(SketchInteraction, EscapeLeavesUpToFacePicking)
{
    Harness h;
    rectangleProfileSelected(h);
    EXPECT_EQ(h.controller.setValueText("12"), "");
    ASSERT_TRUE(h.controller.triggerAction("upToFace").ok());
    h.controller.keyPress(Key::Escape);
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(h.controller.operation());
    ASSERT_NE(extrude, nullptr);
    EXPECT_FALSE(extrude->pickingTarget());
    EXPECT_NEAR(extrude->value(), 12.0, 1e-9);
}

namespace {
// A 40 x 20 rectangle from the origin, dimensioned (still in sketch mode).
void rectangle40x20(Harness& h)
{
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.click(h.sketchScreen({0, 0}));
    h.move(h.sketchScreen({30, 12}));
    h.type("40");
    h.session().focusNextInput();
    h.type("20");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
}

double largestRegion(const sketch::Sketch& s)
{
    const auto regions = doc::sketchRegions(s);
    return regions.ok() && !regions.value().empty() ? regions.value().front().area : 0.0;
}
} // namespace

TEST(SketchInteraction, SlotToolWithTypedWidth)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.session().setTool(SketchTool::Slot);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({30, 0}));
    h.move(h.sketchScreen({15, 4}));
    EXPECT_FALSE(h.session().hintText().empty());
    h.type("10"); // the width
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    const sketch::Sketch& s = h.session().sketch();
    EXPECT_EQ(s.lines().size(), 2u);
    ASSERT_EQ(s.arcs().size(), 2u);
    for (const auto& [id, arc] : s.arcs())
        EXPECT_NEAR(s.arcRadius(id), 5.0, 1e-9);
    EXPECT_NEAR(largestRegion(s), 30 * 10 + kPi * 25, 1e-6);
}

TEST(SketchInteraction, TrimToolRemovesThePieceBeyondACrossing)
{
    Harness h;
    rectangle40x20(h);
    // A vertical line through the rectangle, sticking out above and below.
    h.session().setTool(SketchTool::Line);
    h.click(h.sketchScreen({20, -10}));
    h.click(h.sketchScreen({20, 30}));
    h.controller.keyPress(Key::Escape); // end the chain
    ASSERT_EQ(h.session().sketch().lines().size(), 5u);

    h.session().setTool(SketchTool::Trim);
    h.move(h.sketchScreen({20, 26}));
    bool red = false;
    for (const auto& line : h.session().renderData(h.controller.camera()).lines)
        red = red || line.style == SketchStyle::Conflict;
    EXPECT_TRUE(red) << "the piece to remove is previewed";
    h.click(h.sketchScreen({20, 26}));
    // The line now ends on the rectangle's top edge.
    const sketch::Sketch& s = h.session().sketch();
    bool shortened = false;
    for (const auto& [id, l] : s.lines()) {
        const Vec2 a = s.point(l.start)->position, b = s.point(l.end)->position;
        if (std::abs(a.x - 20) < 1e-9 && std::abs(b.x - 20) < 1e-9)
            shortened = std::abs(std::max(a.y, b.y) - 20) < 1e-9 && std::abs(std::min(a.y, b.y) + 10) < 1e-9;
    }
    EXPECT_TRUE(shortened);
    EXPECT_TRUE(h.messages.empty());
}

TEST(SketchInteraction, FilletCornerThenEditTheRadius)
{
    Harness h;
    rectangle40x20(h);
    h.session().setTool(SketchTool::Select);
    h.click(h.sketchScreen({40, 20})); // the top-right corner point
    ASSERT_EQ(h.session().selection().size(), 1u);
    bool offered = false;
    for (const auto& action : h.session().contextActions())
        offered = offered || action.id == "fillet";
    ASSERT_TRUE(offered);
    ASSERT_TRUE(h.session().triggerAction("fillet").ok());
    const sketch::Sketch& s = h.session().sketch();
    ASSERT_EQ(s.arcs().size(), 1u);
    const sketch::EntityId arc = s.arcs().begin()->first;
    EXPECT_NEAR(s.arcRadius(arc), 5.0, 1e-9) << "a quarter of the short side";
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0);
    // The radius is an editable dimension.
    sketch::EntityId radius = sketch::kNoEntity;
    for (const auto& [id, c] : s.constraints())
        if (c.kind == sketch::ConstraintKind::Radius)
            radius = id;
    ASSERT_NE(radius, sketch::kNoEntity);
    EXPECT_EQ(h.session().setDimension(radius, "3"), "");
    EXPECT_NEAR(h.session().sketch().arcRadius(arc), 3.0, 1e-9);
    EXPECT_NEAR(largestRegion(h.session().sketch()), 40 * 20 - (9 - kPi * 9 / 4), 1e-6);
}

TEST(SketchInteraction, OffsetSelectedCurves)
{
    Harness h;
    rectangle40x20(h);
    h.session().setTool(SketchTool::Select);
    for (const Vec2 mid : {Vec2{20, 0}, Vec2{40, 10}, Vec2{20, 20}, Vec2{0, 10}})
        h.click(h.sketchScreen(mid), true);
    ASSERT_EQ(h.session().selection().size(), 4u);
    ASSERT_TRUE(h.session().triggerAction("offset").ok());
    EXPECT_TRUE(h.session().isOffsetting());
    h.move(h.sketchScreen({45, 10})); // outside, to the right
    h.type("2");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_FALSE(h.session().isOffsetting());
    const sketch::Sketch& s = h.session().sketch();
    EXPECT_EQ(s.lines().size(), 8u);
    bool outer = false;
    for (const auto& [id, l] : s.lines()) {
        const Vec2 a = s.point(l.start)->position, b = s.point(l.end)->position;
        outer = outer || (std::abs(a.x - 42) < 1e-9 && std::abs(b.x - 42) < 1e-9);
    }
    EXPECT_TRUE(outer) << "the right side moved out by 2";
    const auto regions = doc::sketchRegions(s);
    ASSERT_TRUE(regions.ok());
    ASSERT_EQ(regions.value().size(), 2u);
    EXPECT_NEAR(regions.value()[0].area, 40 * 20, 1e-6);           // the original rectangle
    EXPECT_NEAR(regions.value()[1].area, 44 * 24 - 40 * 20, 1e-6); // the ring around it

    // Esc leaves offsetting without adding anything.
    for (const Vec2 mid : {Vec2{20, 0}})
        h.click(h.sketchScreen(mid));
    ASSERT_TRUE(h.session().triggerAction("offset").ok());
    h.controller.keyPress(Key::Escape);
    EXPECT_FALSE(h.session().isOffsetting());
    EXPECT_EQ(h.session().sketch().lines().size(), 8u);
}

// Preferences: "Snap sketches to the grid". On (the default), free points
// round to the zoom-dependent grid; off, they land exactly under the pointer.
TEST(SketchInteraction, GridSnapCanBeTurnedOff)
{
    for (const bool snap : {true, false}) {
        Harness h;
        h.controller.setSketchGridSnap(snap);
        ASSERT_TRUE(h.controller.startSketch().ok());
        h.controller.skipAnimation();
        EXPECT_EQ(h.session().gridSnap(), snap) << "a new sketch takes the setting";
        h.controller.setSketchTool(SketchTool::Line);
        const Vec2 a{3.37, 7.21}, b{13.53, 19.87}; // 51 degrees: no horizontal/vertical inference
        h.click(h.sketchScreen(a));
        h.click(h.sketchScreen(b));
        EXPECT_TRUE(h.controller.keyPress(Key::Escape));
        const sketch::Sketch& s = h.session().sketch();
        ASSERT_EQ(s.lines().size(), 1u);
        const auto& line = s.lines().begin()->second;
        const Vec2 p = s.point(line.start)->position, q = s.point(line.end)->position;
        if (snap) {
            for (double v : {p.x, p.y, q.x, q.y})
                EXPECT_NEAR(v * 10, std::round(v * 10), 1e-9) << v << " is on the grid";
            EXPECT_GT(std::abs(p.x - a.x) + std::abs(p.y - a.y), 1e-3) << "rounded, not where clicked";
        } else {
            EXPECT_NEAR(p.x, a.x, 1e-6);
            EXPECT_NEAR(p.y, a.y, 1e-6);
            EXPECT_NEAR(q.x, b.x, 1e-6);
            EXPECT_NEAR(q.y, b.y, 1e-6);
        }
        // Existing points still snap with the grid off: a line from near the end point.
        h.click(h.sketchScreen(q + Vec2{0.05, -0.04}));
        h.click(h.sketchScreen({25.31, 4.12}));
        EXPECT_TRUE(h.controller.keyPress(Key::Escape));
        EXPECT_EQ(s.lines().size(), 2u);
        EXPECT_EQ(s.points().size(), 4u) << "origin + 3 points: the second line starts on the first one's end";
        // Changing the setting applies to the open sketch at once.
        h.controller.setSketchGridSnap(!snap);
        EXPECT_EQ(h.session().gridSnap(), !snap);
    }
}

TEST(SketchInteraction, CenterRectangleTypedAndExtruded)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::CenterRectangle);
    h.click(h.sketchScreen({0, 0})); // the center, on the origin
    ASSERT_TRUE(h.session().isDrawing());
    h.move(h.sketchScreen({14, 7}));
    h.type("40");
    h.session().focusNextInput();
    h.type("20");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_FALSE(h.session().isDrawing());
    const sketch::Sketch& s = h.session().sketch();
    EXPECT_EQ(s.lines().size(), 5u) << "four sides and the construction diagonal";
    EXPECT_EQ(h.count(sketch::ConstraintKind::Midpoint), 1u);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0);
    EXPECT_NEAR(largestRegion(s), 800.0, 1e-6);
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto& [id, p] : s.points()) {
        minX = std::min(minX, p.position.x);
        maxX = std::max(maxX, p.position.x);
        minY = std::min(minY, p.position.y);
        maxY = std::max(maxY, p.position.y);
    }
    EXPECT_NEAR(minX, -20, 1e-9);
    EXPECT_NEAR(maxX, 20, 1e-9);
    EXPECT_NEAR(minY, -10, 1e-9);
    EXPECT_NEAR(maxY, 10, 1e-9);

    // Editing the width keeps the rectangle centered on the origin.
    sketch::EntityId width = sketch::kNoEntity;
    for (const auto& [id, c] : s.constraints())
        if (c.kind == sketch::ConstraintKind::HorizontalDistance)
            width = id;
    ASSERT_NE(width, sketch::kNoEntity);
    EXPECT_EQ(h.session().setDimension(width, "60"), "");
    EXPECT_NEAR(largestRegion(h.session().sketch()), 1200.0, 1e-6);

    h.controller.finishSketch();
    h.click(h.controller.camera().project({5, 5, 0}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.setValueText("10"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    const auto bb = geom::boundingBox(h.document.bodies()[0]->shape());
    EXPECT_NEAR(bb.min.x, -30, 1e-6);
    EXPECT_NEAR(bb.max.x, 30, 1e-6);
    EXPECT_NEAR(bb.min.y, -10, 1e-6);
    EXPECT_NEAR(bb.max.y, 10, 1e-6);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 12000, 1e-4);
    EXPECT_TRUE(h.messages.empty());
}

TEST(SketchInteraction, PolygonToolSidesAndAcrossFlats)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Polygon);
    ASSERT_TRUE(h.session().counter().has_value());
    EXPECT_EQ(h.session().counter()->value, 6) << "hexagons by default";
    EXPECT_EQ(h.session().counter()->text, "6 sides");
    h.click(h.sketchScreen({0, 0})); // the center, on the origin
    ASSERT_TRUE(h.session().isDrawing());
    h.move(h.sketchScreen({12, 0.2})); // straight to the right: the first side is vertical
    // The size label says what it measures.
    bool captioned = false;
    for (const auto& label : h.session().labels(h.controller.camera()))
        captioned = captioned || (label.key == "size" && label.caption == "across flats");
    EXPECT_TRUE(captioned);
    h.type("10"); // across flats
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    const sketch::Sketch& s = h.session().sketch();
    EXPECT_EQ(s.lines().size(), 6u);
    EXPECT_EQ(s.circles().size(), 2u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Vertical), 1u);
    EXPECT_EQ(s.solveReport().degreesOfFreedom, 0) << "centered, sized and aligned";
    const double hexagon = 6 * 25 * std::tan(kPi / 6);
    EXPECT_NEAR(largestRegion(s), hexagon, 1e-6);
    double maxX = -1e9;
    for (const auto& [id, l] : s.lines())
        maxX = std::max(maxX, s.point(l.start)->position.x);
    EXPECT_NEAR(maxX, 5.0, 1e-9) << "a flat at x = 5";

    // Fewer sides with -, then a typed count: an octagon 20 across flats, elsewhere.
    EXPECT_TRUE(h.session().stepCounter(-1));
    EXPECT_EQ(h.session().polygonSides(), 5);
    EXPECT_TRUE(h.session().stepCounter(+1));
    h.click(h.sketchScreen({40, 0}));
    h.move(h.sketchScreen({47, 3}));
    h.type("20");
    h.session().focusNextInput();
    EXPECT_NE(h.session().typeIntoInput("2"), "") << "too few sides";
    h.type("8");
    EXPECT_EQ(h.session().polygonSides(), 8);
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.session().sketch().lines().size(), 14u);
    const auto regions = doc::sketchRegions(h.session().sketch());
    ASSERT_TRUE(regions.ok());
    ASSERT_EQ(regions.value().size(), 2u);
    double octagon = 0;
    for (const auto& r : regions.value())
        if (std::abs(r.area - hexagon) > 1e-6)
            octagon = r.area;
    EXPECT_NEAR(octagon, 8 * 100 * std::tan(kPi / 8), 1e-6);
    EXPECT_EQ(h.session().polygonSides(), 8) << "remembered for the next polygon";

    // Extrude the hexagon 5 mm.
    h.controller.finishSketch();
    h.click(h.controller.camera().project({0, 1, 0}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.setValueText("5"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), hexagon * 5, 1e-4);
    EXPECT_TRUE(h.messages.empty());
}

// A "D": a line, a tangent half circle back over it, two lines closing it.
TEST(SketchInteraction, TangentArcContinuesALine)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({20, 0}));
    h.controller.keyPress(Key::Escape);

    h.controller.setSketchTool(SketchTool::TangentArc);
    h.click(h.sketchScreen({10, 10})); // not the end of a curve
    EXPECT_FALSE(h.session().isDrawing());
    ASSERT_FALSE(h.messages.empty());
    h.messages.clear();
    h.click(h.sketchScreen({20, 0})); // the line's end
    ASSERT_TRUE(h.session().isDrawing());
    h.move(h.sketchScreen({20, 20}));
    // Straight ahead is no arc: the preview needs the pointer off the line's direction.
    h.click(h.sketchScreen({20, 20}));
    const sketch::Sketch& s = h.session().sketch();
    ASSERT_EQ(s.arcs().size(), 1u);
    const auto& [arcId, arc] = *s.arcs().begin();
    EXPECT_NEAR(s.arcRadius(arcId), 10.0, 1e-9);
    EXPECT_NEAR((s.point(arc.center)->position - Vec2{20, 10}).length(), 0.0, 1e-9);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Tangent), 1u);
    EXPECT_TRUE(h.session().isDrawing()) << "the chain continues from the arc's end";
    h.controller.keyPress(Key::Escape);
    EXPECT_FALSE(h.session().isDrawing());

    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({20, 20}));
    h.click(h.sketchScreen({0, 20}));
    h.click(h.sketchScreen({0, 0}));
    EXPECT_NEAR(largestRegion(h.session().sketch()), 400 + 50 * kPi, 1e-6);

    h.controller.finishSketch();
    h.click(h.controller.camera().project({10, 10, 0}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.setValueText("10"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), (400 + 50 * kPi) * 10, 1e-3);
    EXPECT_TRUE(h.messages.empty());
}

// A typed radius, then an S-bend continuing the first arc.
TEST(SketchInteraction, TangentArcTypedRadiusAndChain)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({20, 0}));
    h.controller.keyPress(Key::Escape);
    h.controller.setSketchTool(SketchTool::TangentArc);
    h.click(h.sketchScreen({20, 0}));
    h.move(h.sketchScreen({27, 4})); // up and to the left of the line's direction: turns left
    h.type("5");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.session().sketch().arcs().size(), 1u);
    {
        const sketch::Sketch& s = h.session().sketch();
        const auto& [id, arc] = *s.arcs().begin();
        EXPECT_NEAR(s.arcRadius(id), 5.0, 1e-9);
        EXPECT_NEAR((s.point(arc.center)->position - Vec2{20, 5}).length(), 0.0, 1e-9);
    }
    EXPECT_EQ(h.count(sketch::ConstraintKind::Radius), 1u);
    // The chain goes on from the arc's end, heading along the arc: turn right now.
    ASSERT_TRUE(h.session().isDrawing());
    const Vec2 bendStart = h.session().sketch().point(h.session().sketch().arcs().begin()->second.end)->position;
    const Vec2 heading = Vec2{-(bendStart.y - 5), bendStart.x - 20} * (1.0 / 5.0);
    const Vec2 right{heading.y, -heading.x};
    h.click(h.sketchScreen(bendStart + right * 12));
    const sketch::Sketch& s = h.session().sketch();
    ASSERT_EQ(s.arcs().size(), 2u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Tangent), 2u);
    // The two centers lie on one line through the shared point, on opposite sides.
    std::vector<Vec2> centers;
    for (const auto& [id, arc] : s.arcs())
        centers.push_back(s.point(arc.center)->position);
    const Vec2 joint = bendStart;
    EXPECT_NEAR((centers[0] - joint).length() + (centers[1] - joint).length(), (centers[0] - centers[1]).length(), 1e-6);
    EXPECT_EQ(s.solveReport().conflicting.size(), 0u);
    EXPECT_TRUE(h.messages.empty());
}

// Only a single profile curve ending at the point is continued: never a
// construction curve ending there too, and never a corner of two sides.
TEST(SketchInteraction, TangentArcStartsOnAProfileCurveNotAGuide)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    // A center rectangle: two of its corners also end the construction diagonal.
    h.controller.setSketchTool(SketchTool::CenterRectangle);
    h.click(h.sketchScreen({0, 0}));
    h.move(h.sketchScreen({14, 7}));
    h.type("40");
    h.session().focusNextInput();
    h.type("20");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.session().sketch().lines().size(), 5u);
    h.controller.setSketchTool(SketchTool::TangentArc);
    h.click(h.sketchScreen({20, 10})); // a corner: two sides and the diagonal end there
    EXPECT_FALSE(h.session().isDrawing()) << "which side to continue is unclear";
    ASSERT_EQ(h.messages.size(), 1u);
    EXPECT_NE(h.messages[0].find("free end"), std::string::npos) << h.messages[0];
    EXPECT_TRUE(h.session().sketch().arcs().empty());
    h.messages.clear();

    // A line, then a construction line drawn on from its end (newer than the line).
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({30, -30}));
    h.click(h.sketchScreen({50, -30}));
    h.click(h.sketchScreen({65, -40}));
    h.controller.keyPress(Key::Escape);
    sketch::EntityId profileLine = sketch::kNoEntity, guide = sketch::kNoEntity;
    for (const auto& [id, l] : h.session().sketch().lines()) {
        const Vec2 end = h.session().sketch().point(l.end)->position;
        if ((end - Vec2{50, -30}).length() < 1e-9)
            profileLine = id;
        if ((end - Vec2{65, -40}).length() < 1e-9)
            guide = id;
    }
    ASSERT_NE(profileLine, sketch::kNoEntity);
    ASSERT_NE(guide, sketch::kNoEntity);
    ASSERT_GT(guide, profileLine);
    h.controller.setSketchTool(SketchTool::Select);
    h.click(h.sketchScreen({57.5, -35}));
    ASSERT_EQ(h.session().selection().size(), 1u);
    ASSERT_EQ(h.session().selection().front(), guide);
    ASSERT_TRUE(h.session().triggerAction("construction").ok());

    h.controller.setSketchTool(SketchTool::TangentArc);
    h.click(h.sketchScreen({50, -30}));
    ASSERT_TRUE(h.session().isDrawing());
    h.move(h.sketchScreen({50, -10}));
    h.click(h.sketchScreen({50, -10}));
    h.controller.keyPress(Key::Escape);
    const sketch::Sketch& s = h.session().sketch();
    ASSERT_EQ(s.arcs().size(), 1u);
    const auto& [arcId, arc] = *s.arcs().begin();
    // Heading on along +x and turning left: a half circle around (50, -20).
    EXPECT_NEAR(s.arcRadius(arcId), 10.0, 1e-9);
    EXPECT_NEAR((s.point(arc.center)->position - Vec2{50, -20}).length(), 0.0, 1e-9);
    bool tangentToLine = false;
    for (const auto& [id, c] : s.constraints())
        tangentToLine = tangentToLine || (c.kind == sketch::ConstraintKind::Tangent && c.a == profileLine && c.b == arcId);
    EXPECT_TRUE(tangentToLine) << "tangent to the profile line, not the construction line";
    EXPECT_TRUE(h.messages.empty());
}

namespace {
std::vector<SketchLabel> constraintIcons(Harness& h)
{
    std::vector<SketchLabel> icons;
    for (const auto& label : h.session().labels(h.controller.camera()))
        if (label.kind == SketchLabel::Kind::Constraint)
            icons.push_back(label);
    return icons;
}
} // namespace

TEST(SketchInteraction, ConstraintIconsSelectAndDelete)
{
    Harness h;
    rectangle40x20(h);
    h.session().setTool(SketchTool::Select);
    auto icons = constraintIcons(h);
    ASSERT_EQ(icons.size(), 4u) << "two H and two V";
    std::size_t horizontal = 0;
    for (const auto& icon : icons) {
        horizontal += icon.text == "H" ? 1 : 0;
        EXPECT_EQ(h.session().sketch().constraint(icon.constraint)->kind,
                  icon.text == "H" ? sketch::ConstraintKind::Horizontal : sketch::ConstraintKind::Vertical);
    }
    EXPECT_EQ(horizontal, 2u);
    // Beside their lines, outside the rectangle, and apart from each other.
    for (std::size_t i = 0; i < icons.size(); ++i) {
        const auto local = h.controller.sketchSession()->sketch().plane().intersect(h.controller.camera().rayAt(icons[i].screen));
        ASSERT_TRUE(local.has_value());
        EXPECT_FALSE(local->x > 0 && local->x < 40 && local->y > 0 && local->y < 20) << icons[i].text << " inside";
        for (std::size_t j = i + 1; j < icons.size(); ++j)
            EXPECT_GE((icons[i].screen - icons[j].screen).length(), 18.0);
    }
    // Clear of every line (a glyph's tap target must not hide a curve); further in the touch layout.
    auto nearestLine = [&](Vec2 p) {
        double best = 1e9;
        const sketch::Sketch& sk = h.session().sketch();
        for (const auto& [id, l] : sk.lines())
            best = std::min(best, distanceToSegment2D(p, h.sketchScreen(sk.point(l.start)->position),
                                                      h.sketchScreen(sk.point(l.end)->position)));
        return best;
    };
    for (const auto& icon : icons)
        EXPECT_GE(nearestLine(icon.screen), 12.0 - 1e-9);
    h.controller.setTouchLayout(true);
    for (const auto& icon : constraintIcons(h))
        EXPECT_GE(nearestLine(icon.screen), 20.0 - 1e-9);
    h.controller.setTouchLayout(false);

    // Select a horizontal constraint through its icon: only Delete is offered.
    sketch::EntityId bottom = sketch::kNoEntity;
    double lowest = -1e9; // screen y grows downwards
    for (const auto& icon : icons)
        if (icon.text == "H" && icon.screen.y > lowest) {
            lowest = icon.screen.y;
            bottom = icon.constraint;
        }
    h.session().select(bottom, false);
    ASSERT_EQ(h.session().selection().size(), 1u);
    const auto actions = h.session().contextActions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].id, "delete");
    bool shownSelected = false;
    for (const auto& icon : constraintIcons(h))
        shownSelected = shownSelected || (icon.constraint == bottom && icon.selected);
    EXPECT_TRUE(shownSelected);
    // Selecting geometry drops the constraint from the selection (they are never mixed).
    const int dofBefore = h.session().sketch().solveReport().degreesOfFreedom;
    ASSERT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.session().sketch().constraint(bottom), nullptr);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Horizontal), 1u);
    EXPECT_EQ(h.session().sketch().lines().size(), 4u) << "the geometry stays";
    EXPECT_EQ(h.session().sketch().solveReport().degreesOfFreedom, dofBefore + 1);
    EXPECT_EQ(constraintIcons(h).size(), 3u);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.count(sketch::ConstraintKind::Horizontal), 2u);

    h.session().select(icons[0].constraint, false);
    h.click(h.sketchScreen({20, 0}), true); // a line, shift-added: replaces the constraint selection
    ASSERT_EQ(h.session().selection().size(), 1u);
    EXPECT_NE(h.session().sketch().line(h.session().selection().front()), nullptr);

    // Hidden while a shape is being drawn.
    h.session().setTool(SketchTool::Line);
    h.click(h.sketchScreen({60, 0}));
    EXPECT_TRUE(constraintIcons(h).empty());
    h.controller.keyPress(Key::Escape);
    EXPECT_EQ(constraintIcons(h).size(), 4u);
}

namespace {
void tap(Harness& h, Vec2 p)
{
    auto e = Harness::at(p);
    e.device = PointerDevice::Touch;
    h.controller.pointerPress(e);
    h.controller.pointerRelease(e);
}

// The bottom side of rectangle40x20 and its H glyph.
std::pair<sketch::EntityId, SketchLabel> bottomSideAndGlyph(Harness& h)
{
    sketch::EntityId bottom = sketch::kNoEntity;
    const sketch::Sketch& s = h.session().sketch();
    for (const auto& [id, l] : s.lines())
        if (std::abs(s.point(l.start)->position.y) < 1e-9 && std::abs(s.point(l.end)->position.y) < 1e-9)
            bottom = id;
    SketchLabel glyph;
    for (const auto& icon : constraintIcons(h))
        if (s.constraint(icon.constraint)->kind == sketch::ConstraintKind::Horizontal && s.constraint(icon.constraint)->a == bottom)
            glyph = icon;
    return {bottom, glyph};
}
} // namespace

// Glyph taps are resolved by the session: a point or curve within pick reach
// wins, then the glyph (with its larger target); drawing tools ignore glyphs.
TEST(SketchInteraction, ConstraintGlyphTapsNeverHideGeometry)
{
    Harness h;
    rectangle40x20(h);
    h.controller.setSketchTool(SketchTool::Select);
    sketch::EntityId bottom = sketch::kNoEntity;
    SketchLabel glyph;
    std::tie(bottom, glyph) = bottomSideAndGlyph(h);
    ASSERT_NE(bottom, sketch::kNoEntity);
    ASSERT_NE(glyph.constraint, sketch::kNoEntity);
    const sketch::Sketch& s = h.session().sketch();
    auto footOnBottom = [&](Vec2 p) {
        const Vec2 a = h.sketchScreen(s.point(s.line(bottom)->start)->position);
        const Vec2 b = h.sketchScreen(s.point(s.line(bottom)->end)->position);
        const Vec2 d = b - a;
        return a + d * std::clamp((p - a).dot(d) / d.dot(d), 0.0, 1.0);
    };
    Vec2 foot = footOnBottom(glyph.screen);
    Vec2 away = (glyph.screen - foot) * (1.0 / (glyph.screen - foot).length());
    EXPECT_GE((glyph.screen - foot).length(), 12.0 - 1e-9);

    // Hovering the glyph lights it; a click 6 px off the line, on the glyph's
    // side and inside its 24 px target, still picks the line.
    h.move(glyph.screen);
    bool hot = false;
    for (const auto& icon : constraintIcons(h))
        hot = hot || (icon.constraint == glyph.constraint && icon.hot);
    EXPECT_TRUE(hot);
    const Vec2 nearLine = foot + away * 6;
    ASSERT_LE(std::max(std::abs(nearLine.x - glyph.screen.x), std::abs(nearLine.y - glyph.screen.y)),
              h.session().glyphTapHalfSize());
    h.click(nearLine);
    ASSERT_EQ(h.session().selection().size(), 1u);
    EXPECT_EQ(h.session().selection().front(), bottom);
    for (const auto& icon : constraintIcons(h))
        EXPECT_FALSE(icon.constraint == glyph.constraint && icon.hot) << "the line under the pointer wins";
    h.click(glyph.screen);
    ASSERT_EQ(h.session().selection().size(), 1u);
    EXPECT_EQ(h.session().selection().front(), glyph.constraint);
    h.controller.keyPress(Key::Escape);

    // The touch layout: a 40 px target, further out; a finger 15 px off the line still gets the line.
    h.controller.setTouchLayout(true);
    EXPECT_TRUE(h.session().largeTargets());
    EXPECT_EQ(h.session().glyphTapHalfSize(), 20.0);
    std::tie(bottom, glyph) = bottomSideAndGlyph(h);
    ASSERT_NE(glyph.constraint, sketch::kNoEntity);
    foot = footOnBottom(glyph.screen);
    away = (glyph.screen - foot) * (1.0 / (glyph.screen - foot).length());
    EXPECT_GE((glyph.screen - foot).length(), 20.0);
    tap(h, foot + away * 15);
    ASSERT_EQ(h.session().selection().size(), 1u);
    EXPECT_EQ(h.session().selection().front(), bottom);
    tap(h, glyph.screen + Vec2{0, 1} * (away.y > 0 ? 12.0 : -12.0)); // the far half of the glyph's target
    ASSERT_EQ(h.session().selection().size(), 1u) << "the constraint replaces the line (never mixed)";
    EXPECT_EQ(h.session().selection().front(), glyph.constraint);
    ASSERT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.session().sketch().constraint(glyph.constraint), nullptr);
    EXPECT_TRUE(h.controller.undo());

    // A drawing tool ignores glyphs: the click places the line's first point.
    h.controller.setSketchTool(SketchTool::Line);
    const auto icons = constraintIcons(h);
    ASSERT_FALSE(icons.empty()) << "shown while the tool waits for its first click";
    h.click(icons.front().screen);
    EXPECT_TRUE(h.session().isDrawing());
    EXPECT_TRUE(h.session().selection().empty());
    h.controller.keyPress(Key::Escape);
    h.controller.setTouchLayout(false);
    EXPECT_FALSE(h.session().largeTargets());

    // A session started in the touch layout has it from the start (as on a tablet).
    h.controller.finishSketch();
    h.controller.setTouchLayout(true);
    ASSERT_TRUE(h.controller.startSketch().ok());
    ASSERT_NE(h.controller.sketchSession(), nullptr);
    EXPECT_TRUE(h.session().largeTargets());
}

// Glyphs keep clear of the dimension labels (pills about 7.5 px per
// character plus 16 px wide, 24 px high), at every zoom.
TEST(SketchInteraction, ConstraintGlyphsKeepClearOfDimensionLabels)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.click(h.sketchScreen({0, 0}));
    h.move(h.sketchScreen({30, 12}));
    h.type("127.25"); // a long label
    h.session().focusNextInput();
    h.type("20");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.setSketchTool(SketchTool::Select);
    auto characters = [](const std::string& text) {
        return std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
    };
    const Vec2 center = h.sketchScreen({63.6, 10});
    h.controller.pinch(center, 0.15);
    int close = 0, shown = 0;
    for (int step = 0; step < 50; ++step) {
        h.controller.pinch(center, 1.07);
        const auto labels = h.session().labels(h.controller.camera());
        for (const auto& glyph : labels) {
            if (glyph.kind != SketchLabel::Kind::Constraint)
                continue;
            ++shown;
            const double glyphHalf = std::max(18.0, 7.5 * double(characters(glyph.text)) + 8) / 2;
            for (const auto& dimension : labels) {
                if (dimension.kind != SketchLabel::Kind::Dimension)
                    continue;
                const double halfWidth = (7.5 * double(characters(dimension.text)) + 16) / 2;
                const double dx = std::abs(glyph.screen.x - dimension.screen.x), dy = std::abs(glyph.screen.y - dimension.screen.y);
                close += dx < halfWidth + glyphHalf + 20 && dy < 12 + glyphHalf + 20 ? 1 : 0;
                EXPECT_TRUE(dx >= halfWidth + glyphHalf || dy >= 12 + glyphHalf)
                    << "step " << step << ": glyph " << glyph.text << " on label " << dimension.text;
            }
        }
    }
    EXPECT_GT(shown, 40) << "glyphs are shown at most zooms";
    EXPECT_GT(close, 0) << "some glyphs sit right beside a label (the check is not vacuous)";
}

namespace {
bool offers(Harness& h, const std::string& id)
{
    for (const auto& action : h.session().contextActions())
        if (action.id == id)
            return true;
    return false;
}
} // namespace

// Half a 20 x 20 square against a construction center line, mirrored into a
// closed square, extruded.
TEST(SketchInteraction, MirrorAcrossAClickedLine)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, -5}));
    h.click(h.sketchScreen({0, 25}));
    h.controller.keyPress(Key::Escape);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({10, 0}));
    h.click(h.sketchScreen({10, 20}));
    h.click(h.sketchScreen({0, 20}));
    h.controller.keyPress(Key::Escape);
    h.controller.setSketchTool(SketchTool::Select);
    h.click(h.sketchScreen({0, 10})); // the center line (away from its points): make it construction
    ASSERT_TRUE(h.session().triggerAction("construction").ok());
    h.controller.keyPress(Key::Escape); // clear the selection
    for (const Vec2 mid : {Vec2{5, 0}, Vec2{10, 10}, Vec2{5, 20}})
        h.click(h.sketchScreen(mid), true);
    ASSERT_EQ(h.session().selection().size(), 3u);
    ASSERT_TRUE(offers(h, "mirror"));
    ASSERT_TRUE(h.session().triggerAction("mirror").ok());
    EXPECT_TRUE(h.session().isMirroring());
    EXPECT_NE(h.session().hintText().find("line to mirror across"), std::string::npos);
    h.move(h.sketchScreen({0, 23}));
    bool previewed = false;
    for (const auto& line : h.session().renderData(h.controller.camera()).lines)
        previewed = previewed || line.style == SketchStyle::Preview;
    EXPECT_TRUE(previewed) << "the mirror image shows before the click";
    h.click(h.sketchScreen({0, 23}));
    EXPECT_FALSE(h.session().isMirroring());
    const sketch::Sketch& s = h.session().sketch();
    EXPECT_EQ(s.lines().size(), 7u);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Symmetric), 2u);
    EXPECT_NEAR(largestRegion(s), 400.0, 1e-6);
    EXPECT_TRUE(h.messages.empty());

    // The glyph of a symmetric pair sits on the axis.
    bool glyph = false;
    for (const auto& icon : constraintIcons(h))
        glyph = glyph || icon.text == "\xE2\x86\x94";
    EXPECT_TRUE(glyph);

    // Esc leaves mirroring without a change.
    h.click(h.sketchScreen({5, 0}));
    ASSERT_TRUE(h.session().triggerAction("mirror").ok());
    h.controller.keyPress(Key::Escape);
    EXPECT_FALSE(h.session().isMirroring());
    EXPECT_EQ(h.session().sketch().lines().size(), 7u);

    h.controller.finishSketch();
    h.click(h.controller.camera().project({3, 10, 0}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.setValueText("5"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 2000, 1e-4);
}

TEST(SketchInteraction, LinearAndCircularPatternOfAHole)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Circle);
    h.click(h.sketchScreen({10, 10}));
    h.move(h.sketchScreen({13, 10}));
    h.type("6");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    h.controller.setSketchTool(SketchTool::Select);
    h.click(h.sketchScreen({13, 10}));
    ASSERT_EQ(h.session().selection().size(), 1u);
    ASSERT_TRUE(offers(h, "pattern"));
    ASSERT_TRUE(h.session().triggerAction("pattern").ok());
    ASSERT_TRUE(h.session().isPatterning());
    ASSERT_TRUE(h.session().counter().has_value());
    EXPECT_EQ(h.session().counter()->text, "3 in total");
    EXPECT_TRUE(h.session().stepCounter(+1));
    // Where the next hole goes: 15 to the right (kept straight).
    h.click(h.sketchScreen({25, 10.3}));
    EXPECT_NEAR(h.session().patternLayout().step.x, 15.0, 1e-9);
    EXPECT_NEAR(h.session().patternLayout().step.y, 0.0, 1e-12);
    h.type("12"); // the spacing
    EXPECT_NEAR(h.session().patternLayout().step.x, 12.0, 1e-9);
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_FALSE(h.session().isPatterning());
    const sketch::Sketch& s = h.session().sketch();
    ASSERT_EQ(s.circles().size(), 4u);
    std::vector<double> xs;
    for (const auto& [id, c] : s.circles())
        xs.push_back(s.point(c.center)->position.x);
    std::sort(xs.begin(), xs.end());
    EXPECT_NEAR(xs[3], 46.0, 1e-9);
    EXPECT_EQ(h.count(sketch::ConstraintKind::Equal), 3u);
    const auto regions = doc::sketchRegions(s);
    ASSERT_TRUE(regions.ok());
    EXPECT_EQ(regions.value().size(), 4u);
    // One diameter edit resizes all four.
    sketch::EntityId diameter = sketch::kNoEntity;
    for (const auto& [id, c] : s.constraints())
        if (c.kind == sketch::ConstraintKind::Diameter)
            diameter = id;
    ASSERT_NE(diameter, sketch::kNoEntity);
    EXPECT_EQ(h.session().setDimension(diameter, "8"), "");
    for (const auto& [id, c] : h.session().sketch().circles())
        EXPECT_NEAR(c.radius, 4.0, 1e-9);

    // Circular: the first hole three times over half a turn about the origin.
    h.click(h.sketchScreen({14, 10})); // the first hole
    ASSERT_TRUE(h.session().triggerAction("pattern").ok());
    ASSERT_TRUE(h.session().triggerAction("pattern:circular").ok());
    EXPECT_TRUE(h.session().patternLayout().circular);
    EXPECT_EQ(h.session().counter()->value, 6);
    h.click(h.sketchScreen({0, 0})); // the center: the origin
    h.type("180");
    h.session().focusNextInput();
    h.type("3");
    ASSERT_TRUE(h.session().triggerAction("apply").ok());
    EXPECT_EQ(h.session().sketch().circles().size(), 6u);
    bool opposite = false, above = false;
    for (const auto& [id, c] : h.session().sketch().circles()) {
        const Vec2 p = h.session().sketch().point(c.center)->position;
        opposite = opposite || (p - Vec2{-10, -10}).length() < 1e-9;
        above = above || (p - Vec2{-10, 10}).length() < 1e-9;
    }
    EXPECT_TRUE(above) << "90 degrees on";
    EXPECT_TRUE(opposite) << "180 degrees on";
    EXPECT_TRUE(h.messages.empty());
}

// A right triangle whose hypotenuse angle is dimensioned, then changed.
TEST(SketchInteraction, AngleDimensionBetweenTwoLines)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click(h.sketchScreen({0, 0}));
    h.click(h.sketchScreen({20, 0}));
    h.click(h.sketchScreen({20, 15}));
    h.click(h.sketchScreen({0, 0})); // closes the triangle
    h.controller.setSketchTool(SketchTool::Select);
    h.click(h.sketchScreen({10, 0}));
    ASSERT_TRUE(h.session().triggerAction("length").ok()); // the base: 20
    h.click(h.sketchScreen({10, 7.5}), true); // and the hypotenuse
    ASSERT_EQ(h.session().selection().size(), 2u);
    ASSERT_TRUE(offers(h, "angle"));
    ASSERT_TRUE(h.session().triggerAction("angle").ok());
    sketch::EntityId angle = sketch::kNoEntity;
    for (const auto& [id, c] : h.session().sketch().constraints())
        if (c.kind == sketch::ConstraintKind::Angle)
            angle = id;
    ASSERT_NE(angle, sketch::kNoEntity);
    EXPECT_EQ(h.session().sketch().solveReport().degreesOfFreedom, 0);
    std::string text;
    for (const auto& label : h.session().labels(h.controller.camera()))
        if (label.kind == SketchLabel::Kind::Dimension && label.constraint == angle)
            text = label.text;
    EXPECT_EQ(text, "36.87\xC2\xB0");
    EXPECT_NE(h.session().setDimension(angle, "180"), "") << "not a corner";
    EXPECT_EQ(h.session().setDimension(angle, "45"), "");
    EXPECT_NEAR(largestRegion(h.session().sketch()), 200.0, 1e-6) << "20 x 20 / 2";
    EXPECT_EQ(h.session().setDimension(angle, "60\xC2\xB0"), "");
    EXPECT_NEAR(largestRegion(h.session().sketch()), 20 * 20 * std::sqrt(3.0) / 2, 1e-6);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(largestRegion(h.session().sketch()), 200.0, 1e-6);
    EXPECT_TRUE(h.messages.empty());
}

// A circle drawn on a box's top plane but beside the box floats in front of
// the box's far edges: in both projections a click on it selects its
// profile (in perspective an edge behind it on screen used to win).
TEST(SketchInteraction, ProfileBesideABoxWinsOverAnEdgeBehindIt)
{
    for (const auto projection : {Camera::Projection::Orthographic, Camera::Projection::Perspective}) {
        Harness h;
        h.controller.setProjection(projection);
        ASSERT_TRUE(h.controller.createBox(20).ok());
        h.controller.skipAnimation();
        h.controller.fitAll(false);
        h.click(h.controller.camera().project({0, 0, 20}));
        ASSERT_TRUE(h.controller.startSketch().ok());
        h.controller.skipAnimation();
        h.controller.setSketchTool(SketchTool::Circle);
        h.click(h.controller.camera().project({17, 0, 20}));
        h.move(h.controller.camera().project({19, 0, 20}));
        h.type("6");
        ASSERT_TRUE(h.controller.keyPress(Key::Enter));
        h.controller.finishSketch();
        h.controller.skipAnimation();
        h.controller.setStandardView(StandardView::Isometric, false);
        h.controller.fitAll(false);
        h.click(h.controller.camera().project({17, 0, 20}));
        ASSERT_NE(h.controller.operation(), nullptr);
        EXPECT_EQ(h.controller.operation()->title(), "Extrude");
    }
}
