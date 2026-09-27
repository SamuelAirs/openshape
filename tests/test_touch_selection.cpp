// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Selecting bodies with a finger, exactly as a finger does it: touch frames
// with a clock go through TouchGestureRecognizer and its intents reach the
// controller as ViewportItem::touchEvent routes them (owner, iPhone and iPad,
// 2026-09-27: "It's also really difficult or impossible to select two or
// more objects by double tapping").
#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchGestures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace os;
using namespace os::interact;

namespace {

using State = TouchPoint::State;
using Kind = TouchIntent::Kind;

constexpr Vec2 kIpad{1180, 820};
constexpr Vec2 kIphone{402, 874};

// Two separate 20 mm boxes side by side: A from x 0 to 20, B from 40 to 60.
struct TwoBoxes {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    Uuid a, b;

    explicit TwoBoxes(Vec2 viewport = kIpad)
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        a = addBox("A", {0, 0, 0});
        b = addBox("B", {40, 0, 0});
        controller.documentChanged();
        controller.setTouchLayout(true);
        controller.setViewportSize(viewport);
        controller.fitAll(false);
    }

    Uuid addBox(const std::string& name, Vec3 origin)
    {
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = origin;
        box->size = {20, 20, 20};
        auto command = std::make_unique<cmd::CreateBodyCommand>(name, std::move(box));
        const Uuid id = command->bodyId();
        EXPECT_TRUE(stack.push(std::move(command), document).ok());
        return id;
    }

    Vec2 screen(Vec3 p) const { return controller.camera().project(p); }
    Vec2 topA() const { return screen({10, 10, 20}); }
    Vec2 topB() const { return screen({50, 10, 20}); }
    Vec2 frontA() const { return screen({10, 0, 10}); }
    Vec2 frontB() const { return screen({50, 0, 10}); }

    std::string name(const Uuid& id) const { return id == a ? "A" : id == b ? "B" : "?"; }

    // "bodies A B", "faces A:3", "edges B:7", "none".
    std::string selectionText() const
    {
        const auto& items = controller.selection().items();
        if (items.empty())
            return "none";
        std::ostringstream out;
        const auto kind = items.front().kind;
        out << (kind == sel::SelectionKind::Body   ? "bodies"
                : kind == sel::SelectionKind::Face ? "faces"
                : kind == sel::SelectionKind::Edge ? "edges"
                                                   : "other");
        for (const auto& item : items) {
            out << ' ' << name(item.bodyId);
            if (item.kind != sel::SelectionKind::Body)
                out << ':' << item.index;
            if (item.kind != kind)
                out << "(mixed)";
        }
        return out.str();
    }
    // Exactly these bodies are selected (as bodies).
    bool bodiesSelected(const std::set<Uuid>& ids) const
    {
        const auto& items = controller.selection().items();
        std::set<Uuid> selected;
        for (const auto& item : items) {
            if (item.kind != sel::SelectionKind::Body)
                return false;
            selected.insert(item.bodyId);
        }
        return selected == ids && items.size() == ids.size();
    }
    bool offers(const std::string& id) const
    {
        const auto actions = controller.contextActions();
        return std::any_of(actions.begin(), actions.end(), [&](const ContextAction& action) { return action.id == id; });
    }
    std::string state() const
    {
        std::string text = selectionText();
        if (const Operation* op = controller.operation())
            text += " | " + op->title();
        if (offers("union"))
            text += " | union";
        return text;
    }
    double volumeOf(const Uuid& id) const { return geom::volume(document.body(id)->shape()); }
    double minX(const Uuid& id) const { return geom::boundingBox(document.body(id)->shape()).min.x; }
};

// One finger on the screen, with a clock: frames go through the recognizer
// and its intents to the controller, as ViewportItem::touchEvent does. Every
// intent is logged with the state it left, for the failure messages.
struct Finger {
    TwoBoxes& scene;
    TouchGestureRecognizer recognizer;
    double time = 100;
    int id = 0;
    std::vector<std::string> log;

    explicit Finger(TwoBoxes& s) : scene(s) {}

    void frame(const std::vector<TouchPoint>& points)
    {
        for (const auto& intent : recognizer.update(points, time)) {
            PointerEvent e;
            e.device = PointerDevice::Touch;
            e.button = PointerButton::Left;
            e.position = intent.position;
            auto& c = scene.controller;
            const char* what = "";
            switch (intent.kind) {
            case Kind::PointerPress: c.pointerPress(e); what = "press"; break;
            case Kind::PointerMove: c.pointerMove(e); what = "move"; break;
            case Kind::PointerRelease: c.pointerRelease(e); what = "release"; break;
            case Kind::PointerCancel: c.cancelPointer(); what = "cancel"; break;
            case Kind::DoubleTap: c.pointerDoubleClick(e); what = "DOUBLE-TAP"; break;
            case Kind::Pan: c.twoFingerPan(intent.from, intent.to); what = "pan"; break;
            case Kind::Pinch: c.pinch(intent.position, intent.scale); what = "pinch"; break;
            case Kind::Undo: (void)c.undo(); what = "undo"; break;
            case Kind::Redo: (void)c.redo(); what = "redo"; break;
            }
            if (intent.kind != Kind::PointerMove && intent.kind != Kind::Pan && intent.kind != Kind::Pinch)
                log.push_back(std::string(what) + " -> " + scene.state());
        }
    }
    // A tap lasting `duration` seconds that lifts `slide` px from where it landed.
    void tap(Vec2 p, double duration = 0.08, Vec2 slide = {})
    {
        ++id;
        frame({{id, p, State::Pressed}});
        if (slide.length() > 0) {
            time += duration / 3;
            frame({{id, p + slide * 0.5, State::Moved}});
            time += duration / 3;
            frame({{id, p + slide, State::Moved}});
            time += duration / 3;
        } else {
            time += duration;
        }
        frame({{id, p + slide, State::Released}});
    }
    void pause(double seconds) { time += seconds; }
    // Two taps `gap` seconds apart (lift to touch), the second at `second`,
    // then a pause (so the next tap starts afresh).
    void doubleTap(Vec2 first, Vec2 second, double gap = 0.15, double duration = 0.08, Vec2 slide = {})
    {
        tap(first, duration, slide);
        pause(gap);
        tap(second, duration, slide);
        pause(1.0);
    }
    void doubleTap(Vec2 p) { doubleTap(p, p); }

    std::string trace() const
    {
        std::string out;
        for (const auto& line : log)
            out += "\n    " + line;
        return out;
    }
};

// The nearest screen point to `from`, walking towards `to`, that picks
// `kind` (touch tolerances).
Vec2 firstPick(const TwoBoxes& s, Vec2 from, Vec2 to, sel::PickKind kind)
{
    const double length = (to - from).length();
    for (double t = 0; t <= length; t += 0.5) {
        const Vec2 p = from + (to - from) * (t / length);
        if (s.controller.pickAt(p, InputProfile::forDevice(PointerDevice::Touch)).kind == kind)
            return p;
    }
    return to;
}

// Within a finger's grab zone of one of the active operation's arrows.
bool onArrow(const TwoBoxes& s, Vec2 p)
{
    const Operation* op = s.controller.operation();
    const double tolerance = InputProfile::forDevice(PointerDevice::Touch).handleTolerance;
    for (int i = 0; op && i < op->handleCount(); ++i)
        if (op->handle(i).hitTest(s.controller.camera(), p, op->handleOffset(i), tolerance))
            return true;
    return false;
}

// A point on body `id`'s visible faces clear of every arrow's grab zone, if
// there is one: on a phone the arrows' finger zones (36 px either side of
// the shaft) cover the whole of a small selected body.
std::optional<Vec2> clearOfArrows(const TwoBoxes& s, const Uuid& id)
{
    const double x0 = id == s.a ? 0 : 40;
    const InputProfile touch = InputProfile::forDevice(PointerDevice::Touch);
    for (const Vec3 p : {Vec3{x0 + 10, 0, 10}, Vec3{x0 + 4, 0, 4}, Vec3{x0 + 16, 0, 4}, Vec3{x0 + 20, 10, 4},
                         Vec3{x0 + 20, 16, 16}, Vec3{x0 + 4, 0, 16}, Vec3{x0 + 4, 16, 20}, Vec3{x0 + 1.5, 0, 1.5},
                         Vec3{x0 + 20, 18.5, 1.5}, Vec3{x0 + 10, 0, 1.5}, Vec3{x0 + 20, 1.5, 1.5}}) {
        const Vec2 screen = s.screen(p);
        const auto hit = s.controller.pickAt(screen, touch);
        if (hit.kind == sel::PickKind::Face && hit.bodyId == id && !onArrow(s, screen))
            return screen;
    }
    return std::nullopt;
}

} // namespace

// The owner's report: double-tap one box, then the other. Both must end up
// selected as bodies, with Union / Subtract / Intersect offered, on an iPad
// and on an iPhone, for realistic finger timing and jitter.
TEST(TouchMultiSelect, DoubleTapTwoBodiesSelectsBoth)
{
    struct Timing {
        double gap, duration;
        Vec2 offset; // where the second tap lands, from the first
    };
    for (const Vec2 viewport : {kIpad, kIphone})
        for (const Timing timing : {Timing{0.15, 0.08, {0, 0}}, Timing{0.06, 0.05, {2, 1}}, Timing{0.25, 0.10, {-9, 11}},
                                    Timing{0.20, 0.12, {15, 0}}, Timing{0.30, 0.09, {6, -6}}}) {
            TwoBoxes s(viewport);
            Finger finger(s);
            finger.doubleTap(s.topA(), s.topA() + timing.offset, timing.gap, timing.duration);
            const std::string where = "viewport " + std::to_string(int(viewport.x)) + " gap " + std::to_string(timing.gap)
                                    + " offset " + std::to_string(timing.offset.x) + "," + std::to_string(timing.offset.y);
            ASSERT_TRUE(s.bodiesSelected({s.a})) << where << finger.trace();
            ASSERT_NE(s.controller.operation(), nullptr) << where;
            EXPECT_EQ(s.controller.operation()->title(), "Move") << where;

            finger.doubleTap(s.topB(), s.topB() + timing.offset, timing.gap, timing.duration);
            EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << where << finger.trace();
            EXPECT_TRUE(s.offers("union") && s.offers("subtract") && s.offers("intersect")) << where << finger.trace();
            EXPECT_EQ(s.controller.operation(), nullptr) << "two bodies arm no tool" << where;
            EXPECT_EQ(s.stack.size(), 2u) << "selecting changed the document" << where << finger.trace();
        }
}

// A double-tap on a selected body takes it out of the selection (and only it).
TEST(TouchMultiSelect, DoubleTapOnSelectedBodyRemovesIt)
{
    for (const Vec2 viewport : {kIpad, kIphone}) {
        TwoBoxes s(viewport);
        Finger finger(s);
        finger.doubleTap(s.topA());
        finger.doubleTap(s.topB());
        ASSERT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
        finger.doubleTap(s.frontB()); // another face of B
        EXPECT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
        ASSERT_NE(s.controller.operation(), nullptr);
        EXPECT_EQ(s.controller.operation()->title(), "Move") << "one body left: its Move arrows again";
        finger.doubleTap(s.topA());
        EXPECT_TRUE(s.controller.selection().empty()) << finger.trace();
        EXPECT_EQ(s.controller.operation(), nullptr);
    }
}

// Shapr3D: while bodies are selected, a single tap on another body adds that
// body (not one of its faces), and a tap on a selected body takes it out.
TEST(TouchMultiSelect, TapAddsBodyWhileBodiesAreSelected)
{
    for (const Vec2 viewport : {kIpad, kIphone}) {
        TwoBoxes s(viewport);
        Finger finger(s);
        finger.doubleTap(s.topA());
        ASSERT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
        finger.tap(s.frontB());
        finger.pause(1.0);
        EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
        EXPECT_TRUE(s.offers("union")) << finger.trace();
        EXPECT_EQ(s.controller.operation(), nullptr) << finger.trace();
        finger.tap(s.topA());
        finger.pause(1.0);
        EXPECT_TRUE(s.bodiesSelected({s.b})) << finger.trace();
        // B alone has its Move arrows again, standing on its top face: a
        // tap there is on the Z arrow (it chooses it), B stays selected.
        ASSERT_NE(s.controller.operation(), nullptr);
        ASSERT_TRUE(onArrow(s, s.topB()));
        finger.tap(s.topB());
        finger.pause(1.0);
        EXPECT_TRUE(s.bodiesSelected({s.b})) << finger.trace();
        // A tap on B away from its arrows takes it out. On the phone its
        // arrows cover all of it: a double-tap does (as anywhere).
        if (const auto clear = clearOfArrows(s, s.b)) {
            EXPECT_EQ(viewport.x, kIpad.x);
            finger.tap(*clear);
            finger.pause(1.0);
        } else {
            EXPECT_EQ(viewport.x, kIphone.x) << "the iPad has room beside the arrows";
            finger.doubleTap(s.topB());
        }
        EXPECT_TRUE(s.controller.selection().empty()) << finger.trace();
        // With nothing selected, a tap selects a face as always.
        finger.tap(s.topB());
        finger.pause(1.0);
        EXPECT_EQ(s.selectionText().rfind("faces B:", 0), 0u) << finger.trace();
    }
}

// On a phone the first body's X arrow reaches over the second body, and a
// finger's grab zone around an arrow is 28 px wide. A tap right on the
// arrow chooses it; a tap beside it on the other body adds that body; a drag
// from there still moves along the arrow.
TEST(TouchMultiSelect, TapBesideAnArrowOverAnotherBodyAddsIt)
{
    const InputProfile touch = InputProfile::forDevice(PointerDevice::Touch);
    for (const int pass : {0, 1, 2}) {
        TwoBoxes s(kIphone);
        Finger finger(s);
        finger.doubleTap(s.topA());
        const Operation* move = s.controller.operation();
        ASSERT_NE(move, nullptr);
        // The X arrow, and a point on its shaft over B.
        int x = -1;
        Vec2 onShaft;
        Vec2 across; // perpendicular to the arrow on screen, pointing down
        for (int i = 0; i < move->handleCount() && x < 0; ++i) {
            const LinearManipulator& arrow = move->handle(i);
            if (std::abs(arrow.direction().x) < 0.9)
                continue;
            const Vec2 base = s.screen(arrow.base());
            Vec2 along = s.screen(arrow.base() + arrow.direction()) - base;
            along = along * (1.0 / along.length());
            across = along.y > 0 ? Vec2{-along.y, along.x} : Vec2{along.y, -along.x};
            if (across.y < 0)
                across = across * -1.0;
            for (double t = 0; t < 200 && x < 0; t += 1) {
                const Vec2 p = base + along * t;
                const auto d = arrow.hitTest(s.controller.camera(), p, move->handleOffset(i), 0);
                if (d && *d < 0.5 && s.controller.pickAt(p + across * 28.0, touch).bodyId == s.b
                    && s.controller.pickAt(p, touch).bodyId == s.b) {
                    x = i;
                    onShaft = p + along * 4.0;
                }
            }
        }
        ASSERT_GE(x, 0) << "on the phone, A's X arrow reaches over B";
        // 24 px beside the shaft: in the arrow's grab zone, on B.
        const Vec2 beside = onShaft + across * 24.0;
        ASSERT_TRUE(move->handle(x).hitTest(s.controller.camera(), beside, move->handleOffset(x), touch.handleTolerance));
        ASSERT_EQ(s.controller.pickAt(beside, touch).bodyId, s.b);

        if (pass == 0) {
            // Right on the arrow: it becomes the active one, nothing else changes.
            finger.tap(onShaft, 0.08, {2, 3});
            finger.pause(1.0);
            EXPECT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
            ASSERT_NE(s.controller.operation(), nullptr);
            EXPECT_EQ(s.controller.operation()->activeHandle(), x);
            EXPECT_EQ(s.controller.operation()->value(), 0.0);
        } else if (pass == 1) {
            // A drag from beside the arrow still moves A along it.
            ++finger.id;
            finger.frame({{finger.id, beside, State::Pressed}});
            for (int step = 1; step <= 5; ++step) {
                finger.time += 0.03;
                finger.frame({{finger.id, beside + Vec2{12.0 * step, 0}, State::Moved}});
            }
            ASSERT_NE(s.controller.operation(), nullptr);
            EXPECT_GT(s.controller.operation()->value(), 1.0) << "dragged along X" << finger.trace();
            finger.time += 0.03;
            finger.frame({{finger.id, beside + Vec2{60, 0}, State::Released}});
            EXPECT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
        } else {
            // A tap beside it, on B: B is added, A did not move.
            finger.tap(beside, 0.08, {-2, 2});
            finger.pause(1.0);
            EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
            EXPECT_TRUE(s.offers("union")) << finger.trace();
            EXPECT_NEAR(s.minX(s.a), 0.0, 1e-9) << "A did not move";
            EXPECT_EQ(s.stack.size(), 2u);
        }
    }
}

// A face tapped first, then a double-tap on the other body: one kind at a
// time, so the body replaces the face (nothing mixed, nothing left over).
TEST(TouchMultiSelect, TapFaceThenDoubleTapOtherBody)
{
    TwoBoxes s;
    Finger finger(s);
    finger.tap(s.topA());
    finger.pause(1.0);
    ASSERT_EQ(s.selectionText().rfind("faces A:", 0), 0u) << finger.trace();
    finger.doubleTap(s.topB());
    EXPECT_TRUE(s.bodiesSelected({s.b})) << finger.trace();
}

// The first tap of a double-tap lands on an edge (edges win within 18 px on
// touch), the second on the face beside it: still that body, added.
TEST(TouchMultiSelect, DoubleTapStartingOnAnEdge)
{
    for (const Vec2 viewport : {kIpad, kIphone}) {
        TwoBoxes s(viewport);
        Finger finger(s);
        const InputProfile touch = InputProfile::forDevice(PointerDevice::Touch);
        // From the middle of the top face towards its front edge.
        const Vec2 edgeA = firstPick(s, s.topA(), s.screen({10, 0, 20}), sel::PickKind::Edge);
        const Vec2 edgeB = firstPick(s, s.topB(), s.screen({50, 0, 20}), sel::PickKind::Edge);
        ASSERT_EQ(s.controller.pickAt(edgeA, touch).kind, sel::PickKind::Edge);
        const Vec2 faceA = edgeA + (s.topA() - edgeA) * (4.0 / (s.topA() - edgeA).length());
        ASSERT_EQ(s.controller.pickAt(faceA, touch).kind, sel::PickKind::Face) << "4 px inside is the face";
        finger.doubleTap(edgeA, faceA);
        ASSERT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
        const Vec2 faceB = edgeB + (s.topB() - edgeB) * (4.0 / (s.topB() - edgeB).length());
        finger.doubleTap(edgeB, faceB);
        EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
        // And the other way round: face first, then the edge.
        finger.doubleTap(faceB, edgeB);
        EXPECT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
    }
}

// The second tap of a double-tap slips off the body (tapping empty space
// clears the selection): the double-tap still takes the body under the first
// tap, and the first body stays selected.
TEST(TouchMultiSelect, SecondTapJustOffTheBody)
{
    TwoBoxes s;
    Finger finger(s);
    finger.doubleTap(s.topA());
    // Just past B's right side, in empty space.
    const Vec2 inside = s.screen({60, 10, 10});
    const Vec2 outside = firstPick(s, inside, inside + Vec2{80, 0}, sel::PickKind::None);
    ASSERT_EQ(s.controller.pickAt(outside, InputProfile::forDevice(PointerDevice::Touch)).kind, sel::PickKind::None);
    const Vec2 first = outside - Vec2{12, 0};
    ASSERT_NE(s.controller.pickAt(first, InputProfile::forDevice(PointerDevice::Touch)).kind, sel::PickKind::None);
    finger.doubleTap(first, outside + Vec2{2, 0});
    EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
}

// A move typed for the first body is applied by the tap on the second (a tap
// elsewhere applies), and the double-tap still adds the second body.
TEST(TouchMultiSelect, PendingMoveIsAppliedAndTheBodyAdded)
{
    TwoBoxes s;
    Finger finger(s);
    finger.doubleTap(s.topA());
    ASSERT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
    ASSERT_EQ(s.controller.setValueText("5"), "");
    finger.doubleTap(s.topB());
    EXPECT_NEAR(s.minX(s.a), 5.0, 1e-6) << "the typed move is applied" << finger.trace();
    EXPECT_NEAR(s.minX(s.b), 40.0, 1e-6) << "B did not move";
    EXPECT_EQ(s.stack.size(), 3u) << "one Move step" << finger.trace();
    EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
}

// A finger on one of the first body's Move arrows is a press on the arrow: a
// tap there (with a few pixels of jitter) must not nudge the move, and a
// double-tap there adds the body under it.
TEST(TouchMultiSelect, TapOnAnArrowDoesNotNudgeTheMove)
{
    TwoBoxes s;
    Finger finger(s);
    finger.doubleTap(s.topA());
    ASSERT_NE(s.controller.operation(), nullptr);
    const auto arrows = s.controller.renderScene().arrows;
    ASSERT_FALSE(arrows.empty());
    // The X arrow's shaft (it points towards B), 60 px from its anchor.
    const auto x = std::find_if(arrows.begin(), arrows.end(), [](const RenderArrow& arrow) { return arrow.direction.x > 0.9; });
    ASSERT_NE(x, arrows.end());
    const Vec2 anchor = s.screen(x->anchor);
    const Vec2 along = s.screen(x->anchor + x->direction) - anchor;
    const Vec2 on = anchor + along * (60.0 / along.length());
    // 9.5 px along the arrow: under the 10 px a finger moves before it drags
    // (and a tap's 12 px), past the snapping step's half (8 px steps here).
    finger.tap(on, 0.09, along * (9.5 / along.length()));
    finger.pause(1.0);
    EXPECT_EQ(s.controller.operation()->value(), 0.0) << "a tap with jitter moved the body" << finger.trace();
    EXPECT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
}

// Pen mode: fingers only navigate, so a finger's double-tap selects nothing;
// the pen's double-tap (a mouse-like press, release, double-click) adds bodies.
TEST(TouchMultiSelect, PenDoubleTapsAddBodiesFingersDoNot)
{
    TwoBoxes s;
    s.controller.setPenMode(true);
    Finger finger(s);
    finger.doubleTap(s.topA());
    EXPECT_TRUE(s.controller.selection().empty()) << finger.trace();
    auto pen = [&](Vec2 p) {
        PointerEvent e;
        e.device = PointerDevice::Pen;
        e.position = p;
        s.controller.pointerPress(e);
        s.controller.pointerRelease(e);
        s.controller.pointerDoubleClick(e); // Qt: the second press arrives as the double-click
        s.controller.pointerRelease(e);
    };
    pen(s.topA());
    EXPECT_TRUE(s.bodiesSelected({s.a})) << s.state();
    pen(s.topB());
    EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << s.state();
    pen(s.frontA());
    EXPECT_TRUE(s.bodiesSelected({s.b})) << s.state();
}

// The mouse as Qt delivers a double-click (press, release, double-click,
// release): a plain double-click selects just that body; Shift adds one, and
// takes a selected one out again.
TEST(TouchMultiSelect, MouseDoubleClickAsQtDeliversIt)
{
    TwoBoxes s;
    s.controller.setTouchLayout(false);
    auto doubleClick = [&](Vec2 p, bool shift) {
        PointerEvent e;
        e.device = PointerDevice::Mouse;
        e.position = p;
        e.modifiers.shift = shift;
        s.controller.pointerPress(e);
        s.controller.pointerRelease(e);
        s.controller.pointerDoubleClick(e);
        s.controller.pointerRelease(e);
    };
    doubleClick(s.topA(), false);
    EXPECT_TRUE(s.bodiesSelected({s.a})) << s.state();
    doubleClick(s.topB(), true);
    EXPECT_TRUE(s.bodiesSelected({s.a, s.b})) << s.state();
    doubleClick(s.topB(), true);
    EXPECT_TRUE(s.bodiesSelected({s.a})) << s.state();
    doubleClick(s.topB(), false);
    EXPECT_TRUE(s.bodiesSelected({s.b})) << s.state();
    // A plain click on a face still replaces a body selection with the face.
    PointerEvent click;
    click.position = s.topA();
    s.controller.pointerPress(click);
    s.controller.pointerRelease(click);
    EXPECT_EQ(s.selectionText().rfind("faces A:", 0), 0u) << s.state();
}

// Union after two double-taps: one step, the volumes of both boxes.
TEST(TouchMultiSelect, UnionAfterTwoDoubleTaps)
{
    TwoBoxes s(kIphone);
    Finger finger(s);
    finger.doubleTap(s.topA());
    finger.doubleTap(s.topB());
    ASSERT_TRUE(s.bodiesSelected({s.a, s.b})) << finger.trace();
    ASSERT_TRUE(s.controller.triggerAction("union").ok());
    EXPECT_NEAR(s.volumeOf(s.a), 16000.0, 1e-3);
    EXPECT_FALSE(s.document.body(s.b)->isVisible());
}

// Review 2026-09-27: a finger dragging a Rotate ring from a point over the
// other body turns the body; it is not a tap beside an arrow (Rotate has no
// arrows), so the rotation stays pending and nothing is selected.
TEST(TouchMultiSelect, RingDragOverAnotherBodyKeepsRotate)
{
    TwoBoxes s(kIphone);
    Finger finger(s);
    finger.doubleTap(s.topA());
    ASSERT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
    ASSERT_TRUE(s.controller.triggerAction("rotate").ok());
    const Operation* rotate = s.controller.operation();
    ASSERT_NE(rotate, nullptr);
    ASSERT_GT(rotate->ringCount(), 0);
    const std::string title = rotate->title();
    const Camera& cam = s.controller.camera();
    const InputProfile touch = InputProfile::forDevice(PointerDevice::Touch);
    int ring = -1;
    double start = 0;
    for (int i = 0; i < rotate->ringCount() && ring < 0; ++i)
        for (int k = 0; k < 360 && ring < 0; ++k) {
            const double angle = 2 * kPi * k / 360.0;
            const Vec2 p = cam.project(rotate->ring(i).pointAt(cam, angle));
            const auto hit = s.controller.pickAt(p, touch);
            if (hit.kind == sel::PickKind::Face && hit.bodyId == s.b && rotate->ring(i).hitTest(cam, p, 2)) {
                ring = i;
                start = angle;
            }
        }
    ASSERT_GE(ring, 0) << "on the phone, a ring of A crosses B";
    const RingManipulator arc = rotate->ring(ring);
    ++finger.id;
    finger.frame({{finger.id, cam.project(arc.pointAt(cam, start)), State::Pressed}});
    for (int step = 1; step <= 12; ++step) {
        finger.time += 0.03;
        finger.frame({{finger.id, cam.project(arc.pointAt(cam, start + kPi / 2 * step / 12.0)), State::Moved}});
    }
    finger.time += 0.03;
    finger.frame({{finger.id, cam.project(arc.pointAt(cam, start + kPi / 2)), State::Released}});
    finger.pause(1.0);
    ASSERT_NE(s.controller.operation(), nullptr) << finger.trace();
    EXPECT_EQ(s.controller.operation()->title(), title) << "Rotate stays open" << finger.trace();
    EXPECT_NEAR(std::abs(s.controller.operation()->value()), 90.0, 1e-9) << finger.trace();
    EXPECT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
    EXPECT_EQ(s.stack.size(), 2u) << "nothing applied" << finger.trace();
}

// Review 2026-09-27: in tools whose clicks pick a target (Mirror's plane,
// Align's target), a double-tap or double-click on a face of the other body
// picks it as a single click does; it never applies the tool.
TEST(TouchMultiSelect, DoubleTapOnATargetDoesNotApplyTheTool)
{
    for (const std::string tool : {"mirror", "align"})
        for (const bool mouse : {false, true}) {
            TwoBoxes s;
            Finger finger(s);
            if (tool == "mirror") {
                finger.doubleTap(s.topA());
                ASSERT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
            } else {
                finger.tap(s.topA()); // a face: Align moves it onto a target
                finger.pause(1.0);
            }
            ASSERT_TRUE(s.controller.triggerAction(tool).ok()) << tool;
            const std::string title = s.controller.operation() ? s.controller.operation()->title() : "";
            ASSERT_FALSE(title.empty());
            if (mouse) {
                s.controller.setTouchLayout(false);
                PointerEvent e;
                e.device = PointerDevice::Mouse;
                e.position = s.frontB();
                s.controller.pointerPress(e);
                s.controller.pointerRelease(e);
                s.controller.pointerPress(e);
                s.controller.pointerDoubleClick(e);
                s.controller.pointerRelease(e);
            } else {
                finger.doubleTap(s.frontB(), s.frontB() + Vec2{3, -2});
            }
            EXPECT_EQ(s.stack.size(), 2u) << tool << (mouse ? " mouse" : " touch") << ": nothing applied " << s.state()
                                          << finger.trace();
            ASSERT_NE(s.controller.operation(), nullptr) << tool << finger.trace();
            EXPECT_EQ(s.controller.operation()->title(), title) << tool << (mouse ? " mouse" : " touch") << finger.trace();
        }
}

// Review 2026-09-27: with the mouse, the button kept down after a
// double-click and dragged still orbits; the double-click still selects.
TEST(TouchMultiSelect, MouseDragAfterADoubleClickOrbits)
{
    // In the middle of the top face (the second press lands on the Push/Pull
    // arrow the first click brought up), and off it.
    for (const Vec3 point : {Vec3{10, 10, 20}, Vec3{4, 4, 20}}) {
        TwoBoxes s;
        s.controller.setTouchLayout(false);
        PointerEvent e;
        e.device = PointerDevice::Mouse;
        e.position = s.screen(point);
        s.controller.pointerPress(e);
        s.controller.pointerRelease(e);
        s.controller.pointerPress(e);
        s.controller.pointerDoubleClick(e);
        EXPECT_TRUE(s.bodiesSelected({s.a})) << s.state();
        const Vec3 eye = s.controller.camera().eye();
        const Vec2 from = e.position;
        for (int step = 1; step <= 5; ++step) {
            e.position = from + Vec2{12.0 * step, 0};
            s.controller.pointerMove(e);
        }
        EXPECT_GT((s.controller.camera().eye() - eye).length(), 1e-3) << "the drag orbits";
        s.controller.pointerRelease(e);
        EXPECT_TRUE(s.bodiesSelected({s.a})) << "the release does not click " << s.state();
        EXPECT_EQ(s.stack.size(), 2u);
        // Without a drag, the second release does not click either.
        e.position = {5, 5}; // empty space: nothing selected
        s.controller.pointerPress(e);
        s.controller.pointerRelease(e);
        ASSERT_TRUE(s.controller.selection().empty()) << s.state();
        e.position = from;
        s.controller.pointerPress(e);
        s.controller.pointerRelease(e);
        s.controller.pointerPress(e);
        s.controller.pointerDoubleClick(e);
        s.controller.pointerRelease(e);
        EXPECT_TRUE(s.bodiesSelected({s.a})) << s.state();
    }
}

// Review 2026-09-27: bodies selected, two quick taps on two neighbouring
// bodies (the recognizer sees a double-tap) add both.
TEST(TouchMultiSelect, QuickTapsOnTwoNeighboursAddBoth)
{
    TwoBoxes s;
    const Uuid c = s.addBox("C", {62, 0, 0});
    s.controller.documentChanged();
    s.controller.fitAll(false);
    Finger finger(s);
    finger.doubleTap(s.topA());
    ASSERT_TRUE(s.bodiesSelected({s.a})) << finger.trace();
    const Vec2 onB = s.screen({59, 10, 20});
    const Vec2 onC = s.screen({63, 10, 20});
    const InputProfile touch = InputProfile::forDevice(PointerDevice::Touch);
    ASSERT_EQ(s.controller.pickAt(onB, touch).bodyId, s.b);
    ASSERT_EQ(s.controller.pickAt(onC, touch).bodyId, c);
    ASSERT_LT((onC - onB).length(), 32.0);
    finger.tap(onB);
    finger.pause(0.1);
    finger.tap(onC);
    finger.pause(1.0);
    EXPECT_TRUE(s.bodiesSelected({s.a, s.b, c})) << finger.trace();
}
