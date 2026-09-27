// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where the value chip and the sketch's live values go: never over the
// selection, the arrows, the tapped point or under a finger. The placement
// is a pure function of screen rectangles (tested with the real layouts of
// an iPhone, an iPad and a desktop window); the keep-clear rectangle comes
// from the interaction layer (tested on a real box).

#include "commands/Command.h"
#include "commands/DocumentCommands.h"
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

// The same phone turned: 874x402, the island on the left, 21 px below; the
// chip is one row (its actions beside the field), as wide as the room beside
// the top bar.
Layout iphoneLandscape()
{
    return {"iPhone landscape",
            rect(72, 10, 730, 361),
            {rect(72, 10, 250, 56), rect(730, 10, 72, 56), rect(730, 74, 72, 56), rect(738, 138, 64, 64),
             rect(72, 251, 730, 56), rect(72, 315, 730, 56)},
            {390, 56},
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

// Typing on a phone: the on-screen keyboard rises over the bottom half, so
// the chip goes below the top bar, even from a dock above the hint that was
// the farther one (the top face of a box, its arrow pointing up).
TEST(ChipPlacement, PhoneDocksAtTheTopWhileTyping)
{
    for (const Layout& phone : {iphonePortrait(), iphoneLandscape()}) {
        SCOPED_TRACE(phone.name);
        const bool portrait = phone.area.height() > phone.area.width();
        const ScreenRect face = portrait ? rect(110, 300, 180, 150) : rect(350, 90, 180, 110);
        auto in = input(phone, {face.center().x, face.top - 60}, face.united(rect(face.center().x - 5, face.top - 70, 10, 70)));
        const ChipPlacement before = placeValueChip(in);
        ASSERT_EQ(before.spot, ChipSpot::DockBottom) << "the farther dock";
        in.typing = true;
        const ChipPlacement typing = placeValueChip(in, before.spot);
        EXPECT_EQ(typing.spot, ChipSpot::DockTop);
        const ScreenRect chip = chipRect(typing, in);
        EXPECT_TRUE(phone.area.contains(chip)) << text(chip);
        EXPECT_FALSE(coversControl(chip, in)) << text(chip);
        EXPECT_LT(chip.bottom, phone.area.top + phone.area.height() / 2) << "in the upper half, above the keyboard";
        // Also while an arrow is dragged, and when nothing is kept clear.
        in.frozen = true;
        EXPECT_EQ(placeValueChip(in, ChipSpot::DockBottom).spot, ChipSpot::DockTop);
        in.keepClear.reset();
        EXPECT_EQ(placeValueChip(in, ChipSpot::DockBottom).spot, ChipSpot::DockTop);
        // Done typing: it stays where it is while that is clear (no jump back).
        in.frozen = false;
        in.typing = false;
        in.keepClear = face;
        const ChipPlacement after = placeValueChip(in, typing.spot);
        EXPECT_EQ(after.spot, ChipSpot::DockTop);
        EXPECT_TRUE(after.clear);
    }
}

// A larger window keeps the chip off an on-screen keyboard (an obstacle like
// the controls): it goes above the keyboard, still clear of the selection.
TEST(ChipPlacement, OffTheOnScreenKeyboard)
{
    const Layout layout = ipad();
    const ScreenRect face = rect(450, 500, 200, 150);
    auto in = input(layout, {550, 450}, face.united(rect(545, 440, 10, 60)));
    const ChipPlacement before = placeValueChip(in);
    ASSERT_TRUE(before.clear);
    const ScreenRect keyboard = rect(0, 820 - 400, 1180, 400);
    ASSERT_TRUE(chipRect(before, in).intersects(keyboard)) << "the keyboard would hide it";
    in.avoid.push_back(keyboard);
    in.typing = true;
    const ChipPlacement typing = placeValueChip(in, before.spot);
    const ScreenRect chip = chipRect(typing, in);
    EXPECT_FALSE(chip.intersects(keyboard)) << text(chip);
    EXPECT_TRUE(typing.clear) << text(chip);
    EXPECT_TRUE(layout.area.contains(chip)) << text(chip);
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
    const ScreenRect viewport = rect(0, 0, 1180, 820);
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

// A finger near the window's edge, or beside a phone's Dynamic Island: the
// moved labels are whole and inside the safe area, still clear of the finger.
TEST(FingerLabels, StayInsideTheSafeArea)
{
    const Vec2 size{88, 24};
    // iPhone in landscape: 874x402, the island on the left (62 px), 21 px below.
    const SafeInsets landscape{0, 62, 21, 62};
    const ScreenRect bounds = landscape.inside({874, 402});
    EXPECT_DOUBLE_EQ(bounds.left, 62);
    EXPECT_DOUBLE_EQ(bounds.right, 812);
    EXPECT_DOUBLE_EQ(bounds.bottom, 381);
    for (const Vec2 finger : {Vec2{70, 300}, Vec2{805, 300}, Vec2{70, 150}}) {
        SCOPED_TRACE(text(ScreenRect::around(finger)));
        // A rectangle's width and height, drawn toward the finger.
        std::vector<Vec2> centers{finger + Vec2{-30, -20}, finger + Vec2{-60, 10}};
        keepLabelsClearOfFinger(centers, size, finger, bounds);
        for (const Vec2 c : centers) {
            const ScreenRect box = ScreenRect::at(c - size * 0.5, size);
            EXPECT_TRUE(bounds.contains(box)) << text(box);
            EXPECT_FALSE(box.intersects(fingerShadow(finger))) << text(box);
        }
    }
    // Many values under a finger high on the screen: no room above, so they
    // stack beside it, starting high enough to end above the home indicator,
    // one below the other.
    {
        const Vec2 finger{70, 130};
        std::vector<Vec2> many;
        for (int i = 0; i < 12; ++i)
            many.push_back(finger + Vec2{-20.0 + 4 * i, -10.0 + 10 * i});
        keepLabelsClearOfFinger(many, size, finger, bounds);
        for (std::size_t i = 0; i < many.size(); ++i) {
            const ScreenRect box = ScreenRect::at(many[i] - size * 0.5, size);
            EXPECT_TRUE(bounds.contains(box)) << i << ": " << text(box);
            EXPECT_FALSE(box.intersects(fingerShadow(finger))) << i << ": " << text(box);
            if (i > 0) {
                EXPECT_GE(many[i].y - many[i - 1].y, size.y) << i << ": stacked, not piled up";
            }
        }
    }
    // Portrait, a finger at the very left edge: the labels start at the window's edge.
    const ScreenRect portrait = SafeInsets{62, 0, 34, 0}.inside({402, 874});
    std::vector<Vec2> centers{Vec2{20, 500} + Vec2{30, -20}};
    keepLabelsClearOfFinger(centers, size, {20, 500}, portrait);
    EXPECT_DOUBLE_EQ(centers[0].x, 44) << "the whole label on screen";
    EXPECT_LT(centers[0].y, 500 - 36) << "above the fingertip";
}

// ---- The keep-clear rectangle of a real selection ------------------------------

namespace {

struct Scene {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};

    // A 20 mm cube on the origin, (-10,-10,0) .. (10,10,20), unless `cube` is false.
    explicit Scene(Vec2 viewport = {402, 874}, bool cube = true)
    {
        controller.setViewportSize(viewport);
        if (cube) {
            EXPECT_TRUE(controller.createBox(20).ok());
        }
        controller.fitAll(false);
    }
    Uuid addBox(Vec3 origin, Vec3 size)
    {
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = origin;
        box->size = size;
        EXPECT_TRUE(stack.push(std::make_unique<cmd::CreateBodyCommand>("Box", std::move(box)), document).ok());
        controller.documentChanged();
        return document.bodies().back()->id();
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
    // The screen rectangle around the box lo .. hi (its eight corners), which
    // must be on screen.
    ScreenRect boxOnScreen(const Vec3& lo, const Vec3& hi) const
    {
        std::optional<ScreenRect> r;
        for (int c = 0; c < 8; ++c) {
            const Vec2 p = screen({c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z});
            EXPECT_TRUE(inside({0, 0, controller.camera().viewportSize.x, controller.camera().viewportSize.y}, p, 0))
                << "corner " << c << " off screen";
            if (r)
                r->include(p);
            else
                r = ScreenRect::around(p);
        }
        return *r;
    }
    // Grabs Move's arrow for `axis` where it is drawn (a click on it).
    void grabArrow(int axis)
    {
        const auto arrow = controller.renderScene().arrows.at(std::size_t(axis));
        const double px = controller.camera().pixelSize(arrow.anchor);
        const Vec2 p = screen(arrow.anchor + arrow.direction * (50 * px));
        controller.pointerPress(at(p, PointerDevice::Mouse));
        controller.pointerRelease(at(p, PointerDevice::Mouse));
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

namespace {
bool sameRect(const std::optional<ScreenRect>& a, const std::optional<ScreenRect>& b)
{
    return a.has_value() == b.has_value()
        && (!a
            || (std::abs(a->left - b->left) < 1e-9 && std::abs(a->top - b->top) < 1e-9 && std::abs(a->right - b->right) < 1e-9
                && std::abs(a->bottom - b->bottom) < 1e-9));
}
} // namespace

// Only a press that can select or act is kept clear: not a hand resting on
// the screen in pen mode, not a right click.
TEST(KeepClear, PressesThatDoNothingAreNotKeptClear)
{
    Scene s({1180, 820});
    const Vec2 edge = s.screen({-10, -10, 10});
    s.tap(edge, PointerDevice::Pen);
    ASSERT_TRUE(s.controller.penMode());
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Fillet");
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    EXPECT_TRUE(s.inside(*keep, edge)) << "the pen's tap";
    const Vec2 corner{1100, 780};
    ASSERT_FALSE(s.inside(*keep, corner));
    // A palm in the corner: in pen mode a finger only moves the view.
    s.tap(corner, PointerDevice::Touch);
    EXPECT_EQ(s.controller.operation()->title(), "Fillet");
    EXPECT_TRUE(sameRect(s.controller.keepClearRect(), keep)) << text(*s.controller.keepClearRect());
    // A right click there does nothing either.
    PointerEvent right = Scene::at(corner, PointerDevice::Mouse);
    right.button = PointerButton::Right;
    s.controller.pointerPress(right);
    s.controller.pointerRelease(right);
    EXPECT_EQ(s.controller.operation()->title(), "Fillet");
    EXPECT_TRUE(sameRect(s.controller.keepClearRect(), keep)) << text(*s.controller.keepClearRect());
    // The pen's tap on the edge again: still kept clear.
    s.tap(edge, PointerDevice::Pen);
    EXPECT_TRUE(s.inside(*s.controller.keepClearRect(), edge));
}

// A press is kept clear with the selection it made; a selection made some
// other way (the Model panel) forgets it, even with the view unchanged.
TEST(KeepClear, PressForgottenWhenTheSelectionChangesOtherwise)
{
    Scene s({1180, 820});
    const Vec2 empty{1100, 120};
    s.tap(empty, PointerDevice::Mouse);
    EXPECT_TRUE(s.controller.selection().empty());
    const auto pressOnly = s.controller.keepClearRect();
    ASSERT_TRUE(pressOnly.has_value());
    EXPECT_TRUE(s.inside(*pressOnly, empty)) << "nothing else: the press itself";
    // The body picked in the Model panel: its Move chip keeps clear of the
    // body and the arrows, not of the old click in the corner.
    ASSERT_EQ(s.document.bodies().size(), 1u);
    ASSERT_TRUE(s.controller.selectBody(s.document.bodies().front()->id(), false).ok());
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Move");
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    EXPECT_FALSE(s.inside(*keep, empty)) << text(*keep);
    EXPECT_TRUE(s.inside(*keep, s.screen({0, 0, 20}), 1.0)) << "the body";
    // A double-click selects the body too, and is kept clear with it.
    EXPECT_TRUE(s.controller.keyPress(Key::Escape));
    const Vec2 face = s.screen({0, -10, 10});
    s.tap(face, PointerDevice::Mouse);
    s.controller.pointerDoubleClick(Scene::at(face, PointerDevice::Mouse));
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Move");
    EXPECT_TRUE(s.inside(*s.controller.keepClearRect(), face));
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

// A selected construction plane is kept clear as it is drawn.
TEST(KeepClear, SelectedConstructionPlane)
{
    Scene s({1400, 900});
    ASSERT_TRUE(s.controller.runTool("plane").ok());
    ASSERT_TRUE(s.controller.triggerAction("datum:origin:0").ok()); // from YZ
    EXPECT_EQ(s.controller.setValueText("15"), "");
    ASSERT_TRUE(s.controller.commitOperation().ok());
    s.controller.cancelOperation();
    ASSERT_FALSE(s.document.datums().empty());
    const doc::Datum& plane = *s.document.datums().back();
    ASSERT_TRUE(s.controller.selectDatum(plane.id()).ok());
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    const auto shape = s.controller.datumShape(plane.geometry(), plane.kind());
    ASSERT_TRUE(shape.plane);
    // The kept-clear area ends at the window's edges (a corner may be off it).
    for (const Vec3& corner : shape.corners) {
        const Vec2 at = s.screen(corner);
        EXPECT_TRUE(s.inside(*keep, {std::clamp(at.x, 0.0, 1400.0), std::clamp(at.y, 0.0, 900.0)}, 1.0))
            << at.x << ", " << at.y;
    }
}

// A body moved on two axes is kept clear where it went (Move's arrows all
// start at the moved center: an arrow's own travel is one axis only), and the
// chip does not go beside the arrow tip onto it.
TEST(KeepClear, MovedBodyOnTwoAxes)
{
    Scene s({1400, 900});
    s.controller.wheel({700, 450}, -6); // room around the box for the move
    const Vec2 top = s.screen({0, 0, 20});
    s.tap(top, PointerDevice::Mouse);
    s.controller.pointerDoubleClick(Scene::at(top, PointerDevice::Mouse));
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Move");
    s.grabArrow(0);
    ASSERT_EQ(s.controller.operation()->valueLabel(), "X");
    ASSERT_EQ(s.controller.setValueText("30"), "");
    s.grabArrow(1);
    ASSERT_EQ(s.controller.operation()->valueLabel(), "Y");
    ASSERT_EQ(s.controller.setValueText("20"), "");
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    const ScreenRect moved = s.boxOnScreen({20, 10, 0}, {40, 30, 20});
    EXPECT_TRUE(keep->contains(moved, 1.0)) << "the moved body " << text(moved) << " in " << text(*keep);
    const ScreenRect before = s.boxOnScreen({-10, -10, 0}, {10, 10, 20});
    EXPECT_TRUE(keep->contains(before, 1.0)) << "where it was (selected)";
    // Where the chip goes on a desktop and an iPad-sized window: off the moved body.
    for (const Layout& layout : {desktop(), ipad()}) {
        const auto in = input(layout, *s.controller.valueLabelPosition(), keep);
        const ChipPlacement chip = placeValueChip(in);
        EXPECT_TRUE(chip.clear) << layout.name;
        EXPECT_FALSE(chipRect(chip, in).intersects(moved)) << layout.name << ": " << text(chipRect(chip, in));
    }
}

// A long bar turned a quarter reaches far out of its box and past the rings:
// it is kept clear as the preview shows it.
TEST(KeepClear, TurnedBodyAsThePreviewShowsIt)
{
    Scene s({1400, 900}, false);
    const Uuid bar = s.addBox({0, 0, 0}, {80, 10, 10});
    // From above: the bar across the screen, turned along it.
    s.controller.setStandardView(StandardView::Top, false);
    s.controller.fitAll(false);
    s.controller.wheel({700, 450}, -3);
    ASSERT_TRUE(s.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(s.controller.runTool("rotate").ok());
    ASSERT_NE(s.controller.operation(), nullptr);
    ASSERT_EQ(s.controller.operation()->title(), "Rotate");
    ASSERT_EQ(s.controller.setValueText("90"), ""); // about Z, through the bar's center (40, 5, 5)
    ASSERT_TRUE(s.controller.operation()->hasPreview());
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    const ScreenRect turned = s.boxOnScreen({35, -35, 0}, {45, 45, 10});
    EXPECT_TRUE(keep->contains(turned, 1.0)) << "the turned bar " << text(turned) << " in " << text(*keep);
}

// Align moves the whole body onto the target: kept clear where it lands.
TEST(KeepClear, AlignedBodyWhereItLands)
{
    Scene s({1400, 900}, false);
    s.addBox({0, 0, 0}, {10, 10, 10});
    s.addBox({40, 0, 0}, {20, 20, 20});
    s.controller.fitAll(false);
    s.tap(s.screen({5, 0, 5}), PointerDevice::Mouse); // the small box's front face
    ASSERT_TRUE(s.controller.runTool("align").ok());
    s.tap(s.screen({50, 10, 20}), PointerDevice::Mouse); // the big box's top
    const auto* align = dynamic_cast<const AlignOperation*>(s.controller.operation());
    ASSERT_NE(align, nullptr);
    ASSERT_TRUE(align->hasTarget());
    ASSERT_TRUE(align->hasPreview());
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    // Face to face: the small box stands on the top, centered on it.
    const ScreenRect landed = s.boxOnScreen({45, 5, 20}, {55, 15, 30});
    EXPECT_TRUE(keep->contains(landed, 1.0)) << "the aligned body " << text(landed) << " in " << text(*keep);
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

// The chip's first placements come while it is still being laid out (its
// actions appear, it grows): the spot is chosen afresh for each size until
// something moves, and forgotten when the operation ends.
TEST(KeepClear, FirstPlacementWaitsForTheChipsSize)
{
    Scene s({1180, 820});
    s.tap(s.screen({-10, -10, 10}));
    ASSERT_NE(s.controller.operation(), nullptr);
    const Vec2 tip = *s.controller.valueLabelPosition();
    const auto keep = s.controller.keepClearRect();
    ASSERT_TRUE(keep.has_value());
    Layout layout = ipad();
    // A panel right of the arrow: room between them for a small chip only.
    layout.avoid.push_back(rect(std::max(tip.x, keep->right) + 160, 16, 20, 788));
    auto in = input(layout, tip, keep);
    in.size = {100, 40};
    const ChipPlacement small = s.controller.placeValueChip(in);
    EXPECT_EQ(small.spot, ChipSpot::Right);
    in.size = layout.chip; // laid out: its real size
    const ChipPlacement laidOut = s.controller.placeValueChip(in);
    EXPECT_NE(laidOut.spot, ChipSpot::Right);
    EXPECT_EQ(laidOut.spot, placeValueChip(in).spot) << "as if chosen afresh";
    EXPECT_TRUE(laidOut.clear);
    // Once the arrow moves, the spot is kept while it stays clear.
    in.tip = tip + Vec2{1, 0};
    in.size = {100, 40};
    EXPECT_EQ(s.controller.placeValueChip(in).spot, laidOut.spot) << "kept: the arrow moved";
    // The operation ends: the next one on the same edge chooses afresh.
    EXPECT_TRUE(s.controller.keyPress(Key::Escape));
    ASSERT_EQ(s.controller.operation(), nullptr);
    s.tap(s.screen({-10, -10, 10}));
    ASSERT_NE(s.controller.operation(), nullptr);
    EXPECT_EQ(s.controller.placeValueChip(in).spot, ChipSpot::Right) << "fresh for a new operation";
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

// On a phone held sideways, a rectangle drawn with a finger beside the
// Dynamic Island: its width and height stay whole inside the safe area (the
// controller hands the window's safe insets to the sketch).
TEST(FingerLabels, RectangleBesideTheDynamicIsland)
{
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    controller.setViewportSize({874, 402});
    controller.setTouchLayout(true);
    const SafeInsets insets{0, 62, 21, 62};
    controller.setSafeInsets(insets);
    controller.fitAll(false);
    ASSERT_TRUE(controller.startSketch().ok());
    controller.skipAnimation();
    controller.setSketchTool(SketchTool::Rectangle);
    // Screen <-> sketch plane (a straight view of the plane: linear).
    auto screen = [&](Vec2 local) { return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local)); };
    const Vec2 o = screen({0, 0}), ex = screen({1, 0}) - o, ey = screen({0, 1}) - o;
    auto local = [&](Vec2 s) {
        const Vec2 d = s - o;
        const double det = ex.x * ey.y - ex.y * ey.x;
        return Vec2{(d.x * ey.y - d.y * ey.x) / det, (ex.x * d.y - ex.y * d.x) / det};
    };
    const Vec2 finger{75, 300};
    PointerEvent e;
    e.device = PointerDevice::Touch;
    e.position = screen(local({195, 220}));
    controller.pointerPress(e);
    const Vec2 start = e.position;
    for (int i = 1; i <= 8; ++i) {
        e.position = start + (finger - start) * (i / 8.0);
        controller.pointerMove(e);
    }
    const ScreenRect bounds = insets.inside({874, 402});
    int inputs = 0;
    for (const auto& label : controller.sketchSession()->labels(controller.camera())) {
        if (label.kind != SketchLabel::Kind::Input)
            continue;
        ++inputs;
        const ScreenRect box = ScreenRect::at(label.screen - SketchSession::kLiveLabelSize * 0.5, SketchSession::kLiveLabelSize);
        EXPECT_TRUE(bounds.contains(box)) << label.key << " at " << text(box);
        EXPECT_FALSE(box.intersects(fingerShadow(finger))) << label.key << " at " << text(box);
    }
    EXPECT_EQ(inputs, 2) << "the width and the height";
    controller.pointerRelease(e);
}
