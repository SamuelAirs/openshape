// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Collecting faces and edges with a finger on a phone (the owner on his
// iPhone Duo, 2026-11-12, after designing with 0.3.1: "It's hard to select
// multiple faces at once, it either locks on to an edge (and you can't tap it
// again to unselect it), or it deselects everything"). test_multi_face taps
// the middles of large faces in a 1200 x 800 window; here the window is a
// phone's and the faces are a part's small ones (a pad's top is about 30 px
// wide: inside the 20 px a finger's edge zone reaches from either side), and
// the taps go through the gesture recognizer with a finger's timing.
#include "commands/Command.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchGestures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace os;
using namespace os::interact;

namespace {

using State = TouchPoint::State;
using Kind = TouchIntent::Kind;

constexpr Vec2 kIphone{402, 874};
constexpr Vec2 kDuoOuter{466, 678};
constexpr Vec2 kDuoInner{951, 669};

// A 60 x 30 x 5 plate with three 10 x 10 pads standing 5 mm on it, at x 5,
// 25 and 45 (y 10 to 20): 4500 mm^3.
geom::Shape paddedPlate()
{
    geom::Shape shape = geom::makeBox({0, 0, 0}, {60, 30, 5}).value();
    for (const double x : {5.0, 25.0, 45.0})
        shape = geom::booleanOp(shape, geom::makeBox({x, 10, 5}, {10, 10, 5}).value(), geom::BooleanKind::Union).value();
    return shape;
}

struct Plate {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    Uuid body;

    explicit Plate(Vec2 viewport = kIphone)
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        EXPECT_TRUE(controller.importBodies({{"Plate", paddedPlate()}}, "plate.step").ok());
        body = document.bodies().front()->id();
        controller.setTouchLayout(true);
        controller.setViewportSize(viewport);
        controller.fitAll(false);
        // importBodies leaves the body selected: start with nothing.
        (void)controller.triggerAction("clearSelection");
        if (!controller.selection().empty())
            controller.keyPress(Key::Escape);
        EXPECT_TRUE(controller.selection().empty());
    }

    Vec2 screen(Vec3 p) const { return controller.camera().project(p); }
    // The middle of pad `i`'s top (0, 1, 2), and of the plate's top in front of it.
    Vec3 padTop(int i) const { return {10.0 + 20.0 * i, 15, 10}; }
    Vec3 plateFront(int i) const { return {10.0 + 20.0 * i, 5, 5}; }

    sel::PickResult pickAt(Vec2 p) const { return controller.pickAt(p, InputProfile::forDevice(PointerDevice::Touch)); }
    // The face at a point well inside it (a mouse's pick: an edge only within 6 px).
    int faceAt(Vec3 p) const
    {
        const auto hit = controller.pickAt(screen(p), InputProfile());
        EXPECT_EQ(hit.kind, sel::PickKind::Face) << "no face at " << p.x << ", " << p.y << ", " << p.z;
        return hit.kind == sel::PickKind::Face ? hit.index : -1;
    }

    // "faces 3 7", "edges 12", "bodies", "none".
    std::string selectionText() const
    {
        const auto& items = controller.selection().items();
        if (items.empty())
            return "none";
        std::ostringstream out;
        const auto kind = items.front().kind;
        out << (kind == sel::SelectionKind::Body ? "bodies" : kind == sel::SelectionKind::Face ? "faces" : kind == sel::SelectionKind::Edge ? "edges" : "other");
        for (const auto& item : items) {
            if (item.kind != sel::SelectionKind::Body)
                out << ' ' << item.index;
            if (item.kind != kind)
                out << "(mixed)";
        }
        return out.str();
    }
    bool facesSelected(std::vector<int> faces) const
    {
        std::vector<int> selected;
        for (const auto& item : controller.selection().items()) {
            if (item.kind != sel::SelectionKind::Face)
                return false;
            selected.push_back(item.index);
        }
        std::sort(selected.begin(), selected.end());
        std::sort(faces.begin(), faces.end());
        return selected == faces;
    }
    std::size_t count(sel::SelectionKind kind) const
    {
        const auto& items = controller.selection().items();
        return static_cast<std::size_t>(
            std::count_if(items.begin(), items.end(), [&](const sel::SelectionItem& item) { return item.kind == kind; }));
    }
    // Within a finger's grab zone of the active operation's arrow.
    bool onArrow(Vec2 p) const
    {
        const Operation* op = controller.operation();
        const double tolerance = InputProfile::forDevice(PointerDevice::Touch).handleTolerance;
        for (int i = 0; op && i < op->handleCount(); ++i)
            if (op->handle(i).hitTest(controller.camera(), p, op->handleOffset(i), tolerance))
                return true;
        return false;
    }
};

// One finger with a clock: frames through the recognizer, its intents to the
// controller as ViewportItem::touchEvent routes them.
struct Finger {
    Plate& scene;
    TouchGestureRecognizer recognizer;
    double time = 100;
    int id = 0;
    std::vector<std::string> log;

    explicit Finger(Plate& s) : scene(s) {}

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
                log.push_back(std::string(what) + " -> " + scene.selectionText());
        }
    }
    // A tap of 80 ms, then a second's rest (never half of a double-tap).
    void tap(Vec2 p, double rest = 1.0)
    {
        ++id;
        frame({{id, p, State::Pressed}});
        time += 0.08;
        frame({{id, p, State::Released}});
        time += rest;
    }
    void tap(Vec3 p, double rest = 1.0) { tap(scene.screen(p), rest); }
    void drag(Vec2 from, Vec2 to, int steps = 12)
    {
        ++id;
        frame({{id, from, State::Pressed}});
        for (int i = 1; i <= steps; ++i) {
            time += 0.016;
            frame({{id, from + (to - from) * (double(i) / steps), State::Moved}});
        }
        time += 0.016;
        frame({{id, to, State::Released}});
        time += 1.0;
    }
    std::string trace() const
    {
        std::string out;
        for (const auto& line : log)
            out += "\n    " + line;
        return out;
    }
};

std::string sizeText(Vec2 viewport) { return std::to_string(int(viewport.x)) + " x " + std::to_string(int(viewport.y)); }

// The first point from `from` towards `to` where a finger picks `kind`.
std::optional<Vec2> firstPick(const Plate& s, Vec2 from, Vec2 to, sel::PickKind kind)
{
    const double length = (to - from).length();
    for (double t = 0; t <= length; t += 0.5) {
        const Vec2 p = from + (to - from) * (t / length);
        if (s.pickAt(p).kind == kind)
            return p;
    }
    return std::nullopt;
}

} // namespace

// A pad's top is narrower than a finger's two edge zones: its middle is the
// face all the same (it was an edge, wherever the finger landed).
TEST(TouchFaces, TheMiddleOfASmallFaceIsTheFace)
{
    for (const Vec2 viewport : {kIphone, kDuoOuter, kDuoInner}) {
        Plate s(viewport);
        Finger finger(s);
        const Vec2 top = s.screen(s.padTop(1));
        const double width = (s.screen({25, 15, 10}) - s.screen({35, 15, 10})).length();
        if (viewport.x < 500) {
            ASSERT_LT(width, 40.0) << "the pad is narrower than two edge zones on a phone, " << sizeText(viewport);
        }
        finger.tap(top);
        ASSERT_TRUE(s.facesSelected({s.faceAt(s.padTop(1))})) << sizeText(viewport) << " (pad " << width << " px wide)" << finger.trace();
        ASSERT_NE(s.controller.operation(), nullptr);
        EXPECT_EQ(s.controller.operation()->title(), "Push/Pull");
    }
}

// ...and its edges are still there to take: in the outer quarter of the pad,
// and anywhere within a finger's reach outside it on the wide plate.
TEST(TouchFaces, TheEdgesOfASmallFaceAreStillInReach)
{
    Plate s;
    const Vec2 middle = s.screen(s.padTop(1));
    const Vec2 rim = s.screen({25, 15, 10}); // the middle of the pad top's -X edge
    const auto first = firstPick(s, middle, rim, sel::PickKind::Edge);
    ASSERT_TRUE(first.has_value()) << "the edge is picked before the finger reaches it";
    const double fromEdge = (*first - rim).length();
    const double half = (middle - rim).length();
    EXPECT_GT(fromEdge, 3.0) << "more than right on the line";
    EXPECT_LT(fromEdge, half * 0.75) << "the middle half of the pad stays the face's";
    EXPECT_EQ(s.pickAt(rim).kind, sel::PickKind::Edge);
}

// The report: one face, then more. Every tap lands on a small face, near
// its edges as a finger does.
TEST(TouchFaces, TapsOnSmallFacesAddThem)
{
    for (const Vec2 viewport : {kIphone, kDuoOuter, kDuoInner}) {
        Plate s(viewport);
        Finger finger(s);
        std::vector<int> expected;
        for (int i = 0; i < 3; ++i) {
            // A little off the middle each time, towards a different edge.
            const Vec3 p = s.padTop(i) + Vec3{i == 0 ? 1.5 : i == 1 ? -1.5 : 0.0, i == 2 ? 1.5 : 0.0, 0};
            finger.tap(p);
            expected.push_back(s.faceAt(s.padTop(i)));
            ASSERT_TRUE(s.facesSelected(expected)) << sizeText(viewport) << " after pad " << i << ": " << s.selectionText() << finger.trace();
        }
        ASSERT_NE(s.controller.operation(), nullptr);
        EXPECT_EQ(s.controller.operation()->title(), "Push/Pull") << "three tops facing up: one arrow";
        EXPECT_EQ(s.stack.size(), 1u) << "selecting changed the document";
    }
}

// With faces selected, a tap beside or right on an edge is a tap on the face
// under the finger: it never turns the faces collected so far into one edge.
TEST(TouchFaces, ATapOnAnEdgeAddsTheFaceUnderTheFinger)
{
    for (const Vec2 viewport : {kIphone, kDuoOuter, kDuoInner}) {
        Plate s(viewport);
        Finger finger(s);
        finger.tap(s.padTop(0));
        finger.tap(s.padTop(1));
        ASSERT_EQ(s.count(sel::SelectionKind::Face), 2u) << finger.trace();
        // The plate's top, 3 px inside its front edge: an edge for a finger.
        const Vec2 edge = s.screen({30, 0, 5});
        const Vec2 inward = s.screen({30, 5, 5}) - edge;
        const Vec2 nearEdge = edge + inward * (3.0 / inward.length());
        ASSERT_EQ(s.pickAt(nearEdge).kind, sel::PickKind::Edge) << "an edge when nothing is selected";
        finger.tap(nearEdge);
        EXPECT_EQ(s.count(sel::SelectionKind::Face), 3u) << sizeText(viewport) << ": " << s.selectionText() << finger.trace();
        EXPECT_EQ(s.count(sel::SelectionKind::Edge), 0u) << finger.trace();
        // Tapped again there: that face goes, the two pads stay.
        finger.tap(nearEdge);
        EXPECT_TRUE(s.facesSelected({s.faceAt(s.padTop(0)), s.faceAt(s.padTop(1))})) << s.selectionText() << finger.trace();
    }
}

// An edge never takes over from a face: the tap right on an edge's line
// adds the face under the finger and says how to pick the edge instead.
// From empty space the edge is an edge again.
TEST(TouchFaces, AnEdgeNeverTakesOverFromAFace)
{
    Plate s;
    Finger finger(s);
    const Vec2 edge = s.screen({50, 0, 5}); // on the plate top's front edge
    const Vec2 inward = s.screen({50, 5, 5}) - edge;
    const Vec2 onLine = edge + inward * (2.0 / inward.length());
    ASSERT_EQ(s.pickAt(onLine).kind, sel::PickKind::Edge) << "an edge when nothing is selected";
    finger.tap(s.padTop(0));
    ASSERT_TRUE(s.facesSelected({s.faceAt(s.padTop(0))})) << finger.trace();
    s.messages.clear();
    finger.tap(onLine);
    EXPECT_TRUE(s.facesSelected({s.faceAt(s.padTop(0)), s.faceAt(s.plateFront(1))})) << s.selectionText() << finger.trace();
    ASSERT_FALSE(s.messages.empty()) << "it says how to pick the edge instead";
    EXPECT_NE(s.messages.back().find("edge"), std::string::npos) << s.messages.back();

    finger.tap(Vec2{5, 5}); // empty space
    ASSERT_TRUE(s.controller.selection().empty()) << finger.trace();
    finger.tap(onLine);
    EXPECT_EQ(s.count(sel::SelectionKind::Edge), 1u) << s.selectionText() << finger.trace();
    EXPECT_EQ(s.controller.selection().size(), 1u);
}

// Several faces are not lost to a near miss: a tap just off the part, beside
// its outline, changes nothing and says how to go on; empty space clears.
TEST(TouchFaces, SeveralFacesSurviveATapJustOffThePart)
{
    Plate s;
    Finger finger(s);
    finger.tap(s.padTop(0));
    finger.tap(s.padTop(2));
    ASSERT_EQ(s.count(sel::SelectionKind::Face), 2u) << finger.trace();
    // Leaving the part at the middle of its right end, towards the right.
    const Vec2 inside = s.screen({60, 15, 2.5});
    const auto outside = firstPick(s, inside, inside + Vec2{80, 0}, sel::PickKind::None);
    ASSERT_TRUE(outside.has_value());
    const Vec2 justOff = *outside - Vec2{6, 0}; // still within a finger's reach of the outline
    ASSERT_FALSE(s.controller.pickAt(justOff, InputProfile()).hit()) << "nothing under the finger there";
    ASSERT_EQ(s.pickAt(justOff).kind, sel::PickKind::Edge) << "but an edge within its reach";
    s.messages.clear();
    finger.tap(justOff);
    EXPECT_EQ(s.count(sel::SelectionKind::Face), 2u) << s.selectionText() << finger.trace();
    ASSERT_FALSE(s.messages.empty()) << "it says how to add and how to start again";
    EXPECT_NE(s.messages.back().find("empty space"), std::string::npos) << s.messages.back();
    finger.tap(*outside + Vec2{30, 0});
    EXPECT_TRUE(s.controller.selection().empty()) << finger.trace();
}

// "You can't tap it again to unselect it": the arrow of Push/Pull stands on
// the selected face, and its finger zone (28 px) covers a small face whole.
TEST(TouchFaces, ASelectedFaceUnderItsArrowIsTakenOutByATap)
{
    for (const Vec2 viewport : {kIphone, kDuoOuter}) {
        Plate s(viewport);
        Finger finger(s);
        const Vec2 top = s.screen(s.padTop(1));
        finger.tap(top);
        ASSERT_TRUE(s.facesSelected({s.faceAt(s.padTop(1))})) << finger.trace();
        ASSERT_TRUE(s.onArrow(top)) << "the tap is in the arrow's zone";
        finger.tap(top);
        EXPECT_TRUE(s.controller.selection().empty()) << sizeText(viewport) << ": " << s.selectionText() << finger.trace();
        EXPECT_EQ(s.controller.operation(), nullptr);
        EXPECT_EQ(s.stack.size(), 1u) << "nothing was pushed or pulled";
    }
}

// The same for an edge and its Fillet arrow, which starts on the edge.
TEST(TouchFaces, ASelectedEdgeUnderItsArrowIsTakenOutByATap)
{
    for (const Vec2 viewport : {kIphone, kDuoOuter}) {
        Plate s(viewport);
        Finger finger(s);
        const Vec2 edge = s.screen({30, 0, 5}); // the middle of the plate top's front edge
        finger.tap(edge);
        ASSERT_EQ(s.count(sel::SelectionKind::Edge), 1u) << s.selectionText() << finger.trace();
        ASSERT_NE(s.controller.operation(), nullptr);
        ASSERT_TRUE(s.onArrow(edge)) << "the arrow starts on the edge";
        finger.tap(edge);
        EXPECT_TRUE(s.controller.selection().empty()) << sizeText(viewport) << ": " << s.selectionText() << finger.trace();
        EXPECT_EQ(s.stack.size(), 1u) << "no fillet was made";
    }
}

// A neighbour inside the arrow's zone is added by a tap; a drag from the
// same spot still pulls the arrow.
TEST(TouchFaces, AFaceBesideTheArrowIsAddedAndADragStillPulls)
{
    Plate s;
    Finger finger(s);
    finger.tap(s.padTop(1));
    ASSERT_EQ(s.count(sel::SelectionKind::Face), 1u) << finger.trace();
    // The arrow stands up from the pad's top, over the plate behind it on
    // screen: a point of the plate's top in its zone, not right on it.
    const Operation* op = s.controller.operation();
    ASSERT_NE(op, nullptr);
    std::optional<Vec2> beside;
    const int plate = s.faceAt(s.plateFront(1));
    for (double y = 21; y <= 29 && !beside; y += 0.5)
        for (double x = 22; x <= 38 && !beside; x += 0.5) {
            const Vec2 p = s.screen({x, y, 5});
            const auto hit = s.pickAt(p);
            const bool nearShaft = op->handle(0).hitTest(s.controller.camera(), p, op->handleOffset(0), 8.0).has_value();
            if (hit.kind == sel::PickKind::Face && hit.index == plate && s.onArrow(p) && !nearShaft)
                beside = p;
        }
    ASSERT_TRUE(beside.has_value()) << "on a phone the arrow's zone reaches the plate behind the pad";
    finger.tap(*beside);
    EXPECT_TRUE(s.facesSelected({s.faceAt(s.padTop(1)), plate})) << s.selectionText() << finger.trace();

    // One face again; a drag from the arrow's zone on it pulls it up.
    finger.tap(*beside);
    ASSERT_TRUE(s.facesSelected({s.faceAt(s.padTop(1))})) << s.selectionText() << finger.trace();
    const Vec2 top = s.screen(s.padTop(1));
    const Vec2 up = s.screen(s.padTop(1) + Vec3{0, 0, 10}) - top;
    finger.drag(top, top + up * (60.0 / up.length()));
    ASSERT_NE(s.controller.operation(), nullptr) << finger.trace();
    EXPECT_GT(s.controller.operation()->value(), 1.0) << "the drag pulled the face" << finger.trace();
    EXPECT_TRUE(s.facesSelected({s.faceAt(s.padTop(1))})) << s.selectionText();
}

// Two quick taps on two neighbouring faces are two taps (the recognizer
// sees a double-tap, which selects the whole body and dropped the faces).
TEST(TouchFaces, QuickTapsOnTwoFacesAddBoth)
{
    for (const Vec2 viewport : {kIphone, kDuoOuter}) {
        Plate s(viewport);
        Finger finger(s);
        finger.tap(s.padTop(0));
        ASSERT_EQ(s.count(sel::SelectionKind::Face), 1u) << finger.trace();
        // The pad's top near its front edge, and the plate just in front of it.
        const Vec2 onPad = s.screen({30, 12.5, 10});
        const Vec2 onPlate = s.screen({30, 6.5, 5});
        ASSERT_LT((onPad - onPlate).length(), TouchGestureRecognizer::kDoubleTapDistance) << sizeText(viewport);
        ASSERT_EQ(s.pickAt(onPad).index, s.faceAt(s.padTop(1)));
        ASSERT_EQ(s.pickAt(onPlate).index, s.faceAt(s.plateFront(1)));
        finger.tap(onPad, 0.1);
        finger.tap(onPlate);
        EXPECT_TRUE(s.facesSelected({s.faceAt(s.padTop(0)), s.faceAt(s.padTop(1)), s.faceAt(s.plateFront(1))}))
            << sizeText(viewport) << ": " << s.selectionText() << finger.trace();
    }
}

// A double-tap on one face is still the body, also with a face selected.
TEST(TouchFaces, ADoubleTapOnOneFaceStillSelectsTheBody)
{
    Plate s;
    Finger finger(s);
    finger.tap(s.padTop(0));
    ASSERT_EQ(s.count(sel::SelectionKind::Face), 1u) << finger.trace();
    const Vec2 front = s.screen(s.plateFront(2));
    finger.tap(front, 0.12);
    finger.tap(front + Vec2{3, -2});
    EXPECT_EQ(s.count(sel::SelectionKind::Body), 1u) << s.selectionText() << finger.trace();
    EXPECT_EQ(s.controller.selection().size(), 1u);
}

// Edges collected for a fillet: one more within a finger's reach is added
// (also across the narrow pad, whose middle is a face when nothing is
// selected); a tap in the open on a face keeps them and says so.
TEST(TouchFaces, SeveralEdgesAreKeptAndTakeMoreAtAFingersReach)
{
    Plate s;
    Finger finger(s);
    finger.tap(s.screen({30, 0, 5}));  // the plate top's front edge
    finger.tap(s.screen({30, 30, 5})); // and its back edge
    ASSERT_EQ(s.count(sel::SelectionKind::Edge), 2u) << s.selectionText() << finger.trace();
    ASSERT_NE(s.controller.operation(), nullptr);
    EXPECT_EQ(s.controller.operation()->title(), "Fillet");
    // The pad's middle: a face for a first tap, but its edges are within reach.
    ASSERT_EQ(s.pickAt(s.screen(s.padTop(1))).kind, sel::PickKind::Face);
    finger.tap(s.padTop(1));
    EXPECT_EQ(s.count(sel::SelectionKind::Edge), 3u) << s.selectionText() << finger.trace();
    // Closer, so that the plate's top has room more than a finger's reach
    // from every edge: a tap in the open there.
    s.controller.pinch(s.screen({20, 5, 5}), 3.0);
    const Vec2 open = s.screen({20, 5, 5});
    ASSERT_GT((open - s.screen({20, 0, 5})).length(), 30.0) << "from the front edge";
    ASSERT_GT((open - s.screen({15, 10, 5})).length(), 30.0) << "from the first pad";
    ASSERT_GT((open - s.screen({25, 10, 5})).length(), 30.0) << "from the second pad";
    s.messages.clear();
    finger.tap(open);
    EXPECT_EQ(s.count(sel::SelectionKind::Edge), 3u) << s.selectionText() << finger.trace();
    ASSERT_FALSE(s.messages.empty());
    EXPECT_NE(s.messages.back().find("edge"), std::string::npos) << s.messages.back();
    finger.tap(Vec2{-2000, -2000}); // empty space
    EXPECT_TRUE(s.controller.selection().empty()) << finger.trace();
}

// With the mouse nothing changes: a plain click replaces, an edge is an
// edge within 6 px, and the middle of a small face needs no help.
TEST(TouchFaces, TheMouseKeepsItsPlainClicksAndShiftAddsFaces)
{
    Plate s(Vec2{1200, 800});
    s.controller.setTouchLayout(false);
    auto click = [&](Vec2 p, bool shift = false) {
        PointerEvent e;
        e.device = PointerDevice::Mouse;
        e.position = p;
        e.modifiers.shift = shift;
        s.controller.pointerPress(e);
        s.controller.pointerRelease(e);
    };
    const Vec2 edge = s.screen({50, 0, 5});
    const Vec2 inward = s.screen({50, 5, 5}) - edge;
    click(s.screen(s.padTop(0)));
    ASSERT_EQ(s.count(sel::SelectionKind::Face), 1u);
    click(edge + inward * (4.0 / inward.length()));
    EXPECT_EQ(s.count(sel::SelectionKind::Edge), 1u) << "4 px from an edge is the edge: " << s.selectionText();
    EXPECT_EQ(s.controller.selection().size(), 1u);
    click(edge + inward * (9.0 / inward.length()));
    EXPECT_TRUE(s.facesSelected({s.faceAt(s.plateFront(2))})) << "9 px from it is the face: " << s.selectionText();
    // Shift adds faces; with two selected, Shift on an edge adds the face under the cursor.
    // (A pad whose top is clear of the plate's Push/Pull arrow: a click on
    // an arrow is the arrow's, with the mouse as before.)
    const Operation* op = s.controller.operation();
    ASSERT_NE(op, nullptr);
    int pad = -1;
    for (int i = 0; i < 3 && pad < 0; ++i)
        if (!op->handle(0).hitTest(s.controller.camera(), s.screen(s.padTop(i)), op->handleOffset(0), 24.0))
            pad = i;
    ASSERT_GE(pad, 0);
    click(s.screen(s.padTop(pad)), true);
    ASSERT_EQ(s.count(sel::SelectionKind::Face), 2u) << s.selectionText();
    click(s.screen(s.padTop(pad) + Vec3{0, 5, 0}) + Vec2{0, 2}, true);
    EXPECT_EQ(s.count(sel::SelectionKind::Edge), 0u) << s.selectionText();
    EXPECT_GE(s.count(sel::SelectionKind::Face), 1u) << s.selectionText();
}
