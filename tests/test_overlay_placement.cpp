// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where the value chip and the sketch's live values go: never over the
// selection, the arrows, the tapped point or under a finger. The placement
// is a pure function of screen rectangles (tested with the real layouts of
// an iPhone, an iPad and a desktop window); the keep-clear rectangle comes
// from the interaction layer (tested on a real box).

#include "commands/Command.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/OverlayPlacement.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

using namespace os;
using namespace os::interact;

namespace {

ScreenRect rect(double x, double y, double w, double h) { return ScreenRect::at({x, y}, {w, h}); }

std::string text(const ScreenRect& r)
{
    return std::to_string(int(r.left)) + "," + std::to_string(int(r.top)) + " " + std::to_string(int(r.width())) + "x"
         + std::to_string(int(r.height()));
}

// A window's layout as Main.qml builds it: the area inside the safe insets
// and the controls the chip must not cover.
struct Layout {
    const char* name;
    ScreenRect area;
    std::vector<ScreenRect> avoid;
    Vec2 chip;
    bool compact;
    bool touch;
};

// iPhone 16 Pro in portrait: 402x874, the Dynamic Island's 62 px on top, the
// home indicator's 34 below, 10 px margins; touch-sized controls.
Layout iphonePortrait()
{
    return {"iPhone portrait",
            rect(10, 72, 382, 758),
            {rect(10, 72, 250, 56),    // top bar
             rect(320, 72, 72, 56),    // Model button
             rect(320, 136, 72, 56),   // View button
             rect(328, 200, 64, 64),   // axis marker
             rect(10, 710, 382, 56),   // selection summary and hint
             rect(10, 774, 382, 56)},  // tool strip
            {298, 106},
            true,
            true};
}

// The same phone turned: 874x402, the island on the left, 21 px below.
Layout iphoneLandscape()
{
    return {"iPhone landscape",
            rect(72, 10, 730, 361),
            {rect(72, 10, 250, 56), rect(730, 10, 72, 56), rect(730, 74, 72, 56), rect(738, 138, 64, 64),
             rect(72, 251, 730, 56), rect(72, 315, 730, 56)},
            {298, 106},
            true,
            true};
}

// iPad Air 11" in landscape: 1180x820, touch, the regular layout.
Layout ipad()
{
    return {"iPad",
            rect(16, 16, 1148, 788),
            {rect(16, 16, 360, 56),     // top bar
             rect(16, 88, 105, 640),    // tool column
             rect(874, 16, 290, 120),   // Model panel
             rect(660, 748, 504, 56),   // view buttons
             rect(1084, 660, 80, 80),   // axis marker
             rect(16, 750, 628, 54)},   // selection summary and hint
            {300, 120},
            false,
            true};
}

// A desktop window: 1400x900, mouse.
Layout desktop()
{
    return {"desktop",
            rect(16, 16, 1368, 868),
            {rect(16, 16, 340, 48), rect(16, 80, 110, 720), rect(1094, 16, 290, 124), rect(850, 836, 534, 48),
             rect(1304, 748, 80, 80), rect(16, 830, 814, 54)},
            {280, 94},
            false,
            false};
}

ChipPlacementInput input(const Layout& layout, Vec2 tip, std::optional<ScreenRect> keepClear)
{
    ChipPlacementInput in;
    in.area = layout.area;
    in.avoid = layout.avoid;
    in.size = layout.chip;
    in.tip = tip;
    in.fieldCenter = layout.touch ? 28 : 24;
    in.keepClear = keepClear;
    in.compact = layout.compact;
    in.touch = layout.touch;
    return in;
}

ScreenRect chipRect(const ChipPlacement& p, const ChipPlacementInput& in) { return ScreenRect::at(p.position, in.size); }

bool coversControl(const ScreenRect& chip, const ChipPlacementInput& in)
{
    return std::any_of(in.avoid.begin(), in.avoid.end(), [&](const ScreenRect& a) { return a.intersects(chip); });
}

bool clearOfKeep(const ScreenRect& chip, const ChipPlacementInput& in)
{
    return !in.keepClear || !in.keepClear->inflated(chipMargin(in.touch)).intersects(chip);
}

// A spot for the chip inside the area, off the controls (with their gap) and
// clear of the keep-clear rectangle, found by trying positions every `step` px.
bool freeSpotExists(const ChipPlacementInput& in, double step)
{
    for (double y = in.area.top; y + in.size.y <= in.area.bottom; y += step)
        for (double x = in.area.left; x + in.size.x <= in.area.right; x += step) {
            const ScreenRect r = ScreenRect::at({x, y}, in.size);
            const bool offControls = std::none_of(in.avoid.begin(), in.avoid.end(),
                                                  [&](const ScreenRect& a) { return a.inflated(kChipPanelGap).intersects(r); });
            if (offControls && clearOfKeep(r, in))
                return true;
        }
    return false;
}

} // namespace

TEST(ScreenRect, IntersectionAndSeparation)
{
    const ScreenRect a = rect(0, 0, 100, 50);
    EXPECT_TRUE(a.intersects(rect(50, 25, 100, 100)));
    EXPECT_FALSE(a.intersects(rect(100, 0, 10, 10))) << "touching edges do not overlap";
    // A vertical edge seen on screen is a rectangle of zero width.
    EXPECT_TRUE(a.intersects(rect(40, -10, 0, 100)));
    EXPECT_FALSE(a.intersects(rect(140, -10, 0, 100)));
    EXPECT_DOUBLE_EQ(separation(a, rect(130, 0, 10, 10)), 30.0);
    EXPECT_DOUBLE_EQ(separation(a, rect(0, 90, 10, 10)), 40.0);
    EXPECT_DOUBLE_EQ(separation(a, rect(103, 54, 10, 10)), 5.0) << "diagonally apart: 3-4-5";
    EXPECT_DOUBLE_EQ(separation(a, rect(90, 40, 100, 100)), -10.0) << "overlapping 10 x 10";
    EXPECT_DOUBLE_EQ(separation(a, rect(60, 45, 100, 100)), -5.0) << "the smaller overlap counts";
    const ScreenRect u = a.united(rect(-10, 20, 5, 100));
    EXPECT_DOUBLE_EQ(u.left, -10);
    EXPECT_DOUBLE_EQ(u.bottom, 120);
    EXPECT_FALSE(a.clippedTo(rect(200, 200, 10, 10)).has_value());
    EXPECT_DOUBLE_EQ(a.clippedTo(rect(50, -20, 500, 40))->height(), 20);
}

// The owner's case: an edge on the left of a body, low in a phone's window,
// the arrow pointing left. The chip docks below the top bar, clear of it all,
// beside the Model / View buttons.
TEST(ChipPlacement, PhoneDocksAwayFromTheSelection)
{
    const Layout phone = iphonePortrait();
    // The edge from y 395 to 675 at x 160, the arrow to x 70, the tap on the edge.
    const ScreenRect edge = rect(60, 395, 105, 280);
    auto in = input(phone, {70, 530}, edge);
    const ChipPlacement low = placeValueChip(in);
    EXPECT_EQ(low.spot, ChipSpot::DockTop);
    EXPECT_TRUE(low.clear);
    const ScreenRect chip = chipRect(low, in);
    EXPECT_TRUE(phone.area.contains(chip)) << text(chip);
    EXPECT_FALSE(coversControl(chip, in)) << text(chip);
    EXPECT_DOUBLE_EQ(chip.top, 72 + 56 + kChipPanelGap) << "just below the top bar";
    EXPECT_DOUBLE_EQ(chip.left, 10);

    // A face high up: the chip docks above the hint.
    in.keepClear = rect(80, 150, 220, 250);
    in.tip = {190, 160};
    const ChipPlacement high = placeValueChip(in);
    EXPECT_EQ(high.spot, ChipSpot::DockBottom);
    EXPECT_TRUE(high.clear);
    const ScreenRect chipLow = chipRect(high, in);
    EXPECT_DOUBLE_EQ(chipLow.bottom, 710 - kChipPanelGap) << "just above the hint";
    EXPECT_FALSE(coversControl(chipLow, in));

    // Nothing to keep clear: above the hint (near the thumb).
    in.keepClear.reset();
    EXPECT_EQ(placeValueChip(in).spot, ChipSpot::DockBottom);
}

TEST(ChipPlacement, PhoneKeepsItsSideWhileDragging)
{
    const Layout phone = iphonePortrait();
    // Starts with the face high up: docked at the bottom.
    auto in = input(phone, {190, 280}, rect(80, 270, 220, 130));
    ChipPlacement p = placeValueChip(in);
    ASSERT_EQ(p.spot, ChipSpot::DockBottom);
    // The arrow is dragged down over the dock: the chip stays (no jumping).
    in.frozen = true;
    for (double y = 280; y <= 700; y += 20) {
        in.tip = {190, y};
        in.keepClear = rect(80, 270, 220, 130).united(ScreenRect::around(in.tip).inflated(10));
        p = placeValueChip(in, p.spot);
        EXPECT_EQ(p.spot, ChipSpot::DockBottom) << "while dragging, at y " << y;
    }
    // Let go with the arrow under it: now it moves up, out of the way.
    in.frozen = false;
    p = placeValueChip(in, p.spot);
    EXPECT_EQ(p.spot, ChipSpot::DockTop);
    EXPECT_TRUE(p.clear);
    // And stays there when the arrow goes back up but the top stays clear...
    in.keepClear = rect(80, 300, 220, 250);
    p = placeValueChip(in, p.spot);
    EXPECT_EQ(p.spot, ChipSpot::DockTop);
    // ...even though the bottom would now be farther: no flicker.
    EXPECT_TRUE(p.clear);
}

TEST(ChipPlacement, PhoneInLandscapeDocksBesideTheTopBar)
{
    const Layout phone = iphoneLandscape();
    // An edge on the left, low: the top row beside the top bar is farther.
    auto in = input(phone, {120, 230}, rect(100, 150, 120, 150));
    const ChipPlacement p = placeValueChip(in);
    const ScreenRect chip = chipRect(p, in);
    EXPECT_EQ(p.spot, ChipSpot::DockTop);
    EXPECT_TRUE(p.clear) << text(chip);
    EXPECT_DOUBLE_EQ(chip.top, 10) << "in the top bar's row";
    EXPECT_DOUBLE_EQ(chip.left, 72 + 250 + kChipPanelGap);
    EXPECT_FALSE(coversControl(chip, in));
    EXPECT_TRUE(phone.area.contains(chip));
}

// Larger windows keep the chip beside the arrow tip, on the first side that
// leaves the selection clear.
TEST(ChipPlacement, BesideTheTipOnTheFreeSide)
{
    for (const Layout& layout : {ipad(), desktop()}) {
        SCOPED_TRACE(layout.name);
        const Vec2 tip{600, 400};
        // An arrow pointing up out of an edge below it: right of the tip.
        auto in = input(layout, tip, rect(595, 395, 10, 200));
        ChipPlacement p = placeValueChip(in);
        EXPECT_EQ(p.spot, ChipSpot::Right);
        EXPECT_TRUE(p.clear);
        EXPECT_DOUBLE_EQ(p.position.x, tip.x + chipTipGap(layout.touch));
        EXPECT_DOUBLE_EQ(p.position.y, tip.y - in.fieldCenter) << "the field level with the tip";
        // A face right of the tip (its arrow points left): right of the tip
        // still, just past the face, level with the tip.
        in.keepClear = rect(590, 300, 250, 240);
        p = placeValueChip(in);
        EXPECT_EQ(p.spot, ChipSpot::Right);
        EXPECT_TRUE(p.clear);
        EXPECT_DOUBLE_EQ(p.position.x, 840 + chipMargin(layout.touch));
        EXPECT_DOUBLE_EQ(p.position.y, tip.y - in.fieldCenter);
        // The selection reaches right of the tip to the window's edge: left of it.
        in.keepClear = rect(590, 380, 800, 160);
        p = placeValueChip(in);
        EXPECT_EQ(p.spot, ChipSpot::Left);
        EXPECT_TRUE(clearOfKeep(chipRect(p, in), in));
        // A selection across the window, level with the tip: above it.
        in.keepClear = rect(16, 390, 1400, 150);
        p = placeValueChip(in);
        EXPECT_EQ(p.spot, ChipSpot::Above);
        EXPECT_TRUE(clearOfKeep(chipRect(p, in), in));
        // ... and reaching up to the top: below it.
        in.keepClear = rect(16, 16, 1400, 394);
        p = placeValueChip(in);
        EXPECT_EQ(p.spot, ChipSpot::Below);
        EXPECT_TRUE(clearOfKeep(chipRect(p, in), in));
        for (const ChipSpot spot : {ChipSpot::Right, ChipSpot::Left, ChipSpot::Above, ChipSpot::Below})
            EXPECT_NE(toString(spot), "none");
    }
}

TEST(ChipPlacement, CornerThenDockWhenNothingBesideTheTipIsFree)
{
    const Layout layout = ipad();
    // A face filling the window's lower right, the tip below the top bar:
    // nothing beside the tip (the tools and the top bar are in the way), so
    // the free corner nearest to the tip.
    auto in = input(layout, {250, 300}, rect(130, 200, 1034, 604));
    ChipPlacement p = placeValueChip(in);
    // (Searched from the bottom left, the lowest free spot there is just above
    // the face, beside the top bar: nearer to the tip than the top row.)
    EXPECT_EQ(p.spot, ChipSpot::BottomLeft) << toString(p.spot);
    EXPECT_DOUBLE_EQ(p.position.x, 376 + kChipPanelGap) << "beside the top bar";
    EXPECT_DOUBLE_EQ(p.position.y + in.size.y, 200 - chipMargin(true)) << "just above the face";
    EXPECT_TRUE(p.clear);
    // A face filling the middle of the window: above it, past the face.
    in.tip = {700, 300};
    in.keepClear = rect(200, 200, 800, 510);
    p = placeValueChip(in);
    EXPECT_EQ(p.spot, ChipSpot::Above) << toString(p.spot);
    EXPECT_DOUBLE_EQ(p.position.y + in.size.y, 200 - chipMargin(true)) << "just above the face";
    EXPECT_TRUE(p.clear);
    const ScreenRect chip = chipRect(p, in);
    EXPECT_FALSE(coversControl(chip, in)) << text(chip);
    EXPECT_TRUE(layout.area.contains(chip));
    // Zoomed in so far that the face covers the whole window: docked, as on a phone.
    in.keepClear = layout.area;
    p = placeValueChip(in);
    EXPECT_TRUE(p.spot == ChipSpot::DockTop || p.spot == ChipSpot::DockBottom) << toString(p.spot);
    EXPECT_FALSE(p.clear);
    EXPECT_FALSE(coversControl(chipRect(p, in), in));
}

// The chip moves along with the arrow while its side stays free: small moves
// never make it jump.
TEST(ChipPlacement, StableForSmallMovesOfTheArrow)
{
    for (const Layout& layout : {ipad(), desktop()}) {
        SCOPED_TRACE(layout.name);
        const ScreenRect face = rect(480, 420, 200, 140);
        auto in = input(layout, {580, 330}, face.united(rect(575, 330, 10, 90)));
        ChipPlacement p = placeValueChip(in);
        const ChipSpot first = p.spot;
        for (int i = 1; i <= 120; ++i) {
            const Vec2 tip{580.0 + 0.5 * i, 330.0 - 1.0 * i}; // dragged up and a little right
            in.tip = tip;
            in.keepClear = face.united(rect(tip.x - 5, tip.y, 10, 420 - tip.y));
            const ChipPlacement next = placeValueChip(in, p.spot);
            EXPECT_EQ(next.spot, first) << "step " << i;
            EXPECT_LE((next.position - p.position).length(), 1.2) << "moves with the tip, step " << i;
            EXPECT_TRUE(next.clear);
            p = next;
        }
    }
}

// Many random selections and tips on each window: the chip stays inside the
// safe area, off the controls, and clear of the keep-clear rectangle whenever
// any free spot exists (phones: whenever one of its docks is clear).
TEST(ChipPlacement, NeverCoversTheKeepClearWhenThereIsRoom)
{
    std::mt19937 random(12345);
    for (const Layout& layout : {iphonePortrait(), iphoneLandscape(), ipad(), desktop()}) {
        SCOPED_TRACE(layout.name);
        std::uniform_real_distribution<double> px(layout.area.left, layout.area.right);
        std::uniform_real_distribution<double> py(layout.area.top, layout.area.bottom);
        std::uniform_real_distribution<double> extent(0, 0.5);
        int clear = 0;
        int searched = 0;
        const int cases = 400;
        for (int i = 0; i < cases; ++i) {
            const Vec2 tip{px(random), py(random)};
            // Every other case zoomed in further: selections up to the whole window.
            const double w = layout.area.width() * (i % 2 ? 1.0 : 1.8), h = layout.area.height() * (i % 2 ? 1.0 : 1.8);
            ScreenRect keep{tip.x - extent(random) * w, tip.y - extent(random) * h, tip.x + extent(random) * w, tip.y + extent(random) * h};
            keep = *keep.clippedTo(layout.area);
            const auto in = input(layout, tip, keep);
            const ChipPlacement p = placeValueChip(in);
            const ScreenRect chip = chipRect(p, in);
            ASSERT_TRUE(layout.area.contains(chip)) << "case " << i << ": " << text(chip);
            ASSERT_FALSE(coversControl(chip, in)) << "case " << i << ": " << text(chip);
            ASSERT_EQ(p.clear, clearOfKeep(chip, in)) << "case " << i;
            if (layout.compact) {
                // Either dock, as it would be kept during a drag.
                auto frozen = in;
                frozen.frozen = true;
                const bool topClear = placeValueChip(frozen, ChipSpot::DockTop).clear;
                const bool bottomClear = placeValueChip(frozen, ChipSpot::DockBottom).clear;
                ASSERT_EQ(p.clear, topClear || bottomClear) << "case " << i << ": keep " << text(keep);
                ASSERT_TRUE(p.spot == ChipSpot::DockTop || p.spot == ChipSpot::DockBottom);
            } else if (!p.clear) {
                ASSERT_FALSE(freeSpotExists(in, 6)) << "case " << i << ": a free spot was missed; keep " << text(keep);
                ++searched;
            }
            clear += p.clear ? 1 : 0;
        }
        std::printf("%s: %d of %d clear, %d without room checked\n", layout.name, clear, cases, searched);
        EXPECT_GT(clear, cases / 5) << "many random selections leave room";
        EXPECT_TRUE(layout.compact || searched > 5) << "some selections leave no room at all";
    }
}

// ---- The live values of a sketch beside a finger -------------------------------

TEST(FingerLabels, MoveAboveTheFingerOnlyWhenHidden)
{
    const Vec2 finger{600, 500};
    const Vec2 size{88, 24};
    const Vec2 viewport{1180, 820};
    // Right beside the finger (a circle's diameter), below it (a width), and far away.
    std::vector<Vec2> centers{finger + Vec2{40, -18}, finger + Vec2{-10, 40}, {200, 200}};
    keepLabelsClearOfFinger(centers, size, finger, viewport);
    const ScreenRect shadow = fingerShadow(finger);
    for (const Vec2 c : centers) {
        const ScreenRect box = ScreenRect::at(c - size * 0.5, size);
        EXPECT_FALSE(box.intersects(shadow)) << text(box);
    }
    EXPECT_DOUBLE_EQ(centers[0].x, finger.x);
    EXPECT_LT(centers[0].y, finger.y - 36) << "above the fingertip";
    EXPECT_LT(centers[1].y, centers[0].y) << "stacked in order";
    EXPECT_DOUBLE_EQ(centers[2].x, 200) << "a label away from the finger stays";
    EXPECT_DOUBLE_EQ(centers[2].y, 200);
    // Near the top of the window: beside the finger instead (the side with room).
    std::vector<Vec2> top{Vec2{1000, 90} + Vec2{40, -18}};
    keepLabelsClearOfFinger(top, size, {1000, 90}, viewport);
    EXPECT_FALSE(ScreenRect::at(top[0] - size * 0.5, size).intersects(fingerShadow({1000, 90})));
    EXPECT_LT(top[0].x, 1000.0) << "to the left: more room there";
    EXPECT_GE(top[0].y - size.y / 2, 90.0) << "below the top bar";
}

// ---- The keep-clear rectangle of a real selection ------------------------------

namespace {

struct Scene {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};

    explicit Scene(Vec2 viewport = {402, 874})
    {
        controller.setViewportSize(viewport);
        EXPECT_TRUE(controller.createBox(20).ok());
        controller.fitAll(false);
    }

    static PointerEvent at(Vec2 p, PointerDevice device = PointerDevice::Touch)
    {
        PointerEvent e;
        e.position = p;
        e.device = device;
        return e;
    }
    void tap(Vec2 p, PointerDevice device = PointerDevice::Touch)
    {
        controller.pointerPress(at(p, device));
        controller.pointerRelease(at(p, device));
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    bool inside(const ScreenRect& r, Vec2 p, double tolerance = 0.5) const
    {
        return p.x >= r.left - tolerance && p.x <= r.right + tolerance && p.y >= r.top - tolerance && p.y <= r.bottom + tolerance;
    }
};

} // namespace

TEST(KeepClear, EdgeArrowAndTap)
{
    Scene s;
    EXPECT_FALSE(s.controller.keepClearRect().has_value()) << "nothing selected, nothing tapped";
    // The box's left vertical edge (x = -10, y = -10), tapped in the middle.
    const Vec2 tapAt = s.screen({-10, -10, 10});
    s.tap(tapAt);
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Fillet");
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    EXPECT_TRUE(s.inside(*keep, s.screen({-10, -10, 0}))) << "the edge's lower end";
    EXPECT_TRUE(s.inside(*keep, s.screen({-10, -10, 20}))) << "the edge's upper end";
    EXPECT_TRUE(s.inside(*keep, *s.controller.valueLabelPosition())) << "the arrow tip";
    EXPECT_TRUE(s.inside(*keep, tapAt)) << "the tap";
    // Only the edge and its arrow: not the whole box.
    EXPECT_FALSE(s.inside(*keep, s.screen({10, 10, 20}))) << text(*keep);
    EXPECT_FALSE(s.controller.manipulatorDragging());

    // The view turns: the rectangle follows the edge; the old tap no longer counts.
    s.controller.twoFingerRotate(60, 0);
    const auto turned = s.controller.keepClearRect();
    ASSERT_TRUE(turned.has_value());
    EXPECT_TRUE(s.inside(*turned, s.screen({-10, -10, 0})));
    EXPECT_TRUE(s.inside(*turned, s.screen({-10, -10, 20})));
    EXPECT_TRUE(s.inside(*turned, *s.controller.valueLabelPosition()));
}

TEST(KeepClear, PushedFaceIsKeptClearWhereItMoved)
{
    Scene s({1180, 820});
    s.tap(s.screen({0, 0, 20}), PointerDevice::Mouse);
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Push/Pull");
    const auto before = s.controller.keepClearRect();
    ASSERT_TRUE(before.has_value());
    for (const Vec3 corner : {Vec3{-10, -10, 20}, Vec3{10, -10, 20}, Vec3{10, 10, 20}, Vec3{-10, 10, 20}})
        EXPECT_TRUE(s.inside(*before, s.screen(corner))) << "a corner of the top face";
    // Pushed up to 30 mm: the face is there now (the arrow sits on it).
    EXPECT_EQ(s.controller.setValueText("30"), "");
    const auto after = s.controller.keepClearRect();
    ASSERT_TRUE(after.has_value());
    for (const Vec3 corner : {Vec3{-10, -10, 30}, Vec3{10, -10, 30}, Vec3{10, 10, 30}, Vec3{-10, 10, 30}})
        EXPECT_TRUE(s.inside(*after, s.screen(corner), 1.0)) << "a corner of the pushed face";
    EXPECT_TRUE(s.inside(*after, *s.controller.valueLabelPosition()));
}

TEST(KeepClear, BodyAndItsArrows)
{
    Scene s({1400, 900});
    const Vec2 top = s.screen({0, 0, 20});
    s.tap(top, PointerDevice::Mouse);
    s.controller.pointerDoubleClick(Scene::at(top, PointerDevice::Mouse));
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Move");
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    for (int c = 0; c < 8; ++c) {
        const Vec3 corner{c & 1 ? 10.0 : -10.0, c & 2 ? 10.0 : -10.0, c & 4 ? 20.0 : 0.0};
        EXPECT_TRUE(s.inside(*keep, s.screen(corner), 1.0)) << "body corner " << c;
    }
    for (int i = 0; i < s.controller.operation()->handleCount(); ++i) {
        const LinearManipulator handle = s.controller.operation()->handle(i);
        const Vec3 anchor = handle.anchor(s.controller.operation()->handleOffset(i));
        const Vec3 tip = anchor + handle.direction() * (ArrowStyle{}.totalPx() * s.controller.camera().pixelSize(anchor));
        EXPECT_TRUE(s.inside(*keep, s.screen(tip))) << "arrow " << i;
    }
}

// Through the controller: the spot is remembered for the selection, and a new
// selection chooses afresh.
TEST(KeepClear, PlacementRemembersItsSpotPerSelection)
{
    Scene s;
    const Layout phone = iphonePortrait();
    s.tap(s.screen({-10, -10, 10}));
    ASSERT_NE(s.controller.operation(), nullptr);
    auto in = input(phone, *s.controller.valueLabelPosition(), s.controller.keepClearRect());
    const ChipPlacement first = s.controller.placeValueChip(in);
    EXPECT_TRUE(first.clear);
    const ScreenRect chip = chipRect(first, in);
    EXPECT_FALSE(chip.intersects(*s.controller.keepClearRect()));
    // The same selection: a keep-clear that would favour the other dock does
    // not move it while its own dock stays clear.
    const ChipSpot other = first.spot == ChipSpot::DockTop ? ChipSpot::DockBottom : ChipSpot::DockTop;
    in.keepClear = first.spot == ChipSpot::DockTop ? rect(60, 280, 50, 50) : rect(60, 520, 50, 50);
    EXPECT_EQ(s.controller.placeValueChip(in).spot, first.spot);
    // A new selection chooses afresh: the farther dock.
    s.tap(s.screen({10, -10, 10}));
    ASSERT_NE(s.controller.operation(), nullptr);
    EXPECT_EQ(s.controller.placeValueChip(in).spot, other);
}

// ---- Sketch labels under a finger ------------------------------------------------

TEST(FingerLabels, RectangleDrawnByTouchKeepsItsValuesVisible)
{
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    controller.setViewportSize({1180, 820});
    controller.fitAll(false);
    ASSERT_TRUE(controller.startSketch().ok());
    controller.skipAnimation();
    controller.setSketchTool(SketchTool::Rectangle);
    auto screen = [&](Vec2 local) { return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local)); };
    auto labelsNear = [&](PointerDevice device, Vec2 start) {
        controller.setSketchTool(SketchTool::Rectangle);
        PointerEvent e;
        e.device = device;
        e.position = screen(start);
        controller.pointerPress(e);
        const Vec2 corner = screen(start + Vec2{6, -4});
        for (int i = 1; i <= 8; ++i) {
            e.position = screen(start) + (corner - screen(start)) * (i / 8.0);
            controller.pointerMove(e);
        }
        std::vector<SketchLabel> inputs;
        for (const auto& label : controller.sketchSession()->labels(controller.camera()))
            if (label.kind == SketchLabel::Kind::Input)
                inputs.push_back(label);
        controller.pointerRelease(e);
        (void)controller.keyPress(Key::Escape);
        return std::make_pair(inputs, corner);
    };
    // With a finger, the small rectangle's width and height stay out from under it.
    const auto [touchLabels, finger] = labelsNear(PointerDevice::Touch, {0, 0});
    ASSERT_EQ(touchLabels.size(), 2u);
    for (const auto& label : touchLabels) {
        const ScreenRect box = ScreenRect::at(label.screen - SketchSession::kLiveLabelSize * 0.5, SketchSession::kLiveLabelSize);
        EXPECT_FALSE(box.intersects(fingerShadow(finger))) << label.key << " at " << text(box);
        EXPECT_LT(label.screen.y, finger.y) << label.key << " above the finger";
    }
    // With a mouse they stay beside the pointer, as before.
    controller.setTouchLayout(false);
    const auto [mouseLabels, pointer] = labelsNear(PointerDevice::Mouse, {20, 20});
    ASSERT_EQ(mouseLabels.size(), 2u);
    bool besidePointer = false;
    for (const auto& label : mouseLabels)
        besidePointer = besidePointer
                     || ScreenRect::at(label.screen - SketchSession::kLiveLabelSize * 0.5, SketchSession::kLiveLabelSize).intersects(fingerShadow(pointer));
    EXPECT_TRUE(besidePointer) << "a mouse hides nothing: the labels stay next to the corner";
}
