// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The numeric keypad shown on touch screens (interact::NumericKeypad): key
// sequences give the text a field takes, and the text the value.
#include "commands/Command.h"
#include "core/Math.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "interaction/NumericKeypad.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <set>
#include <string>

using namespace os;
using namespace os::interact;

namespace {

// Taps `keys` on a keypad showing `start` (selected, as a field shows its value).
KeypadState tap(std::initializer_list<const char*> keys, KeypadMode mode = KeypadMode::Length, std::string start = "20.00 mm")
{
    KeypadState state{std::move(start), true};
    for (const char* key : keys)
        state = pressKeypadKey(state, key, mode).state;
    return state;
}

double millimeters(const std::string& text, LengthUnit unit = LengthUnit::Millimeter)
{
    const auto value = keypadValue(text, KeypadMode::Length, unit);
    EXPECT_TRUE(value.millimeters.has_value()) << text << ": " << value.error;
    return value.millimeters.value_or(-1);
}

} // namespace

TEST(NumericKeypad, DigitsReplaceTheValueShownThenAddUp)
{
    const KeypadState state = tap({"1", "0", "0"});
    EXPECT_EQ(state.text, "100");
    EXPECT_FALSE(state.replacing);
    EXPECT_DOUBLE_EQ(millimeters(state.text), 100.0);
    // In inches as the display unit, a number alone is inches.
    EXPECT_NEAR(millimeters(state.text, LengthUnit::Inch), 2540.0, 1e-9);
}

TEST(NumericKeypad, DecimalPointAndArithmetic)
{
    EXPECT_EQ(tap({".", "5"}).text, "0.5");
    EXPECT_EQ(tap({"2", ".", "5", "."}).text, "2.5") << "one decimal point per number";
    const KeypadState sum = tap({"(", "1", "0", "+", "2", ")", "*", "3"});
    EXPECT_EQ(sum.text, "(10+2)*3");
    EXPECT_DOUBLE_EQ(millimeters(sum.text), 36.0);
    const KeypadState quarter = tap({"1", "0", "0", "/", "4", "-", "1", ".", "5"});
    EXPECT_EQ(quarter.text, "100/4-1.5");
    EXPECT_DOUBLE_EQ(millimeters(quarter.text), 23.5);
    // A leading minus (or plus) on a measured value: 5 less (more) than now.
    EXPECT_EQ(tap({"-", "5"}).text, "-5");
    EXPECT_EQ(tap({"+", "5"}).text, "+5");
}

TEST(NumericKeypad, UnitsFollowTheNumberAndReplaceEachOther)
{
    EXPECT_EQ(tap({"2", "5", "mm"}).text, "25mm");
    EXPECT_EQ(tap({"2", "5", "mm", "in"}).text, "25in") << "a second unit replaces the first";
    EXPECT_NEAR(millimeters(tap({"2", "5", "mm", "in"}).text), 25.0 * 25.4, 1e-9);
    EXPECT_DOUBLE_EQ(millimeters(tap({"2", ".", "5", "cm"}).text), 25.0);
    EXPECT_NEAR(millimeters(tap({"1", "in", "-", "2", "mm"}).text), 23.4, 1e-9);
    // The value shown keeps its number: 20.00 mm, then in.
    EXPECT_EQ(tap({"in"}).text, "20.00 in");
    // A unit needs a number.
    const KeypadResult none = pressKeypadKey({"", false}, "mm", KeypadMode::Length);
    EXPECT_EQ(none.action, KeypadAction::None);
    EXPECT_EQ(none.state.text, "");
}

TEST(NumericKeypad, BackspaceTakesACharacterOrTheWholeUnit)
{
    EXPECT_EQ(tap({"back"}).text, "") << "backspace over the value shown empties it";
    EXPECT_EQ(tap({"1", "2", "3", "back"}).text, "12");
    // Never "25m" (meters) from "25mm".
    EXPECT_EQ(tap({"2", "5", "mm", "back"}).text, "25");
    EXPECT_EQ(tap({"4", "5", "deg", "back"}, KeypadMode::Angle, "30.0\xC2\xB0").text, "45");
    const KeypadState shown{"20.00 mm", false};
    EXPECT_EQ(pressKeypadKey(shown, "back", KeypadMode::Length).state.text, "20.00");
    EXPECT_EQ(pressKeypadKey({"", false}, "back", KeypadMode::Length).state.text, "");
}

TEST(NumericKeypad, ClearNextAndDone)
{
    const KeypadResult cleared = pressKeypadKey({"125", false}, "clear", KeypadMode::Length);
    EXPECT_EQ(cleared.action, KeypadAction::Edited);
    EXPECT_EQ(cleared.state.text, "");
    const KeypadResult next = pressKeypadKey({"30", false}, "next", KeypadMode::Length);
    EXPECT_EQ(next.action, KeypadAction::Next);
    EXPECT_EQ(next.state.text, "30") << "Next leaves the text for the field to take";
    const KeypadResult done = pressKeypadKey({"30", false}, "done", KeypadMode::Length);
    EXPECT_EQ(done.action, KeypadAction::Done);
    EXPECT_EQ(done.state.text, "30");
    EXPECT_EQ(pressKeypadKey({"30", false}, "7", KeypadMode::Length).action, KeypadAction::Edited);
    EXPECT_EQ(pressKeypadKey({"30", false}, "nonsense", KeypadMode::Length).action, KeypadAction::None);
}

TEST(NumericKeypad, AnglesTakeDegreesAndCountsWholeNumbers)
{
    const KeypadState angle = tap({"4", "5", "deg"}, KeypadMode::Angle, "0.0\xC2\xB0");
    EXPECT_EQ(angle.text, "45\xC2\xB0");
    const auto radians = keypadValue(angle.text, KeypadMode::Angle, LengthUnit::Millimeter);
    ASSERT_TRUE(radians.millimeters.has_value()) << radians.error;
    EXPECT_NEAR(*radians.millimeters, kPi / 4, 1e-12);
    EXPECT_EQ(pressKeypadKey({"45", false}, "mm", KeypadMode::Angle).action, KeypadAction::None) << "no lengths for an angle";
    EXPECT_EQ(pressKeypadKey({"45", false}, "deg", KeypadMode::Length).action, KeypadAction::None) << "no degrees for a length";

    const KeypadState count = tap({"1", ".", "2", "+"}, KeypadMode::Count, "6");
    EXPECT_EQ(count.text, "12") << "a count takes digits only";
    const auto sides = keypadValue(count.text, KeypadMode::Count, LengthUnit::Millimeter);
    ASSERT_TRUE(sides.millimeters.has_value());
    EXPECT_DOUBLE_EQ(*sides.millimeters, 12.0);
    EXPECT_FALSE(keypadValue("", KeypadMode::Count, LengthUnit::Millimeter).millimeters.has_value());
    EXPECT_FALSE(keypadValue("2.5", KeypadMode::Count, LengthUnit::Millimeter).millimeters.has_value());
}

TEST(NumericKeypad, HardwareKeysTypeAsTheyAre)
{
    // A hardware keyboard while the keypad edits: over the value shown, then after it.
    KeypadState state{"20.00 mm", true};
    state = typeIntoKeypad(state, "1").state;
    state = typeIntoKeypad(state, "in").state;
    EXPECT_EQ(state.text, "1in");
    EXPECT_FALSE(state.replacing);
    EXPECT_EQ(typeIntoKeypad(state, "\t").action, KeypadAction::None) << "control characters are not text";
}

TEST(NumericKeypad, LayoutHasEveryKeyOnceAndFullRows)
{
    for (const KeypadMode mode : {KeypadMode::Length, KeypadMode::Angle, KeypadMode::Count}) {
        for (const bool hasNext : {false, true}) {
            std::set<std::string> ids;
            for (const auto& row : keypadLayout(mode, hasNext)) {
                int columns = 0;
                for (const KeypadKey& key : row) {
                    EXPECT_TRUE(ids.insert(key.id).second) << "twice: " << key.id;
                    EXPECT_GE(key.span, 1);
                    EXPECT_FALSE(key.label.empty());
                    // Every key does something in its mode (a unit needs a number first).
                    const KeypadResult result = pressKeypadKey({"1", false}, key.id, mode);
                    EXPECT_NE(result.action, KeypadAction::None) << key.id;
                    columns += key.span;
                }
                EXPECT_EQ(columns, kKeypadColumns);
            }
            for (int digit = 0; digit <= 9; ++digit)
                EXPECT_TRUE(ids.count(std::to_string(digit))) << digit;
            for (const char* key : {"back", "clear", "done"})
                EXPECT_TRUE(ids.count(key)) << key;
            EXPECT_EQ(ids.count("next"), hasNext ? 1u : 0u);
            EXPECT_EQ(ids.count("mm") + ids.count("cm") + ids.count("in"), mode == KeypadMode::Length ? 3u : 0u);
            EXPECT_EQ(ids.count("deg"), mode == KeypadMode::Angle ? 1u : 0u);
            if (mode != KeypadMode::Count) {
                for (const char* key : {".", "+", "-", "*", "/", "(", ")"})
                    EXPECT_TRUE(ids.count(key)) << key;
            }
        }
    }
    EXPECT_EQ(keypadModeFromString("angle"), KeypadMode::Angle);
    EXPECT_FALSE(keypadModeFromString("text").has_value());
}

// ---- Where the keypad goes ----------------------------------------------------------

namespace {

KeypadPlacementInput ipad()
{
    KeypadPlacementInput in;
    in.area = {0, 24, 1180, 800};
    in.size = {320, 290};
    return in;
}

ScreenRect placed(const KeypadPlacementInput& in, const KeypadPlacement& p) { return ScreenRect::at(p.position, in.size); }

} // namespace

TEST(KeypadPlacement, PhoneDocksAlongTheBottom)
{
    KeypadPlacementInput in;
    in.area = {0, 62, 402, 840}; // 402x874 with the Dynamic Island and the home indicator
    in.size = {402, 300};
    in.target = {10, 130, 300, 180};
    in.compact = true;
    const KeypadPlacement p = placeKeypad(in);
    EXPECT_TRUE(p.docked);
    EXPECT_DOUBLE_EQ(p.position.x, 0);
    EXPECT_DOUBLE_EQ(p.position.y + in.size.y, 840) << "down to the home indicator's safe area";
}

TEST(KeypadPlacement, SidewaysPhoneUsesABottomCornerAwayFromTheValueBox)
{
    KeypadPlacementInput in;
    in.area = {66, 4, 808, 377}; // 874x402 with the Dynamic Island on the left
    in.size = {336, 290};
    in.target = {100, 10, 400, 66}; // a value box docked at the top, on the left
    in.compact = true;
    const KeypadPlacement p = placeKeypad(in);
    EXPECT_TRUE(p.docked);
    EXPECT_DOUBLE_EQ(p.position.x + in.size.x, 808) << "right: the value box leans left";
    EXPECT_DOUBLE_EQ(p.position.y + in.size.y, 377);
    EXPECT_LT(in.size.x, in.area.width() / 2) << "the model keeps the other half";
    in.target = {500, 70, 808, 300}; // the Model panel on the right
    EXPECT_DOUBLE_EQ(placeKeypad(in).position.x, 66) << "left of the Model panel";
    in.target = {70, 203, 470, 243}; // (both corners reach it)
    EXPECT_FALSE(placeKeypad(in).clear) << "the value box has to move (a dimension's field does)";
    in.target = {70, 203, 190, 243};
    const KeypadPlacement q = placeKeypad(in);
    EXPECT_TRUE(q.clear);
    EXPECT_DOUBLE_EQ(q.position.x + in.size.x, 808);
}

TEST(KeypadPlacement, BesideTheValueBoxClearOfTheSelectionAndTheControls)
{
    KeypadPlacementInput in = ipad();
    in.target = {600, 400, 800, 460};
    const KeypadPlacement below = placeKeypad(in);
    EXPECT_FALSE(below.docked);
    EXPECT_TRUE(below.clear);
    EXPECT_DOUBLE_EQ(below.position.y, 460 + kKeypadGap) << "right below the value box";
    EXPECT_NEAR(below.position.x + in.size.x / 2, 700, 1e-9) << "centered under it";

    // The selection below the value box: above it instead.
    in.keepClear = ScreenRect{560, 480, 790, 760};
    const KeypadPlacement above = placeKeypad(in);
    EXPECT_TRUE(above.clear);
    EXPECT_DOUBLE_EQ(above.position.y + in.size.y, 400 - kKeypadGap);
    EXPECT_FALSE(placed(in, above).intersects(in.keepClear->inflated(kKeypadKeepClearMargin)));

    // A control above too (the top bar reaching down): to the right.
    in.avoid = {{0, 0, 1180, 120}};
    const KeypadPlacement right = placeKeypad(in);
    EXPECT_TRUE(right.clear);
    EXPECT_DOUBLE_EQ(right.position.x, 800 + kKeypadGap);
    const ScreenRect r = placed(in, right);
    EXPECT_TRUE(in.area.contains(r));
    EXPECT_FALSE(r.intersects(in.target));
    EXPECT_FALSE(r.intersects(in.avoid[0]));
}

TEST(KeypadPlacement, SlidesAlongTheValueBoxPastAControl)
{
    // The acceptance run's iPad case: a wide value box (its actions below the
    // field) low in the window, the face and its arrow above it, the tool
    // column on the left and the axis marker low on the right. Right of the
    // value box, centered on it instead of level with its top, clears the
    // axis marker.
    KeypadPlacementInput in = ipad();
    in.area = {4, 4, 1176, 816};
    in.size = {340, 290};
    in.target = {360, 430, 820, 576};
    in.keepClear = ScreenRect{412, 218, 767, 410};
    in.avoid = {{16, 16, 361, 72}, {16, 88, 106, 732}, {670, 748, 1164, 804}, {1084, 660, 1164, 740}, {16, 748, 654, 804}, {874, 16, 1164, 136}};
    const KeypadPlacement p = placeKeypad(in);
    EXPECT_TRUE(p.clear);
    EXPECT_FALSE(p.docked);
    EXPECT_DOUBLE_EQ(p.position.x, 820 + kKeypadGap);
    EXPECT_DOUBLE_EQ(p.position.y + in.size.y / 2, (430.0 + 576.0) / 2);
    EXPECT_LE(p.position.y + in.size.y, 660) << "above the axis marker";
}

TEST(KeypadPlacement, NeverOverTheValueBoxOrOutsideTheWindow)
{
    // Value boxes all over the window: the keypad is inside the area and off
    // the value box every time; clear of the selection whenever it is not docked.
    for (double x = 0; x <= 1000; x += 125) {
        for (double y = 30; y <= 740; y += 90) {
            KeypadPlacementInput in = ipad();
            in.target = {x, y, x + 180, y + 56};
            in.keepClear = ScreenRect{x + 40, y + 70, x + 260, y + 260};
            const KeypadPlacement p = placeKeypad(in);
            const ScreenRect r = placed(in, p);
            EXPECT_TRUE(in.area.contains(r)) << x << "," << y;
            if (!p.docked) {
                EXPECT_FALSE(r.intersects(in.target)) << x << "," << y;
                EXPECT_FALSE(r.intersects(*in.keepClear)) << x << "," << y;
            }
        }
    }
}

// ---- A phone's keypad over the selection: the view moves ----------------------------

TEST(KeypadReveal, TheSelectionMovesIntoTheRoomTheKeypadLeaves)
{
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    controller.setViewportSize({402, 874});
    ASSERT_TRUE(controller.createBox(20).ok());
    controller.fitAll(false);
    // The box's front face (y = -10), tapped in its middle.
    const Vec2 p = controller.camera().project({0, -10, 10});
    PointerEvent e;
    e.position = p;
    e.button = PointerButton::Left;
    e.device = PointerDevice::Touch;
    controller.pointerPress(e);
    controller.pointerRelease(e);
    ASSERT_NE(controller.operation(), nullptr);
    const auto before = controller.keepClearRect();
    ASSERT_TRUE(before.has_value());

    // Room between a chip docked at the top and a keypad along the bottom.
    const ScreenRect room{0, 240, 402, 520};
    EXPECT_TRUE(controller.revealKeepClear(room));
    const auto after = controller.keepClearRect();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(room.contains(*after)) << after->left << "," << after->top << " " << after->right << "," << after->bottom;
    EXPECT_FALSE(controller.revealKeepClear(room)) << "already there";

    // A band smaller than the selection: the view zooms out too.
    const ScreenRect band{0, 300, 402, 380};
    EXPECT_TRUE(controller.revealKeepClear(band));
    const auto small = controller.keepClearRect();
    ASSERT_TRUE(small.has_value());
    EXPECT_TRUE(band.contains(*small)) << small->top << " " << small->bottom;
    EXPECT_EQ(document.bodies().size(), 1u) << "only the view moved";
}

// ---- A keypad beside a sketch's value keeps clear of the sketch -----------------------

TEST(KeypadReveal, SketchOnScreenHoldsItsCornersAndTheShapeBeingDrawn)
{
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    controller.setViewportSize({1200, 800});
    EXPECT_FALSE(controller.sketchScreenRect().has_value()) << "no sketch";
    ASSERT_TRUE(controller.startSketch().ok());
    controller.skipAnimation();
    const auto click = [&](Vec2 p) {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        controller.pointerPress(e);
        controller.pointerRelease(e);
    };
    const auto move = [&](Vec2 p) {
        PointerEvent e;
        e.position = p;
        controller.pointerMove(e);
    };
    click({500, 300});
    move({560, 380});
    const auto drawing = controller.sketchScreenRect();
    ASSERT_TRUE(drawing.has_value()) << "the shape being drawn";
    EXPECT_TRUE(drawing->contains({500, 300, 560, 380}, 3.0))
        << drawing->left << "," << drawing->top << " " << drawing->right << "," << drawing->bottom;
    click({700, 450});
    const auto drawn = controller.sketchScreenRect();
    ASSERT_TRUE(drawn.has_value());
    // (The corners snap to the grid: within a few pixels of the clicks.)
    EXPECT_NEAR(drawn->left, 500, 3.0);
    EXPECT_NEAR(drawn->top, 300, 3.0);
    EXPECT_NEAR(drawn->right, 700, 3.0);
    EXPECT_NEAR(drawn->bottom, 450, 3.0);
}
