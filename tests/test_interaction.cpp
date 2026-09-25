// Headless end-to-end tests of the interaction layer: the same code paths the
// desktop UI drives, fed with synthetic pointer/keyboard events.
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

#include <chrono>
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

    // 9-10. Drag the arrow upward: preview updates, document does not change yet.
    const double px = h.controller.camera().pixelSize({0, 0, 20});
    const Vec2 grab = h.screen({0, 0, 20 + 40 * px});
    h.drag(grab, grab + Vec2{0, -80});
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_GT(h.controller.operation()->value(), 1.0);
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    EXPECT_TRUE(h.controller.renderScene().bodies[0].isPreview);
    EXPECT_NEAR(h.height(), 20.0, 1e-6) << "preview must not modify the document";
    EXPECT_EQ(h.stack.size(), 1u);

    // 11. Type an exact value.
    EXPECT_EQ(h.controller.setValueText("15"), "");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 15.0);
    EXPECT_EQ(h.controller.operationValueText(), "15.00 mm");

    // 12. Enter commits: body height becomes exactly 35 mm.
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_NEAR(h.height(), 35.0, 1e-6);
    EXPECT_NEAR(geom::volume(h.body().shape()), 20.0 * 20.0 * 35.0, 1e-4);
    // The pushed face stays selected with a fresh manipulator for the next push.
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].index, h.topFace());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 0.0);
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
    h.controller.setValueText("5");
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

TEST(Interaction, UnitAwareInput)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    EXPECT_EQ(h.controller.setValueText("1in"), "");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 25.4);
    EXPECT_NE(h.controller.setValueText("banana"), "");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 25.4) << "bad input keeps the previous value";
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.height(), 45.4, 1e-6);
}

TEST(Interaction, TooDeepPushShowsErrorAndCannotCommit)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.clickAt(h.screen({0, 0, 20}));
    EXPECT_NE(h.controller.setValueText("-30"), "");
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
    EXPECT_EQ(h.controller.setValueText("25"), "Unable to create this fillet. Try a smaller radius.");
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

TEST(Interaction, WheelZoomsTowardCursor)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    const double before = h.controller.camera().orthoHeight;
    h.controller.wheel({600, 400}, 2);
    EXPECT_LT(h.controller.camera().orthoHeight, before);
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
