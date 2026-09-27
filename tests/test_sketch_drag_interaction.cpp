// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Editing a sketch by dragging, through the interaction layer with the
// mouse and with a finger (owner, from an iPhone, 2026-09-27: "really
// difficult to edit a sketch. In Shapr3D, you can modify a sketch by
// clicking and dragging unconstrained lines"; "I have no idea how to resize
// rectangles"). Every check measures coordinates.
#include "commands/Command.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchGestures.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace os;
using namespace os::interact;

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    TouchGestureRecognizer recognizer;
    double time = 0;

    explicit Harness(bool touch = false)
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
        controller.setTouchLayout(touch);
        controller.fitAll(false);
        EXPECT_TRUE(controller.startSketch().ok());
        controller.skipAnimation();
    }

    SketchSession& session() { return *controller.sketchSession(); }
    const sketch::Sketch& sketch() { return session().sketch(); }

    static PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left)
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        return e;
    }
    Vec2 screen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }

    // ---- Mouse, in sketch coordinates ----
    void click(Vec2 p, bool shift = false)
    {
        auto e = at(screen(p));
        e.modifiers.shift = shift;
        controller.pointerMove(at(screen(p), PointerButton::None));
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    void drag(Vec2 from, Vec2 to)
    {
        const Vec2 a = screen(from), b = screen(to);
        controller.pointerPress(at(a));
        for (int i = 1; i <= 8; ++i)
            controller.pointerMove(at(a + (b - a) * (i / 8.0)));
        controller.pointerRelease(at(b));
    }
    // Press, release, press + double-click, release (Qt's order).
    void doubleClick(Vec2 p)
    {
        const auto e = at(screen(p));
        controller.pointerPress(e);
        controller.pointerRelease(e);
        controller.pointerPress(e);
        controller.pointerDoubleClick(e);
        controller.pointerRelease(e);
    }

    // ---- A finger, through the gesture recognizer as ViewportItem routes it ----
    void frame(TouchPoint::State state, Vec2 p)
    {
        time += 0.02;
        using Kind = TouchIntent::Kind;
        for (const auto& intent : recognizer.update({{1, p, state}}, time)) {
            PointerEvent e;
            e.device = PointerDevice::Touch;
            e.button = PointerButton::Left;
            e.position = intent.position;
            switch (intent.kind) {
            case Kind::PointerPress: controller.pointerPress(e); break;
            case Kind::PointerMove: controller.pointerMove(e); break;
            case Kind::PointerRelease: controller.pointerRelease(e); break;
            case Kind::PointerCancel: controller.cancelPointer(); break;
            case Kind::DoubleTap: controller.pointerDoubleClick(e); break;
            default: break;
            }
        }
    }
    void tap(Vec2 local, bool soon = false)
    {
        frame(TouchPoint::State::Pressed, screen(local));
        frame(TouchPoint::State::Released, screen(local));
        if (!soon)
            time += 1.0; // not a double tap with the next one
    }
    void doubleTap(Vec2 local)
    {
        tap(local, true);
        time += 0.1;
        tap(local);
    }
    void stroke(Vec2 from, Vec2 to)
    {
        const Vec2 a = screen(from), b = screen(to);
        frame(TouchPoint::State::Pressed, a);
        for (int i = 1; i <= 8; ++i)
            frame(TouchPoint::State::Moved, a + (b - a) * (i / 8.0));
        frame(TouchPoint::State::Released, b);
        time += 1.0;
    }

    // ---- Reading the sketch ----
    Vec2 pos(sketch::EntityId id) { return sketch().point(id)->position; }
    // The line between these two positions (either way round), or none.
    sketch::EntityId lineAt(Vec2 a, Vec2 b)
    {
        for (const auto& [id, l] : sketch().lines()) {
            const Vec2 p = pos(l.start), q = pos(l.end);
            if (((p - a).length() < 1e-6 && (q - b).length() < 1e-6) || ((p - b).length() < 1e-6 && (q - a).length() < 1e-6))
                return id;
        }
        return sketch::kNoEntity;
    }
    bool hasPoint(Vec2 q)
    {
        return std::any_of(sketch().points().begin(), sketch().points().end(),
                           [&](const auto& p) { return (p.second.position - q).length() < 1e-6; });
    }
    std::size_t count(sketch::ConstraintKind kind)
    {
        return std::size_t(std::count_if(sketch().constraints().begin(), sketch().constraints().end(),
                                         [&](const auto& c) { return c.second.kind == kind; }));
    }
    bool offers(const std::string& id)
    {
        const auto actions = controller.contextActions();
        return std::any_of(actions.begin(), actions.end(), [&](const ContextAction& a) { return a.id == id; });
    }
    bool selected(sketch::EntityId id)
    {
        const auto& s = session().selection();
        return std::find(s.begin(), s.end(), id) != s.end();
    }

    // A free 40 x 20 rectangle (10, 10)..(50, 30), drawn by dragging, then the Select tool.
    void rectangle()
    {
        controller.setSketchTool(SketchTool::Rectangle);
        drag({10, 10}, {50, 30});
        ASSERT_EQ(sketch().lines().size(), 4u);
        ASSERT_TRUE(hasPoint({10, 10}) && hasPoint({50, 30}));
        controller.setSketchTool(SketchTool::Select);
    }
    // A free circle at (-40, 0), radius 20.
    sketch::EntityId circle()
    {
        controller.setSketchTool(SketchTool::Circle);
        click({-40, 0});
        click({-20, 0});
        EXPECT_EQ(sketch().circles().size(), 1u);
        controller.setSketchTool(SketchTool::Select);
        return sketch().circles().empty() ? sketch::kNoEntity : sketch().circles().begin()->first;
    }
};

} // namespace

TEST(SketchDragInteraction, TheGridIsFiveMillimetres)
{
    // The drags below land on the grid; the view they assume.
    Harness h;
    const double pixel = h.controller.camera().pixelSize({0, 0, 0});
    EXPECT_GT(pixel * 10, 2.0);
    EXPECT_LE(pixel * 10, 5.0);
}

TEST(SketchDragInteraction, MouseDragsARectangleSide)
{
    Harness h;
    h.rectangle();
    const sketch::EntityId top = h.lineAt({10, 30}, {50, 30});
    ASSERT_NE(top, sketch::kNoEntity);
    h.click({30, 30});
    ASSERT_TRUE(h.selected(top));
    const std::size_t steps = h.stack.index();

    // The top side pulled up 10: the rectangle grows, its bottom stays.
    h.drag({30, 30}, {30, 40});
    EXPECT_TRUE(h.hasPoint({10, 40}));
    EXPECT_TRUE(h.hasPoint({50, 40}));
    EXPECT_TRUE(h.hasPoint({10, 10}));
    EXPECT_TRUE(h.hasPoint({50, 10}));
    EXPECT_EQ(h.stack.index(), steps + 1);
    EXPECT_EQ(h.stack.undoLabel(), "Move line");
    // The release after the drag kept the selection.
    EXPECT_EQ(h.session().selection(), std::vector<sketch::EntityId>{top});
    EXPECT_TRUE(h.messages.empty());

    // A side dragged without selecting it first: the right one, out by 15.
    h.drag({50, 25}, {65, 25});
    EXPECT_TRUE(h.hasPoint({65, 10}));
    EXPECT_TRUE(h.hasPoint({65, 40}));
    EXPECT_TRUE(h.hasPoint({10, 10}));

    // One undo step each.
    ASSERT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.hasPoint({50, 40}));
    ASSERT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.hasPoint({50, 30}));
}

TEST(SketchDragInteraction, FingerDragsALineAndACircleRim)
{
    // The same with one finger (the Select tool; no selecting first).
    Harness h(true);
    h.rectangle();
    const sketch::EntityId circle = h.circle();
    ASSERT_NE(circle, sketch::kNoEntity);
    ASSERT_NEAR(h.sketch().circle(circle)->radius, 20, 1e-9);

    h.stroke({30, 10}, {30, 0}); // the bottom side down 10
    EXPECT_TRUE(h.hasPoint({10, 0}));
    EXPECT_TRUE(h.hasPoint({50, 0}));
    EXPECT_TRUE(h.hasPoint({50, 30}));
    EXPECT_EQ(h.stack.undoLabel(), "Move line");

    // The circle's rim, out by 10: the radius follows, the center stays.
    const Vec2 center = h.pos(h.sketch().circle(circle)->center);
    h.stroke({-40, 20}, {-40, 30});
    EXPECT_NEAR(h.sketch().circle(circle)->radius, 30, 1e-6);
    EXPECT_NEAR((h.pos(h.sketch().circle(circle)->center) - center).length(), 0, 1e-6);
    EXPECT_EQ(h.stack.undoLabel(), "Resize circle");

    // Its center: the circle moves, its size stays.
    h.stroke({-40, 0}, {-45, 5});
    EXPECT_NEAR(h.pos(h.sketch().circle(circle)->center).x, -45, 1e-6);
    EXPECT_NEAR(h.pos(h.sketch().circle(circle)->center).y, 5, 1e-6);
    EXPECT_NEAR(h.sketch().circle(circle)->radius, 30, 1e-6);
    EXPECT_EQ(h.stack.undoLabel(), "Move circle");
    EXPECT_TRUE(h.messages.empty());
}

TEST(SketchDragInteraction, DragInsideAShapeMovesItWhole)
{
    for (const bool touch : {false, true}) {
        Harness h(touch);
        h.rectangle();
        const sketch::EntityId circle = h.circle();
        const Vec2 center = h.pos(h.sketch().circle(circle)->center);
        if (touch)
            h.stroke({30, 20}, {40, 15});
        else
            h.drag({30, 20}, {40, 15});
        for (const Vec2 corner : {Vec2{20, 5}, Vec2{60, 5}, Vec2{60, 25}, Vec2{20, 25}})
            EXPECT_TRUE(h.hasPoint(corner)) << corner.x << ", " << corner.y << (touch ? " (touch)" : "");
        EXPECT_EQ(h.stack.undoLabel(), "Move shape");
        EXPECT_EQ(h.sketch().lines().size(), 4u);
        // The other shape stayed.
        EXPECT_NEAR((h.pos(h.sketch().circle(circle)->center) - center).length(), 0, 1e-9);
        // And the camera did not orbit (still looking straight down).
        EXPECT_NEAR(h.controller.camera().forward().z, -1.0, 1e-9);
    }
}

TEST(SketchDragInteraction, SeveralSelectedItemsMoveTogether)
{
    Harness h;
    h.rectangle();
    const sketch::EntityId circle = h.circle();
    const sketch::EntityId top = h.lineAt({10, 30}, {50, 30});
    h.click({30, 30});
    h.click({-40, 20}, true); // Shift: the circle too
    ASSERT_EQ(h.session().selection().size(), 2u);
    ASSERT_TRUE(h.selected(circle));
    h.drag({30, 30}, {35, 40});
    // The top side and the circle moved by (5, 10). The rectangle's sides
    // stay vertical, so its bottom shifts sideways by 5 but stays at y = 10.
    EXPECT_TRUE(h.hasPoint({15, 40}));
    EXPECT_TRUE(h.hasPoint({55, 40}));
    EXPECT_TRUE(h.hasPoint({15, 10}));
    EXPECT_NEAR(h.pos(h.sketch().circle(circle)->center).x, -35, 1e-6);
    EXPECT_NEAR(h.pos(h.sketch().circle(circle)->center).y, 10, 1e-6);
    EXPECT_EQ(h.stack.undoLabel(), "Move shape");
    EXPECT_EQ(h.session().selection().size(), 2u);
    EXPECT_TRUE(h.selected(top));
}

TEST(SketchDragInteraction, AFullySizedShapeSaysSoAndStays)
{
    Harness h;
    h.controller.setSketchTool(SketchTool::Rectangle);
    // From the origin, typed 60 x 40: nothing of it can move.
    h.click({0, 0});
    h.controller.pointerMove(Harness::at(h.screen({35, 22}), PointerButton::None));
    EXPECT_EQ(h.session().typeIntoInput("60"), "");
    h.session().focusNextInput();
    EXPECT_EQ(h.session().typeIntoInput("40"), "");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.sketch().solveReport().degreesOfFreedom, 0);
    h.controller.setSketchTool(SketchTool::Select);
    const std::size_t steps = h.stack.index();
    const sketch::Sketch before = h.sketch();

    for (const auto& [from, to] : {std::pair{Vec2{30, 40}, Vec2{30, 50}}, std::pair{Vec2{30, 20}, Vec2{40, 30}}}) {
        h.messages.clear();
        h.drag(from, to);
        ASSERT_EQ(h.messages.size(), 1u);
        EXPECT_EQ(h.messages.front(), "Fully sized: change or remove a dimension to move it.");
        EXPECT_EQ(h.stack.index(), steps);
        for (const auto& [id, p] : h.sketch().points())
            EXPECT_NEAR((p.position - before.point(id)->position).length(), 0, 1e-12);
    }
    // Each item shows it cannot move (all fixed: the dark "defined" color).
    const RenderSketch render = h.session().renderData(h.controller.camera());
    for (const auto& line : render.lines)
        EXPECT_NE(line.style, SketchStyle::Normal);
}

TEST(SketchDragInteraction, MovableAndFixedItemsAreColoredApart)
{
    // A rectangle from the origin with only its width typed: its bottom side
    // (at the origin, horizontal, sized) is fixed; its top can move. A free
    // circle beside it can move.
    Harness h;
    h.controller.setSketchTool(SketchTool::Rectangle);
    h.click({0, 0});
    h.controller.pointerMove(Harness::at(h.screen({35, 22}), PointerButton::None));
    EXPECT_EQ(h.session().typeIntoInput("60"), "");
    h.click({60, 40});
    h.controller.setSketchTool(SketchTool::Select);
    const sketch::EntityId bottom = h.lineAt({0, 0}, {60, 0});
    const sketch::EntityId top = h.lineAt({0, 40}, {60, 40});
    ASSERT_NE(bottom, sketch::kNoEntity);
    ASSERT_NE(top, sketch::kNoEntity);
    EXPECT_FALSE(h.sketch().solveReport().canMove(bottom));
    EXPECT_TRUE(h.sketch().solveReport().canMove(top));
    // Drawn apart: the bottom side's segment in the fixed style, the top's in the movable one.
    const RenderSketch render = h.session().renderData(h.controller.camera());
    const sketch::Plane& plane = h.sketch().plane();
    auto styleAt = [&](Vec2 a, Vec2 b) {
        for (const auto& l : render.lines)
            if (((l.a - plane.toWorld(a)).length() < 1e-6 && (l.b - plane.toWorld(b)).length() < 1e-6)
                || ((l.a - plane.toWorld(b)).length() < 1e-6 && (l.b - plane.toWorld(a)).length() < 1e-6))
                return l.style;
        return SketchStyle::Preview;
    };
    EXPECT_EQ(styleAt({0, 0}, {60, 0}), SketchStyle::Defined);
    EXPECT_EQ(styleAt({0, 40}, {60, 40}), SketchStyle::Normal);
    // Dragging the fixed side is refused; the top moves (only up and down).
    h.drag({30, 0}, {30, -10});
    EXPECT_EQ(h.messages.size(), 1u);
    EXPECT_TRUE(h.hasPoint({0, 0}));
    h.drag({30, 40}, {35, 50});
    EXPECT_TRUE(h.hasPoint({0, 50}));
    EXPECT_TRUE(h.hasPoint({60, 50}));
}

TEST(SketchDragInteraction, ADroppedPointJoinsWhatItLandsOn)
{
    Harness h;
    h.rectangle();
    // A separate line from (70, 10) to (80, 45).
    h.controller.setSketchTool(SketchTool::Line);
    h.click({70, 10});
    h.click({80, 45});
    ASSERT_TRUE(h.controller.keyPress(Key::Escape));
    h.controller.setSketchTool(SketchTool::Select);
    const sketch::EntityId tail = h.lineAt({70, 10}, {80, 45});
    ASSERT_NE(tail, sketch::kNoEntity);
    const std::size_t points = h.sketch().points().size();

    // Its lower end onto the rectangle's corner (50, 10): one point from now on.
    h.drag({70, 10}, {51, 11});
    EXPECT_EQ(h.stack.undoLabel(), "Connect point");
    EXPECT_EQ(h.sketch().points().size(), points - 1);
    const auto* line = h.sketch().line(tail);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE((h.pos(line->start) - Vec2{50, 10}).length() < 1e-6 || (h.pos(line->end) - Vec2{50, 10}).length() < 1e-6);
    // The rectangle and the line are one chain now.
    h.doubleClick({65, 27.5});
    EXPECT_EQ(h.session().selection().size(), 5u);
    h.click({-80, -60}); // empty space

    // Its upper end onto the top side's middle: a Midpoint constraint.
    h.drag({80, 45}, {31, 31});
    EXPECT_EQ(h.stack.undoLabel(), "Connect point");
    EXPECT_EQ(h.count(sketch::ConstraintKind::Midpoint), 1u);
    EXPECT_TRUE(h.hasPoint({30, 30}));

    // A new free line whose end is dropped onto a side (not at its middle): On line.
    h.controller.setSketchTool(SketchTool::Line);
    h.click({-10, 60});
    h.click({-30, 70});
    ASSERT_TRUE(h.controller.keyPress(Key::Escape));
    h.controller.setSketchTool(SketchTool::Select);
    h.drag({-10, 60}, {20, 10}); // onto the bottom side, left of its middle
    EXPECT_EQ(h.count(sketch::ConstraintKind::PointOnLine), 1u);
    bool onBottom = false;
    for (const auto& [id, l] : h.sketch().lines())
        for (const auto end : {l.start, l.end})
            onBottom = onBottom || (std::abs(h.pos(end).x - 20) < 1e-6 && std::abs(h.pos(end).y - 10) < 1e-6);
    EXPECT_TRUE(onBottom);
    EXPECT_TRUE(h.sketch().solveReport().ok);
}

TEST(SketchDragInteraction, TappingASizeTypesIt)
{
    // Tap a rectangle side -> its length shows; typing one resizes it.
    Harness h(true);
    h.rectangle();
    const sketch::EntityId circle = h.circle();
    const sketch::EntityId top = h.lineAt({10, 30}, {50, 30});
    h.tap({30, 30});
    ASSERT_EQ(h.session().selection(), std::vector<sketch::EntityId>{top});
    auto sizeLabel = [&]() -> std::optional<SketchLabel> {
        for (const auto& label : h.session().labels(h.controller.camera()))
            if (label.kind == SketchLabel::Kind::Size)
                return label;
        return std::nullopt;
    };
    auto label = sizeLabel();
    ASSERT_TRUE(label);
    EXPECT_EQ(label->entity, top);
    EXPECT_EQ(label->text, "40");
    EXPECT_EQ(h.session().setDimension(top, "25"), "");
    EXPECT_EQ(h.stack.undoLabel(), "Length");
    EXPECT_EQ(h.count(sketch::ConstraintKind::Distance), 1u);
    const auto* l = h.sketch().line(top);
    EXPECT_NEAR((h.pos(l->end) - h.pos(l->start)).length(), 25, 1e-9);
    EXPECT_NEAR(h.pos(l->start).y, h.pos(l->end).y, 1e-9);
    // Sized now: a dimension label takes its place.
    EXPECT_FALSE(sizeLabel());
    // The opposite side follows (a rectangle stays one).
    const sketch::EntityId bottom = [&] {
        for (const auto& [id, line] : h.sketch().lines())
            if (id != top && std::abs(h.pos(line.start).y - h.pos(line.end).y) < 1e-9)
                return id;
        return sketch::kNoEntity;
    }();
    ASSERT_NE(bottom, sketch::kNoEntity);
    EXPECT_NEAR((h.pos(h.sketch().line(bottom)->end) - h.pos(h.sketch().line(bottom)->start)).length(), 25, 1e-9);
    // That side's size is set by the other one now: typing it is refused plainly.
    h.tap({-80, -60}); // empty space
    h.session().select(bottom, false);
    EXPECT_EQ(h.session().setDimension(bottom, "30"), "Other dimensions already set this size: change one of them.");

    // A circle: its diameter.
    h.session().select(circle, false);
    label = sizeLabel();
    ASSERT_TRUE(label);
    EXPECT_EQ(label->text, "\xC3\x98" "40");
    EXPECT_EQ(h.session().setDimension(circle, "30"), "");
    EXPECT_EQ(h.stack.undoLabel(), "Diameter");
    EXPECT_NEAR(h.sketch().circle(circle)->radius, 15, 1e-9);
    EXPECT_NE(h.session().setDimension(circle, "-3"), "");
}

TEST(SketchDragInteraction, DoubleTapSelectsTheChainAndDeleteRemovesIt)
{
    for (const bool touch : {false, true}) {
        Harness h(touch);
        h.rectangle();
        const sketch::EntityId circle = h.circle();
        if (touch)
            h.doubleTap({30, 30});
        else
            h.doubleClick({30, 30});
        ASSERT_EQ(h.session().selection().size(), 4u) << (touch ? "touch" : "mouse");
        for (const auto id : h.session().selection())
            EXPECT_NE(h.sketch().line(id), nullptr);
        ASSERT_TRUE(h.controller.triggerAction("delete").ok());
        EXPECT_TRUE(h.sketch().lines().empty());
        EXPECT_NE(h.sketch().circle(circle), nullptr);
    }
}

TEST(SketchDragInteraction, TapInsideAShapeThenExtrude)
{
    Harness h(true);
    h.rectangle();
    EXPECT_FALSE(h.offers("extrude"));
    h.tap({30, 20});
    EXPECT_TRUE(h.session().selection().empty());
    ASSERT_TRUE(h.session().selectedRegion());
    ASSERT_TRUE(h.offers("extrude"));
    const Uuid sketchId = h.session().sketchId();
    ASSERT_TRUE(h.controller.triggerAction("extrude").ok());
    EXPECT_EQ(h.controller.mode(), InteractionController::Mode::Model);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    EXPECT_EQ(h.controller.selection().items()[0].bodyId, sketchId);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Extrude");
    EXPECT_EQ(h.controller.setValueText("10"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 1u);
    EXPECT_NEAR(geom::volume(h.document.bodies()[0]->shape()), 40 * 20 * 10, 1e-6);
    const auto bb = geom::boundingBox(h.document.bodies()[0]->shape());
    EXPECT_NEAR(bb.min.x, 10, 1e-6);
    EXPECT_NEAR(bb.max.y, 30, 1e-6);
}

TEST(SketchDragInteraction, ACancelledDragPutsItBack)
{
    // A second finger turns the drag into a pan: what was dragged goes back.
    Harness h(true);
    h.rectangle();
    const sketch::Sketch before = h.sketch();
    const Vec2 a = h.screen({30, 30}), b = h.screen({30, 45});
    h.frame(TouchPoint::State::Pressed, a);
    for (int i = 1; i <= 4; ++i)
        h.frame(TouchPoint::State::Moved, a + (b - a) * (i / 4.0));
    EXPECT_TRUE(h.hasPoint({10, 45}));
    h.controller.cancelPointer();
    for (const auto& [id, p] : h.sketch().points())
        EXPECT_NEAR((p.position - before.point(id)->position).length(), 0, 1e-12);
}
