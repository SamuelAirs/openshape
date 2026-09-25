// Headless end-to-end tests of the interaction layer: the same code paths the
// desktop UI drives, fed with synthetic pointer/keyboard events.
#include "commands/Command.h"
#include "document/Document.h"
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
