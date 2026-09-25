// Sketching driven through the interaction layer with synthetic input: the
// same code paths the UI uses.
#include "commands/Command.h"
#include "document/Document.h"
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
