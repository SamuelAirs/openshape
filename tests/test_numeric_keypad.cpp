// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The numeric keypad shown on touch screens (interact::NumericKeypad): key
// sequences give the text a field takes, and the text the value.
#include "core/Math.h"
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
