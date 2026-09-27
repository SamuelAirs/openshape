// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Headless end-to-end tests of the interaction layer: the same code paths the
// desktop UI drives, fed with synthetic pointer/keyboard events.
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

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
    }

    const doc::Body& body() const { return *document.bodies().front(); }
    double height() const { return geom::boundingBox(body().shape()).size().z; }

    static PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left, PointerDevice device = PointerDevice::Mouse)
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        e.device = device;
        return e;
    }

    void hover(Vec2 p) { controller.pointerMove(at(p, PointerButton::None)); }
    void clickAt(Vec2 p, PointerDevice device = PointerDevice::Mouse)
    {
        controller.pointerPress(at(p, PointerButton::Left, device));
        controller.pointerRelease(at(p, PointerButton::Left, device));
    }
    void drag(Vec2 from, Vec2 to, PointerButton button = PointerButton::Left, int steps = 8)
    {
        controller.pointerPress(at(from, button));
        for (int i = 1; i <= steps; ++i)
            controller.pointerMove(at(from + (to - from) * (double(i) / steps), button));
        controller.pointerRelease(at(to, button));
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }

    int topFace() const
    {
        for (int i = 0; i < body().shape().faceCount(); ++i)
            if (geom::faceInfo(body().shape(), i)->normal.z > 0.999)
                return i;
        return -1;
    }
};

} // namespace

// Milestone 0 acceptance test, driven through the interaction layer exactly as
// the UI does. Steps refer to the acceptance list in ROADMAP.md.
TEST(Milestone0, AcceptanceScript)
{
    Harness h;
    // 2. Create 20 mm cube.
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(h.height(), 20.0, 1e-6);

    // 3. Orbit around the cube (drag in empty space).
    const double yawBefore = h.controller.camera().yaw;
    h.drag({60, 60}, {160, 90});
    EXPECT_NE(h.controller.camera().yaw, yawBefore);
    EXPECT_TRUE(h.controller.selection().empty()) << "a drag must not select";
    h.controller.setStandardView(StandardView::Isometric, false);

    // 4-5. Hover over the top face: it becomes the hover target.
    const Vec2 topCenter = h.screen({0, 0, 20});
    h.hover(topCenter);
    ASSERT_EQ(h.controller.hover().kind, sel::PickKind::Face);
    EXPECT_EQ(h.controller.hover().index, h.topFace());
    ASSERT_EQ(h.controller.renderScene().bodies.size(), 1u);
    EXPECT_EQ(h.controller.renderScene().bodies[0].hoverFace, h.topFace());

    // 6-8. Click: face selected, arrow manipulator appears along +Z.
    h.clickAt(topCenter);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Face);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Push/Pull");
    auto scene = h.controller.renderScene();
    ASSERT_EQ(scene.arrows.size(), 1u);
    EXPECT_NEAR(scene.arrows[0].direction.z, 1.0, 1e-9);
    EXPECT_FALSE(h.controller.contextActions().empty());
    // The value is the cube's height (the distance to the bottom face), shown
    // as a dimension line through the part.
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Height");
    EXPECT_NEAR(h.controller.operation()->value(), 20.0, 1e-9);
    EXPECT_EQ(h.controller.operationValueText(), "20.00 mm");
    ASSERT_EQ(scene.sketches.size(), 1u);
    ASSERT_EQ(scene.sketches[0].lines.size(), 1u);
    EXPECT_NEAR(scene.sketches[0].lines[0].a.z, 0.0, 1e-9);
    EXPECT_NEAR(scene.sketches[0].lines[0].b.z, 20.0, 1e-9);

    // 9-10. Drag the arrow upward: preview updates, document does not change yet.
    const double px = h.controller.camera().pixelSize({0, 0, 20});
    const Vec2 grab = h.screen({0, 0, 20 + 40 * px});
    h.drag(grab, grab + Vec2{0, -80});
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_GT(h.controller.operation()->value(), 21.0);
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    EXPECT_TRUE(h.controller.renderScene().bodies[0].isPreview);
    EXPECT_NEAR(h.height(), 20.0, 1e-6) << "preview must not modify the document";
    EXPECT_EQ(h.stack.size(), 1u);

    // 11. Type the exact new height: no arithmetic needed.
    EXPECT_EQ(h.controller.setValueText("35"), "");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 35.0);
    EXPECT_EQ(h.controller.operationValueText(), "35.00 mm");

    // 12. Enter commits: body height becomes exactly 35 mm.
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_NEAR(h.height(), 35.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.body().shape()), 20.0 * 20.0 * 35.0, 1e-4);
    // The pushed face stays selected with a fresh manipulator for the next push.
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].index, h.topFace());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_NEAR(h.controller.operation()->value(), 35.0, 1e-9);
    EXPECT_NEAR(h.controller.operation()->anchor().z, 35.0, 1e-6);

    // 13-14. Undo restores 20 mm.
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.height(), 20.0, 1e-6);

    // 15-16. Redo returns to 35 mm.
    EXPECT_TRUE(h.controller.redo());
    EXPECT_NEAR(h.height(), 35.0, 1e-6);
    EXPECT_TRUE(h.messages.empty());
}

TEST(Interaction, EscapeCancelsPreviewThenSelection)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_NE(h.controller.operation(), nullptr);
    h.controller.setValueText("7");
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    h.controller.keyPress(Key::Escape);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_FALSE(h.controller.operation()->hasPreview());
    h.controller.keyPress(Key::Escape);
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_NEAR(h.height(), 20.0, 1e-6);
}

TEST(Interaction, ClickingEmptySpaceCommitsPendingValue)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    h.controller.setValueText("-5");
    h.clickAt({30, 30});
    EXPECT_NEAR(h.height(), 15.0, 1e-6);
    EXPECT_TRUE(h.controller.selection().empty());
}

// A right click is not a selection click: it must neither select nor apply a
// pending value (users right-click expecting a menu, not a commit).
TEST(Interaction, RightClickDoesNotSelectOrCommit)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    h.controller.setValueText("+5"); // 5 mm taller than the 20 mm it shows
    const std::size_t steps = h.stack.size();
    h.controller.pointerPress(Harness::at({30, 30}, PointerButton::Right));
    h.controller.pointerRelease(Harness::at({30, 30}, PointerButton::Right));
    EXPECT_EQ(h.stack.size(), steps);
    EXPECT_NEAR(h.height(), 20.0, 1e-6);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_TRUE(h.controller.operation()->canCommit());
    h.clickAt({30, 30}); // a left click elsewhere still applies it
    EXPECT_NEAR(h.height(), 25.0, 1e-6);
}

namespace {
Uuid addBox(Harness& h, const std::string& name, Vec3 origin, Vec3 size)
{
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = origin;
    box->size = size;
    EXPECT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>(name, std::move(box)), h.document).ok());
    h.controller.documentChanged();
    return h.document.bodies().back()->id();
}
} // namespace

// Bodies picked in the model panel (Shift adds) show clear boolean actions;
// Swap flips which body is cut; the tool body is consumed.
TEST(Interaction, SelectBodiesFromPanelAndSubtract)
{
    Harness h;
    const Uuid a = addBox(h, "Body 1", {0, 0, 0}, {20, 20, 20});
    const Uuid b = addBox(h, "Body 2", {10, 10, 10}, {20, 20, 20});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    EXPECT_EQ(h.controller.selectionSummary(), "Body 1 + Body 2");
    bool subtractLabel = false, swap = false;
    for (const auto& action : h.controller.contextActions()) {
        subtractLabel = subtractLabel || (action.id == "subtract" && action.label == "Subtract Body 2");
        swap = swap || action.id == "swap";
    }
    EXPECT_TRUE(subtractLabel);
    ASSERT_TRUE(swap);
    ASSERT_TRUE(h.controller.triggerAction("swap").ok());
    EXPECT_EQ(h.controller.selection().items()[0].bodyId, b); // now Body 2 is kept
    ASSERT_TRUE(h.controller.triggerAction("swap").ok());

    ASSERT_TRUE(h.controller.runTool("subtract").ok());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 8000.0 - 1000.0, 1e-3);
    EXPECT_FALSE(h.document.body(b)->isVisible());
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 8000.0, 1e-3);
    EXPECT_TRUE(h.document.body(b)->isVisible());
}

TEST(Interaction, UnionOfThreeBodies)
{
    Harness h;
    const Uuid a = addBox(h, "Body 1", {0, 0, 0}, {10, 10, 10});
    const Uuid b = addBox(h, "Body 2", {5, 0, 0}, {10, 10, 10});
    const Uuid c = addBox(h, "Body 3", {10, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.selectBody(b, true).ok());
    ASSERT_TRUE(h.controller.selectBody(c, true).ok());
    ASSERT_TRUE(h.controller.runTool("union").ok());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 20.0 * 10 * 10, 1e-3);
    EXPECT_EQ(h.document.body(a)->features().size(), 3u); // box + two combine steps
    EXPECT_FALSE(h.document.body(b)->isVisible());
    EXPECT_FALSE(h.document.body(c)->isVisible());
    EXPECT_TRUE(h.controller.undo()); // one undo step for the whole union
    EXPECT_EQ(h.document.body(a)->features().size(), 1u);
}

// Tools from the palette explain what to select instead of failing silently.
TEST(Interaction, ToolPaletteExplainsSelection)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    EXPECT_FALSE(h.controller.runTool("subtract").ok());
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("Select the body to keep"), std::string::npos);
    EXPECT_FALSE(h.controller.runTool("fillet").ok());
    EXPECT_NE(h.messages.back().find("edge"), std::string::npos);
}

// Hovering a step in the model panel highlights the faces it created or
// changed; hovering the body highlights all of it.
TEST(Interaction, HistoryHighlightShowsWhatAStepTouched)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({10, -10, 10})); // the front vertical edge of the centered cube
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Edge);
    h.controller.setValueText("3");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const doc::Body& body = h.body();
    const Uuid fillet = body.features().back()->id();

    auto highlighted = [&] {
        for (const auto& rb : h.controller.renderScene().bodies)
            if (rb.id == body.id())
                return rb.highlightFaces;
        return std::vector<int>{};
    };
    h.controller.setHistoryHighlight(fillet);
    const auto faces = highlighted();
    ASSERT_EQ(faces.size(), 1u); // the fillet surface only
    EXPECT_EQ(geom::faceInfo(body.shape(), faces.front())->kind, geom::SurfaceKind::Cylinder);
    h.controller.setHistoryHighlight(body.id());
    EXPECT_EQ(highlighted().size(), std::size_t(body.shape().faceCount()));
    h.controller.setHistoryHighlight(std::nullopt);
    EXPECT_TRUE(highlighted().empty());
}

// Align through the interaction layer: a small box's front face onto the top
// of a bigger box; Esc steps back; the step is an editable "Align" move.
TEST(Interaction, AlignFaceOntoAnotherBody)
{
    Harness h;
    const Uuid a = addBox(h, "Body 1", {0, 0, 0}, {10, 10, 10});
    const Uuid b = addBox(h, "Body 2", {40, 0, 0}, {20, 20, 20});
    h.controller.fitAll(false);
    h.clickAt(h.screen({5, 0, 5})); // front (-Y) face of the small box
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Face);
    ASSERT_TRUE(h.controller.runTool("align").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Align");
    EXPECT_FALSE(h.controller.operation()->prompt().empty());
    EXPECT_FALSE(h.controller.valueLabelPosition().has_value()); // no arrow until there is a target

    h.clickAt(h.screen({50, 10, 20})); // top face of the big box
    const auto* align = dynamic_cast<const AlignOperation*>(h.controller.operation());
    ASSERT_NE(align, nullptr);
    ASSERT_TRUE(align->hasTarget());
    EXPECT_TRUE(align->canCommit());
    EXPECT_EQ(h.document.body(a)->shape().faceCount(), 6); // preview only
    EXPECT_TRUE(h.controller.keyPress(Key::Escape));        // back to picking the target
    EXPECT_FALSE(dynamic_cast<const AlignOperation*>(h.controller.operation())->hasTarget());
    h.clickAt(h.screen({50, 10, 20}));
    ASSERT_TRUE(h.controller.commitOperation().ok());

    const auto bb = geom::boundingBox(h.document.body(a)->shape());
    EXPECT_NEAR(bb.min.z, 20.0, 1e-6);
    EXPECT_NEAR(bb.size().z, 10.0, 1e-6);
    EXPECT_NEAR(bb.center().x, 50.0, 1e-6);
    EXPECT_NEAR(bb.center().y, 10.0, 1e-6);
    const auto& step = *h.document.body(a)->features().back();
    EXPECT_EQ(step.name(), "Align");
    EXPECT_TRUE(step.parameter("angle").has_value());
    EXPECT_EQ(dynamic_cast<const AlignOperation*>(h.controller.operation()), nullptr); // Align is done
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(geom::boundingBox(h.document.body(a)->shape()).min.z, 0.0, 1e-6);
    (void)b;
}

// "Onto ground" lays a flat face on the XY plane where the body is.
TEST(Interaction, AlignOntoGround)
{
    Harness h;
    const Uuid a = addBox(h, "Body 1", {0, 0, 5}, {10, 10, 20}); // floating, standing up
    h.controller.fitAll(false);
    h.clickAt(h.screen({5, 0, 15})); // front (-Y) face
    ASSERT_TRUE(h.controller.triggerAction("align").ok());
    ASSERT_TRUE(h.controller.triggerAction("ground").ok());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const auto bb = geom::boundingBox(h.document.body(a)->shape());
    EXPECT_NEAR(bb.min.z, 0.0, 1e-6);  // lying on the ground
    EXPECT_NEAR(bb.size().z, 10.0, 1e-6); // the 10 mm depth is now its height
    EXPECT_NEAR(bb.size().y, 20.0, 1e-6);
}

// Rotation rings: dragging along the Z ring turns the body in 15 degree
// steps; a 30 x 10 footprint becomes 10 x 30 after a quarter turn.
TEST(Interaction, RotateBodyWithRing)
{
    Harness h;
    const Uuid a = addBox(h, "Body 1", {0, 0, 0}, {30, 10, 5});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("rotate").ok());
    const Operation* op = h.controller.operation();
    ASSERT_NE(op, nullptr);
    EXPECT_EQ(op->title(), "Rotate");
    ASSERT_EQ(op->ringCount(), 3);
    EXPECT_EQ(h.controller.renderScene().rings.size(), 3u);

    // Drag the Z ring a quarter turn through intermediate points on it,
    // grabbing at 45 degrees (the rings cross each other on the axes).
    const RingManipulator ring = op->ring(2);
    const Camera& cam = h.controller.camera();
    const double a0 = kPi / 4;
    h.controller.pointerPress(Harness::at(cam.project(ring.pointAt(cam, a0))));
    for (int i = 1; i <= 12; ++i)
        h.controller.pointerMove(Harness::at(cam.project(ring.pointAt(cam, a0 + kPi / 2 * i / 12.0))));
    h.controller.pointerRelease(Harness::at(cam.project(ring.pointAt(cam, a0 + kPi / 2))));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_NEAR(h.controller.operation()->value(), 90.0, 1e-9);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Angle Z");
    ASSERT_TRUE(h.controller.commitOperation().ok());

    const auto bb = geom::boundingBox(h.document.body(a)->shape());
    EXPECT_NEAR(bb.size().x, 10.0, 1e-6);
    EXPECT_NEAR(bb.size().y, 30.0, 1e-6);
    EXPECT_NEAR(bb.center().x, 15.0, 1e-6); // turned about its own center
    EXPECT_NEAR(bb.center().y, 5.0, 1e-6);
    bool detail = false;
    for (const auto& row : h.controller.historyRows())
        detail = detail || (row.name == "Rotate" && row.detail.find("about Z") != std::string::npos);
    EXPECT_TRUE(detail);

    // Typed angle on the X ring; switching rings starts from zero.
    ASSERT_NE(dynamic_cast<const RotateOperation*>(h.controller.operation()), nullptr); // still rotating
    EXPECT_EQ(h.controller.setValueText("45"), "");
    EXPECT_NEAR(h.controller.operation()->value(), 45.0, 1e-9);
    const_cast<Operation*>(h.controller.operation())->setActiveHandle(0);
    EXPECT_EQ(h.controller.operation()->value(), 0.0);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(geom::boundingBox(h.document.body(a)->shape()).size().x, 30.0, 1e-6);
}

// The ring keeps counting past half a turn, and snaps unless Alt is held.
TEST(Interaction, RingDragPastHalfTurn)
{
    Camera cam;
    cam.viewportSize = {1000, 800};
    cam.fit({-10, -10, -10}, {10, 10, 10});
    RingManipulator ring({0, 0, 0}, {0, 0, 1});
    ASSERT_TRUE(ring.hitTest(cam, cam.project(ring.pointAt(cam, 1.0)), 4.0).has_value());
    EXPECT_FALSE(ring.hitTest(cam, cam.project({0, 0, 0}), 4.0).has_value()); // the center is not the ring
    ring.beginDrag(cam, cam.project(ring.pointAt(cam, 0.0)), 0.0);
    double angle = 0;
    for (int i = 1; i <= 30; ++i)
        angle = ring.dragTo(cam, cam.project(ring.pointAt(cam, 1.5 * kPi * i / 30.0)));
    EXPECT_NEAR(angle, 1.5 * kPi, 1e-6);
}

// Mirror a cube across its own +X face: one 40 x 20 x 20 block.
TEST(Interaction, MirrorAcrossAFace)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0)..(10,10,20)
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.runTool("mirror").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Mirror");
    EXPECT_FALSE(h.controller.operation()->prompt().empty());
    h.clickAt(h.screen({10, 0, 10})); // the +X face
    ASSERT_TRUE(h.controller.operation() && h.controller.operation()->canCommit());
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    const auto bb = geom::boundingBox(h.body().shape());
    EXPECT_NEAR(bb.min.x, -10.0, 1e-6);
    EXPECT_NEAR(bb.max.x, 30.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.body().shape()), 16000.0, 1e-3);
    EXPECT_EQ(h.body().shape().solidCount(), 1);
    EXPECT_EQ(h.stack.undoLabel(), "Mirror");
}

// Found on the CI Mac's small window: a click on the face just beside an edge
// picked the edge (edges win within the pick tolerance), so Mirror, which
// waits for a flat face, ignored it. Tools waiting for a face pick faces only.
TEST(Interaction, MirrorPlaneClickBesideAnEdgeTakesTheFace)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0)..(10,10,20)
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.runTool("mirror").ok());
    const Vec3 nearEdge{10, 0, 20 - 1.5 * h.controller.camera().pixelSize({10, 0, 20})}; // on +X, 1.5 px below the top edge
    const auto profile = InputProfile::forDevice(PointerDevice::Mouse);
    ASSERT_EQ(h.controller.pickAt(h.screen(nearEdge), profile).kind, sel::PickKind::Edge); // what a plain click picks
    EXPECT_EQ(h.controller.operationPickAt(h.screen(nearEdge), profile).kind, sel::PickKind::Face);
    h.clickAt(h.screen(nearEdge));
    ASSERT_TRUE(h.controller.operation() && h.controller.operation()->canCommit());
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_NEAR(geom::boundingBox(h.body().shape()).max.x, 30.0, 1e-6);
}

TEST(Interaction, MirrorAcrossAnOriginPlane)
{
    Harness h;
    const Uuid a = addBox(h, "Body 1", {5, 0, 0}, {10, 10, 10});
    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_TRUE(h.controller.triggerAction("plane:0").ok()); // across YZ
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const auto bb = geom::boundingBox(h.document.body(a)->shape());
    EXPECT_NEAR(bb.min.x, -15.0, 1e-6);
    EXPECT_EQ(h.document.body(a)->shape().solidCount(), 2);
    bool detail = false;
    for (const auto& row : h.controller.historyRows())
        detail = detail || (row.name == "Mirror" && row.detail == "Across YZ");
    EXPECT_TRUE(detail);
}

// Pattern defaults to three copies side by side along X; the count stays
// editable in the model panel; circular patterns turn around the center.
TEST(Interaction, PatternLinearThenEditCount)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    ASSERT_TRUE(h.controller.selectBody(h.body().id(), false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_EQ(pattern->count(), 3);
    EXPECT_NEAR(pattern->value(), 25.0, 1e-9); // 20 mm body + 5 mm gap
    EXPECT_TRUE(pattern->hasPreview());
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_NEAR(geom::boundingBox(h.body().shape()).size().x, 20.0 + 2 * 25.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.body().shape()), 3 * 8000.0, 1e-3);

    const Uuid step = h.body().features().back()->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(step, "count", "4").ok());
    EXPECT_NEAR(geom::volume(h.body().shape()), 4 * 8000.0, 1e-3);
    EXPECT_FALSE(h.controller.setFeatureParameter(step, "count", "2.5").ok());
    bool detail = false;
    for (const auto& row : h.controller.historyRows())
        detail = detail || (row.name == "Pattern" && row.detail.find("along X") != std::string::npos);
    EXPECT_TRUE(detail);
}

TEST(Interaction, PatternCircularAroundCenter)
{
    Harness h;
    const Uuid bar = addBox(h, "Bar", {-10, -2, 0}, {20, 4, 4});
    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.triggerAction("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("layout:circular").ok());
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_TRUE(pattern->circular());
    EXPECT_EQ(pattern->count(), 6);
    EXPECT_NEAR(pattern->value(), 360.0, 1e-9);
    ASSERT_TRUE(h.controller.triggerAction("fewer").ok());
    ASSERT_TRUE(h.controller.triggerAction("fewer").ok());
    EXPECT_EQ(dynamic_cast<const PatternOperation*>(h.controller.operation())->count(), 4);
    EXPECT_TRUE(h.controller.valueLabelPosition().has_value()); // the angle can be typed
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::volume(h.document.body(bar)->shape()), 2 * 320.0 - 64.0, 1e-3); // a plus sign
}

TEST(Interaction, UnitAwareInput)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    EXPECT_EQ(h.controller.setValueText("1in"), ""); // the new height
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 25.4);
    EXPECT_NE(h.controller.setValueText("banana"), "");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 25.4) << "bad input keeps the previous value";
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.height(), 25.4, 1e-6);
}

TEST(Interaction, TooDeepPushShowsErrorAndCannotCommit)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    // Typed: 30 mm less than the 20 mm height is refused.
    EXPECT_EQ(h.controller.setValueText("-30"), "Height must be greater than zero.");
    EXPECT_FALSE(h.controller.operation()->canCommit());
    // Dragged through the bottom face: the arrow shows the error.
    const double px = h.controller.camera().pixelSize({0, 0, 20});
    const Vec2 grab = h.screen({0, 0, 20 + 40 * px});
    h.drag(grab, grab + Vec2{0, 600});
    EXPECT_EQ(h.controller.operation()->error(), "Height must be greater than zero.");
    EXPECT_FALSE(h.controller.operation()->canCommit());
    EXPECT_FALSE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.height(), 20.0, 1e-6);
    EXPECT_EQ(h.controller.renderScene().arrows[0].state, HandleState::Error);
}

TEST(Interaction, EdgeSelectionAndFillet)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    // Front-right vertical edge at (10, -10, z) is visible in the iso view.
    h.clickAt(h.screen({10, -10, 10}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Edge);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Fillet");
    EXPECT_EQ(h.controller.setValueText("3"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double expected = 8000.0 - (9.0 - kPi * 9.0 / 4.0) * 20.0;
    EXPECT_NEAR(geom::volume(h.body().shape()), expected, 1e-3);
    EXPECT_TRUE(h.controller.selection().empty());

    // Too-large fillet: user-facing error, document unchanged.
    h.clickAt(h.screen({-10, -10, 10}));
    ASSERT_NE(h.controller.operation(), nullptr);
    const std::string refusal = h.controller.setValueText("25");
    double suggested = 0;
    ASSERT_EQ(std::sscanf(refusal.c_str(), "The radius is too large for this edge. Try %lf mm or less.", &suggested), 1) << refusal;
    EXPECT_EQ(h.controller.setValueText(std::to_string(suggested)), "") << "the suggested radius works";
    EXPECT_NEAR(geom::volume(h.body().shape()), expected, 1e-3);
}

TEST(Interaction, ShiftClickAddsEdgesAndChamferSwitch)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({10, -10, 10}));
    auto e = Harness::at(h.screen({0, -10, 20}));
    e.modifiers.shift = true;
    h.controller.pointerPress(e);
    h.controller.pointerRelease(e);
    EXPECT_EQ(h.controller.selection().size(), 2u);
    ASSERT_TRUE(h.controller.triggerAction("chamfer").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Chamfer");
    EXPECT_EQ(h.controller.setValueText("2"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_LT(geom::volume(h.body().shape()), 8000.0);
}

TEST(Interaction, TouchTapIsAdditive)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({10, -10, 10}), PointerDevice::Touch);
    h.clickAt(h.screen({0, -10, 20}), PointerDevice::Touch);
    EXPECT_EQ(h.controller.selection().size(), 2u);
    h.clickAt({20, 20}, PointerDevice::Touch);
    EXPECT_TRUE(h.controller.selection().empty());
}

TEST(Interaction, DoubleClickSelectsBodyAndDeleteRemovesIt)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    const Vec2 p = h.screen({0, 0, 20});
    h.clickAt(p);
    h.controller.pointerDoubleClick(Harness::at(p));
    ASSERT_TRUE(h.controller.selection().allOfKind(sel::SelectionKind::Body));
    EXPECT_NE(h.controller.selectionSummary().find("20.00"), std::string::npos);
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_TRUE(h.document.bodies().empty());
    EXPECT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
}

// The ground grid (drawn per pixel, fading out from half its radius): spaced
// for the zoom at the ground, reaching past the model's footprint, fading
// with distance from the eye only in perspective, the axes further out.
TEST(Interaction, GroundGridCoversTheFootprintAndFollowsTheZoom)
{
    for (const auto projection : {Camera::Projection::Orthographic, Camera::Projection::Perspective}) {
        Harness h;
        h.controller.setProjection(projection);
        addBox(h, "Long", {0, -10, 0}, {300, 20, 10});
        h.controller.fitAll(false);
        // Zoomed in on the far end: the view shows little of the part.
        for (int i = 0; i < 12; ++i)
            h.controller.wheel(h.screen({290, 0, 10}), 1);
        const Camera& camera = h.controller.camera();
        const RenderGrid grid = h.controller.renderScene().grid;
        EXPECT_NEAR(grid.majorStep, 10 * grid.minorStep, 1e-12);
        EXPECT_NEAR(std::remainder(grid.center.x, grid.majorStep), 0.0, 1e-9) << "the center lies on a major line";
        EXPECT_EQ(grid.center.z, 0.0);
        double reach = 0;
        for (const double x : {0.0, 300.0})
            for (const double y : {-10.0, 10.0})
                reach = std::max(reach, std::hypot(x - grid.center.x, y - grid.center.y));
        EXPECT_GE(grid.radius, 2 * reach - 1e-9) << "the whole footprint lies inside the unfaded half";
        EXPECT_NEAR(grid.axisRadius, 1.5 * grid.radius, 1e-9);
        const Vec3 below{camera.target.x, camera.target.y, 0};
        EXPECT_NEAR(grid.minorStep, snapIncrement(camera.pixelSize(below), 14.0), 1e-12) << "spaced for the ground";
        if (projection == Camera::Projection::Perspective) {
            EXPECT_GT(grid.eyeFadeStart, 0.0);
            EXPECT_GT(grid.eyeFadeEnd, grid.eyeFadeStart);
        } else {
            EXPECT_EQ(grid.eyeFadeEnd, 0.0) << "no distance fade in orthographic";
        }
        // Everything on the ground stays inside the clipping range.
        EXPECT_GE(h.controller.renderScene().camera.sceneRadius, grid.axisRadius);
    }
}

// In perspective, zoomed onto the top of a tall part, the grid is spaced for
// the ground far below, not for the top.
TEST(Interaction, GroundGridIsSpacedForTheGroundInPerspective)
{
    Harness h;
    addBox(h, "Tower", {-10, -10, 0}, {20, 20, 200});
    h.controller.setStandardView(StandardView::Top, false);
    h.controller.fitAll(false);
    for (int i = 0; i < 10; ++i)
        h.controller.wheel(h.screen({0, 0, 200}), 1);
    const Camera& camera = h.controller.camera();
    ASSERT_NEAR(camera.target.z, 200.0, 1e-6) << "the zoom headed for the top face";
    const RenderGrid grid = h.controller.renderScene().grid;
    EXPECT_NEAR(grid.minorStep, snapIncrement(camera.pixelSize({0, 0, 0}), 14.0), 1e-12);
    EXPECT_GT(grid.minorStep, snapIncrement(camera.pixelSize(camera.target), 14.0));
}

// The point under the cursor stays there. In perspective the zoom heads for
// the surface under the cursor (not the target's depth), so the eye closes
// in on that point by the zoom factor and never passes through it.
TEST(Interaction, WheelZoomsTowardCursor)
{
    for (const auto projection : {Camera::Projection::Orthographic, Camera::Projection::Perspective}) {
        Harness h;
        h.controller.setProjection(projection);
        ASSERT_TRUE(h.controller.createBox(20).ok());
        h.controller.fitAll(false);
        const Vec3 onFace{6, -10, 14}; // the front face, off the view's center and the target's depth
        const Vec2 cursor = h.screen(onFace);
        const double heightBefore = h.controller.camera().orthoHeight;
        const double distanceBefore = (h.controller.camera().eye() - onFace).length();
        h.controller.wheel(cursor, 2);
        const Vec2 after = h.screen(onFace);
        EXPECT_NEAR(after.x, cursor.x, 1e-6);
        EXPECT_NEAR(after.y, cursor.y, 1e-6);
        const double factor = 0.85 * 0.85;
        if (projection == Camera::Projection::Orthographic) {
            EXPECT_NEAR(h.controller.camera().orthoHeight, heightBefore * factor, 1e-9);
        } else {
            EXPECT_NEAR((h.controller.camera().eye() - onFace).length(), distanceBefore * factor, 1e-6);
            for (int i = 0; i < 80; ++i)
                h.controller.wheel(cursor, 1);
            EXPECT_GT(h.controller.camera().depthOf(onFace), 0.0) << "never through the face";
            EXPECT_NEAR((h.screen(onFace) - cursor).length(), 0.0, 1e-6);
        }
    }
}

TEST(Interaction, PanWithMiddleButton)
{
    Harness h;
    h.controller.fitAll(false);
    const Vec3 before = h.controller.camera().target;
    h.drag({500, 400}, {600, 450}, PointerButton::Middle);
    EXPECT_GT((h.controller.camera().target - before).length(), 1e-3);
}

TEST(Interaction, AnimationReachesTarget)
{
    Harness h;
    h.controller.setStandardView(StandardView::Top, true);
    int guard = 0;
    while (h.controller.advanceAnimation() && ++guard < 500)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_NEAR(h.controller.camera().pitch, kPi / 2, 1e-9);
}

// Maker enclosure: shell a box open at the top with 2 mm walls.
TEST(Interaction, ShellMakesAnOpenEnclosure)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Push/Pull");
    bool offersShell = false, offersSketch = false;
    for (const auto& a : h.controller.contextActions()) {
        offersShell = offersShell || a.id == "shell";
        offersSketch = offersSketch || a.id == "sketch";
    }
    EXPECT_TRUE(offersShell);
    EXPECT_TRUE(offersSketch);

    ASSERT_TRUE(h.controller.triggerAction("shell").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Shell");
    EXPECT_EQ(h.controller.setValueText("2"), "");
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::volume(h.body().shape()), 8000.0 - 16.0 * 16.0 * 18.0, 1e-3);
    EXPECT_TRUE(h.controller.selection().empty());

    // Too thick: explained, not applied.
    h.clickAt(h.screen({10, 0, 10})); // right side face
    ASSERT_TRUE(h.controller.triggerAction("shell").ok());
    EXPECT_NE(h.controller.setValueText("12"), "");
    EXPECT_FALSE(h.controller.operation()->canCommit());
}

// Move: grab an axis arrow, type a value, switch axis, type again, apply.
TEST(Interaction, MoveBodyAlongAxes)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
    h.controller.fitAll(false);
    const Vec2 p = h.screen({0, 0, 20});
    h.clickAt(p);
    h.controller.pointerDoubleClick(Harness::at(p));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Move");
    ASSERT_EQ(h.controller.renderScene().arrows.size(), 3u);

    // Grab an axis arrow where it is currently drawn (arrows follow the preview).
    auto grab = [&](int axis) {
        const auto arrow = h.controller.renderScene().arrows.at(std::size_t(axis));
        const double px = h.controller.camera().pixelSize(arrow.anchor);
        const Vec2 s = h.screen(arrow.anchor + arrow.direction * (50 * px));
        h.controller.pointerPress(Harness::at(s));
        h.controller.pointerRelease(Harness::at(s));
    };
    grab(0);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "X");
    EXPECT_EQ(h.controller.setValueText("15"), "");
    grab(1);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Y");
    EXPECT_EQ(h.controller.setValueText("-5"), "");
    ASSERT_TRUE(h.controller.operation()->canCommit());
    ASSERT_TRUE(h.controller.commitOperation().ok());

    const auto bb = geom::boundingBox(h.body().shape());
    EXPECT_NEAR(bb.min.x, 5.0, 1e-9);
    EXPECT_NEAR(bb.min.y, -15.0, 1e-9);
    EXPECT_NEAR(bb.min.z, 0.0, 1e-9);
    // Still selected with fresh arrows for another move.
    EXPECT_TRUE(h.controller.selection().allOfKind(sel::SelectionKind::Body));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Move");

    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        listed = listed || (row.name == "Move" && row.detail.find("15.00, -5.00, 0.00") != std::string::npos);
    EXPECT_TRUE(listed);

    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(geom::boundingBox(h.body().shape()).min.x, -10.0, 1e-9);
}

namespace {
// Two overlapping 20 mm boxes: A at the origin, B shifted +10 in X.
Uuid addBox(doc::Document& d, cmd::UndoStack& stack, const std::string& name, Vec3 origin)
{
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = origin;
    box->size = {20, 20, 20};
    auto command = std::make_unique<cmd::CreateBodyCommand>(name, std::move(box));
    const Uuid id = command->bodyId();
    EXPECT_TRUE(stack.push(std::move(command), d).ok());
    return id;
}
} // namespace

TEST(Interaction, CombineBodies)
{
    for (const auto& [action, expected] : std::vector<std::pair<std::string, double>>{
             {"union", 12000.0}, {"subtract", 4000.0}, {"intersect", 4000.0}}) {
        Harness h;
        const Uuid a = addBox(h.document, h.stack, "A", {0, 0, 0});
        const Uuid b = addBox(h.document, h.stack, "B", {10, 0, 0});
        h.controller.documentChanged();
        h.controller.fitAll(false);
        // Double-click A's top (x=5), Shift+double-click B's top (x=25).
        h.controller.pointerDoubleClick(Harness::at(h.screen({5, 10, 20})));
        auto e = Harness::at(h.screen({25, 10, 20}));
        e.modifiers.shift = true;
        h.controller.pointerDoubleClick(e);
        ASSERT_EQ(h.controller.selection().size(), 2u) << action;
        ASSERT_TRUE(h.controller.triggerAction(action).ok()) << action;
        EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), expected, 1e-3) << action;
        EXPECT_FALSE(h.document.body(b)->isVisible()) << "tool body is consumed";
        // One undo restores both bodies as they were.
        EXPECT_TRUE(h.controller.undo());
        EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 8000.0, 1e-6);
        EXPECT_TRUE(h.document.body(b)->isVisible());
    }
}

TEST(Interaction, CombineFollowsToolEdits)
{
    Harness h;
    const Uuid a = addBox(h.document, h.stack, "A", {0, 0, 0});
    const Uuid b = addBox(h.document, h.stack, "B", {10, 0, 0});
    h.controller.documentChanged();
    h.controller.fitAll(false);
    h.controller.pointerDoubleClick(Harness::at(h.screen({5, 10, 20})));
    auto e = Harness::at(h.screen({25, 10, 20}));
    e.modifiers.shift = true;
    h.controller.pointerDoubleClick(e);
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 4000.0, 1e-3);
    // Make the (hidden) tool narrower through the history: A regains material.
    const Uuid toolBox = h.document.body(b)->features().front()->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(toolBox, "width", "5").ok());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 8000.0 - 5.0 * 20 * 20, 1e-3);
    bool named = false;
    for (const auto& row : h.controller.historyRows())
        named = named || row.detail == "Subtract \xC2\xB7 B";
    EXPECT_TRUE(named);
    // B cannot now subtract A (cycle).
    h.document.setBodyVisible(b, true);
    h.controller.documentChanged();
    h.controller.pointerDoubleClick(Harness::at(h.screen({12, 10, 20})));
    auto e2 = Harness::at(h.screen({2, 10, 20}));
    e2.modifiers.shift = true;
    h.controller.pointerDoubleClick(e2);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    EXPECT_FALSE(h.controller.triggerAction("subtract").ok());
}

TEST(Interaction, MeasureBetweenTwoFaces)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));             // top
    auto e = Harness::at(h.screen({10, 0, 10})); // +X side
    e.modifiers.shift = true;
    h.controller.pointerPress(e);
    h.controller.pointerRelease(e);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    EXPECT_EQ(h.controller.selectionSummary(), "Distance 0.00 mm \xC2\xB7 Angle 90.0\xC2\xB0");

    // Top and bottom are parallel: 20 mm apart. Pick the bottom through a
    // view from below.
    h.controller.keyPress(Key::Escape); // clear (the armed shell arrow would catch the next click)
    h.controller.setStandardView(StandardView::Bottom, false);
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 0}));
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);
    auto top = Harness::at(h.screen({0, 0, 20}));
    top.modifiers.shift = true;
    h.controller.pointerPress(top);
    h.controller.pointerRelease(top);
    ASSERT_EQ(h.controller.selection().size(), 2u);
    EXPECT_EQ(h.controller.selectionSummary(), "Gap 20.00 mm \xC2\xB7 parallel");
}

// The heat-set insert helper: select a hole's rim, choose the insert size.
TEST(Interaction, HeatSetInsertFromHoleRim)
{
    Harness h;
    // 40 x 40 x 10 block with a 3 mm through-hole at the center (cut from a sketch).
    auto block = std::make_unique<doc::BoxFeature>();
    block->origin = {-20, -20, 0};
    block->size = {40, 40, 10};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid id = create->bodyId();
    ASSERT_TRUE(h.stack.push(std::move(create), h.document).ok());
    auto sk = std::make_unique<sketch::Sketch>(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 10}, {0, 0, 1}));
    sk->addCircle(sketch::kOriginId, 1.5);
    const Uuid sketchId = sk->id();
    h.document.addSketch(std::move(sk));
    auto regions = doc::sketchRegions(*h.document.sketch(sketchId)).value();
    auto cut = std::make_unique<doc::ExtrudeFeature>();
    cut->sketchId = sketchId;
    cut->profiles = {doc::makeProfileRef(regions.front(), *h.document.sketch(sketchId))};
    cut->distance = -10;
    cut->mode = doc::ExtrudeMode::Cut;
    cut->throughAll = true;
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(cut)), h.document).ok());
    h.controller.documentChanged();
    h.controller.fitAll(false);
    const double holed = 16000.0 - kPi * 2.25 * 10;
    ASSERT_NEAR(geom::volume(h.document.body(id)->shape()), holed, 1e-3);

    // Click the top rim of the hole.
    h.clickAt(h.screen({1.5, 0, 10}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Edge);
    bool offered = false;
    for (const auto& a : h.controller.contextActions())
        offered = offered || a.id == "insert";
    ASSERT_TRUE(offered) << "a hole rim offers the heat-set insert helper";

    ASSERT_TRUE(h.controller.triggerAction("insert").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Heat-set insert M3");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 6.0) << "typical M3 depth, previewed right away";
    EXPECT_TRUE(h.controller.operation()->hasPreview());

    // Switch to M4, then apply.
    ASSERT_TRUE(h.controller.triggerAction("preset:3").ok());
    EXPECT_EQ(h.controller.operation()->title(), "Heat-set insert M4");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double expected = holed - kPi * (2.8 * 2.8 - 1.5 * 1.5) * 8.5;
    EXPECT_NEAR(geom::volume(h.document.body(id)->shape()), expected, 1e-3);
    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        listed = listed || (row.name == "Hole" && row.detail.find("M4 heat-set insert") != std::string::npos);
    EXPECT_TRUE(listed);
}

// Clicking a flat face shows the part's size to the parallel face behind it;
// typing sets that size directly (a leading + or - changes it by that much).
TEST(Interaction, PushPullShowsAndSetsTheThickness)
{
    Harness h;
    addBox(h, "Block", {0, 0, 0}, {30, 20, 10}); // X 30 wide, Y 20 deep, Z 10 high
    h.controller.fitAll(false);

    h.clickAt(h.screen({15, 10, 10})); // top
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Height");
    EXPECT_NEAR(h.controller.operation()->value(), 10.0, 1e-9);
    EXPECT_FALSE(h.controller.operation()->canCommit()) << "nothing changed yet";
    EXPECT_EQ(h.controller.setValueText("+5"), "");
    EXPECT_NEAR(h.controller.operation()->value(), 15.0, 1e-9);
    EXPECT_EQ(h.controller.setValueText("-2"), "") << "relative to the size shown, not the last value";
    EXPECT_NEAR(h.controller.operation()->value(), 8.0, 1e-9);
    EXPECT_EQ(h.controller.setValueText("12"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.height(), 12.0, 1e-6);
    ASSERT_NE(h.controller.operation(), nullptr) << "the face stays selected";
    EXPECT_NEAR(h.controller.operation()->value(), 12.0, 1e-9) << "and shows the new height";

    h.controller.keyPress(Key::Escape);
    h.clickAt(h.screen({30, 10, 5})); // +X side
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Width");
    EXPECT_NEAR(h.controller.operation()->value(), 30.0, 1e-9);

    h.controller.keyPress(Key::Escape);
    h.clickAt(h.screen({15, 0, 5})); // -Y side (front)
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Depth");
    EXPECT_NEAR(h.controller.operation()->value(), 20.0, 1e-9);
    EXPECT_EQ(h.controller.setValueText("0"), "Depth must be greater than zero.");
    EXPECT_EQ(h.controller.setValueText("25"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::boundingBox(h.body().shape()).size().y, 25.0, 1e-6);
}

// A washer's centroid is in its hole: the arrow and the measurement move onto
// the face itself.
TEST(Interaction, PushPullOnAFaceWithAHoleMeasuresOnTheFace)
{
    Harness h;
    const Uuid id = addBox(h, "Plate", {-20, -20, 0}, {40, 40, 5});
    auto cut = std::make_unique<doc::CombineFeature>();
    const Uuid pin = addBox(h, "Pin", {-10, -10, -1}, {20, 20, 7});
    cut->toolBody = pin;
    cut->mode = doc::CombineMode::Subtract;
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(cut)), h.document).ok());
    h.controller.documentChanged();
    const doc::Body* plate = h.document.body(id);
    int top = -1;
    for (int i = 0; i < plate->shape().faceCount(); ++i)
        if (const auto info = geom::faceInfo(plate->shape(), i); info && info->normal.z > 0.999)
            top = i;
    ASSERT_GE(top, 0);
    auto op = PushPullOperation::create(h.document, id, top);
    ASSERT_NE(op, nullptr);
    const Vec3 anchor = op->anchor();
    EXPECT_GT(std::max(std::abs(anchor.x), std::abs(anchor.y)), 10.0) << "not in the square hole";
    EXPECT_NEAR(anchor.z, 5.0, 1e-9);
    ASSERT_TRUE(op->thickness().has_value());
    EXPECT_NEAR(op->value(), 5.0, 1e-9);
}

// Without a parallel face behind (here a deep chamfer), push/pull falls back
// to the distance the face moves.
TEST(Interaction, PushPullWithoutParallelOppositeMovesByDistance)
{
    Harness h;
    const Uuid id = addBox(h, "Block", {0, 0, 0}, {20, 20, 20});
    const geom::Shape box = h.document.body(id)->shape();
    int edge = -1;
    for (int i = 0; i < box.edgeCount(); ++i)
        if (const auto e = geom::edgeInfo(box, i); e && std::abs(e->midpoint.x - 20) < 1e-9 && std::abs(e->midpoint.z) < 1e-9)
            edge = i;
    ASSERT_GE(edge, 0);
    auto chamfer = std::make_unique<doc::ChamferFeature>();
    chamfer->size = 15;
    chamfer->edges = {{edge, *geom::captureEdgeSignature(box, edge)}};
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(chamfer)), h.document).ok());
    h.controller.documentChanged();

    const geom::Shape shape = h.document.body(id)->shape();
    int top = -1;
    for (int i = 0; i < shape.faceCount(); ++i)
        if (const auto info = geom::faceInfo(shape, i); info && info->normal.z > 0.999)
            top = i;
    auto op = PushPullOperation::create(h.document, id, top);
    ASSERT_NE(op, nullptr);
    EXPECT_FALSE(op->thickness().has_value());
    EXPECT_EQ(op->valueLabel(), "Distance");
    EXPECT_DOUBLE_EQ(op->value(), 0.0);
    op->setValue(5.0, h.document);
    EXPECT_TRUE(op->canCommit());
    ASSERT_TRUE(h.stack.push(op->makeCommand(h.document), h.document).ok());
    EXPECT_NEAR(geom::boundingBox(h.document.body(id)->shape()).size().z, 25.0, 1e-6);
}

TEST(Interaction, AxisTriadFollowsTheView)
{
    Harness h;
    h.controller.setStandardView(StandardView::Top, false);
    auto marks = h.controller.axisTriad();
    ASSERT_EQ(marks.size(), 3u);
    EXPECT_NEAR(marks[0].direction.x, 1.0, 1e-9); // X to the right
    EXPECT_NEAR(marks[0].direction.y, 0.0, 1e-9);
    EXPECT_NEAR(marks[1].direction.x, 0.0, 1e-9); // Y up the screen
    EXPECT_NEAR(marks[1].direction.y, -1.0, 1e-9);
    EXPECT_NEAR(marks[2].depth, 1.0, 1e-9); // Z straight at the viewer
    EXPECT_NEAR(marks[2].direction.length(), 0.0, 1e-9);

    h.controller.setStandardView(StandardView::Isometric, false);
    marks = h.controller.axisTriad();
    EXPECT_NEAR(marks[2].direction.x, 0.0, 1e-9);
    EXPECT_LT(marks[2].direction.y, -0.8) << "Z points up the screen";
    EXPECT_GT(marks[0].direction.x, 0.5) << "X to the lower right";
    EXPECT_GT(marks[0].direction.y, 0.0);
}

// Fillets around a pushed face come along (Shapr3D-style), stored so that
// older files without the flag recompute as before.
TEST(Interaction, PushPullTakesRoundedEdgesAlong)
{
    Harness h;
    const Uuid id = addBox(h, "Block", {-10, -10, 0}, {20, 20, 20});
    const geom::Shape cube = h.document.body(id)->shape();
    auto fillet = std::make_unique<doc::FilletFeature>();
    fillet->size = 3;
    for (int i = 0; i < cube.edgeCount(); ++i)
        if (const auto e = geom::edgeInfo(cube, i); e && std::abs(e->start.z - 20) < 1e-9 && std::abs(e->end.z - 20) < 1e-9)
            fillet->edges.push_back({i, *geom::captureEdgeSignature(cube, i)});
    ASSERT_EQ(fillet->edges.size(), 4u);
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(fillet)), h.document).ok());
    h.controller.documentChanged();
    h.controller.fitAll(false);
    const int facesBefore = h.body().shape().faceCount();

    h.clickAt(h.screen({0, 0, 20}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Height");
    EXPECT_NEAR(h.controller.operation()->value(), 20.0, 1e-9);
    EXPECT_EQ(h.controller.setValueText("30"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.height(), 30.0, 1e-6);
    EXPECT_EQ(h.body().shape().faceCount(), facesBefore) << "no step: the fillets moved up whole";
    int roundEdges = 0;
    for (int i = 0; i < h.body().shape().faceCount(); ++i)
        if (const auto f = geom::faceInfo(h.body().shape(), i); f && f->kind == geom::SurfaceKind::Cylinder && std::abs(f->radius - 3) < 1e-9)
            roundEdges += std::abs(f->axisOrigin.z - 27) < 1e-6 ? 1 : 0;
    EXPECT_EQ(roundEdges, 4);

    // The step says so in its file entry; an entry without it is the classic push/pull.
    const auto& step = static_cast<const doc::PushPullFeature&>(*h.body().features().back());
    EXPECT_TRUE(step.keepEdges);
    nlohmann::json params;
    step.writeParams(params);
    EXPECT_TRUE(params.value("keepEdges", false));
    params.erase("keepEdges");
    doc::PushPullFeature old;
    ASSERT_TRUE(old.readParams(params).ok());
    EXPECT_FALSE(old.keepEdges);
}
