// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Units.h"

#include <gtest/gtest.h>

using namespace os;

static constexpr double kPiForTests = 3.14159265358979323846;

namespace {

double mm(std::string_view text, LengthUnit unit = LengthUnit::Millimeter)
{
    const auto r = parseLength(text, unit);
    EXPECT_TRUE(r.millimeters.has_value()) << "input '" << text << "' error: " << r.error;
    return r.millimeters.value_or(-12345.0);
}

bool rejects(std::string_view text)
{
    const auto r = parseLength(text, LengthUnit::Millimeter);
    return !r.millimeters.has_value() && !r.error.empty();
}

} // namespace

TEST(Units, ConversionFactors)
{
    EXPECT_DOUBLE_EQ(toMillimeters(1.0, LengthUnit::Inch), 25.4);
    EXPECT_DOUBLE_EQ(toMillimeters(1.0, LengthUnit::Centimeter), 10.0);
    EXPECT_DOUBLE_EQ(toMillimeters(1.0, LengthUnit::Meter), 1000.0);
    EXPECT_DOUBLE_EQ(fromMillimeters(25.4, LengthUnit::Inch), 1.0);
    EXPECT_DOUBLE_EQ(fromMillimeters(toMillimeters(3.7, LengthUnit::Inch), LengthUnit::Inch), 3.7);
}

TEST(Units, BareNumberUsesDefaultUnit)
{
    EXPECT_DOUBLE_EQ(mm("25.4"), 25.4);
    EXPECT_DOUBLE_EQ(mm("15"), 15.0);
    EXPECT_DOUBLE_EQ(mm("2", LengthUnit::Inch), 50.8);
    EXPECT_DOUBLE_EQ(mm("3", LengthUnit::Centimeter), 30.0);
}

TEST(Units, ExplicitUnitsOverrideDefault)
{
    EXPECT_DOUBLE_EQ(mm("1in"), 25.4);
    EXPECT_DOUBLE_EQ(mm("1 in"), 25.4);
    EXPECT_DOUBLE_EQ(mm("1\""), 25.4);
    EXPECT_DOUBLE_EQ(mm("2.5cm"), 25.0);
    EXPECT_DOUBLE_EQ(mm("0.1 m"), 100.0);
    EXPECT_DOUBLE_EQ(mm("10mm", LengthUnit::Inch), 10.0);
    EXPECT_DOUBLE_EQ(mm("10 MM"), 10.0);
}

TEST(Units, Arithmetic)
{
    EXPECT_DOUBLE_EQ(mm("20 + 5"), 25.0);
    EXPECT_DOUBLE_EQ(mm("1in - 0.4mm"), 25.0);
    EXPECT_DOUBLE_EQ(mm("(10+2)*3"), 36.0);
    EXPECT_DOUBLE_EQ(mm("2 * 10mm"), 20.0);
    EXPECT_DOUBLE_EQ(mm("30 / 4"), 7.5);
    EXPECT_DOUBLE_EQ(mm("-5"), -5.0);
    EXPECT_DOUBLE_EQ(mm("(20+5)mm"), 25.0);
    // Grouped plain numbers under an inch default must convert exactly once.
    EXPECT_DOUBLE_EQ(mm("(1+1)*2", LengthUnit::Inch), 4 * 25.4);
    EXPECT_DOUBLE_EQ(mm("1 + 1", LengthUnit::Inch), 50.8);
}

TEST(Units, CommaDecimal)
{
    EXPECT_DOUBLE_EQ(mm("2,5"), 2.5);
}

TEST(Units, NumberForms)
{
    EXPECT_DOUBLE_EQ(mm(".5"), 0.5);
    EXPECT_DOUBLE_EQ(mm("1."), 1.0);
    EXPECT_DOUBLE_EQ(mm("0.1"), 0.1);
    EXPECT_DOUBLE_EQ(mm("1234567.125"), 1234567.125);
}

TEST(Units, RejectsBadInput)
{
    EXPECT_TRUE(rejects(""));
    EXPECT_TRUE(rejects("abc"));
    EXPECT_TRUE(rejects("5 furlongs"));
    EXPECT_TRUE(rejects("10mm * 10mm"));
    EXPECT_TRUE(rejects("10 / 0"));
    EXPECT_TRUE(rejects("10 / 2mm"));
    EXPECT_TRUE(rejects("(3"));
    EXPECT_TRUE(rejects("3mm in"));
    EXPECT_TRUE(rejects("1.2.3"));
}

TEST(Units, Formatting)
{
    EXPECT_EQ(formatLength(25.0, LengthUnit::Millimeter), "25.00 mm");
    EXPECT_EQ(formatLength(25.4, LengthUnit::Inch, 3), "1.000 in");
    EXPECT_EQ(formatLength(-0.0001, LengthUnit::Millimeter), "0.00 mm");
}

TEST(Units, Angles)
{
    EXPECT_NEAR(*parseAngle("90").millimeters, kPiForTests / 2, 1e-12);
    EXPECT_NEAR(*parseAngle("180deg").millimeters, kPiForTests, 1e-12);
    EXPECT_NEAR(*parseAngle("45\xC2\xB0").millimeters, kPiForTests / 4, 1e-12);
    EXPECT_NEAR(*parseAngle("1rad").millimeters, 1.0, 1e-12);
    EXPECT_NEAR(*parseAngle("360/4").millimeters, kPiForTests / 2, 1e-12);
    EXPECT_FALSE(parseAngle("north").millimeters.has_value());
    EXPECT_EQ(formatAngle(kPiForTests), "180.0\xC2\xB0");
}

// ---- Cases from OpenShape3D's keypad tests (NumericKeypadTests.swift) ----------------
// Their test ideas, checked against our parser; where we differ on purpose
// the comment says why.

TEST(Units, TypedUnitBeatsTheDisplayUnitAndABareNumberTakesIt)
{
    // "20 cm is 200 mm, whatever the document is displaying."
    for (const LengthUnit display : {LengthUnit::Millimeter, LengthUnit::Centimeter, LengthUnit::Meter, LengthUnit::Inch}) {
        EXPECT_DOUBLE_EQ(mm("20 cm", display), 200.0);
        EXPECT_NEAR(mm("0.05 in", display), 1.27, 1e-9);
        EXPECT_NEAR(mm("0.0025 ft", display), 0.762, 1e-9);
        EXPECT_DOUBLE_EQ(mm("1.5 m", display), 1500.0);
    }
    // Without a suffix the display unit applies.
    EXPECT_DOUBLE_EQ(mm("20", LengthUnit::Centimeter), 200.0);
    EXPECT_DOUBLE_EQ(mm("20", LengthUnit::Meter), 20000.0);
    EXPECT_DOUBLE_EQ(mm("2", LengthUnit::Inch), 50.8);
}

TEST(Units, ATrailingUnitIsReadWhole)
{
    // "mm" is never a trailing "m" (20 mm would become 20 m).
    EXPECT_DOUBLE_EQ(mm("20 mm"), 20.0);
    EXPECT_DOUBLE_EQ(mm("20mm"), 20.0);
    EXPECT_DOUBLE_EQ(mm("20 m"), 20000.0);
    EXPECT_DOUBLE_EQ(mm("20 cm"), 200.0);
    EXPECT_NEAR(mm("0.05 in"), 1.27, 1e-9);
    EXPECT_NEAR(mm("0.0025 ft"), 0.762, 1e-9);
    EXPECT_NEAR(mm("2 feet"), 609.6, 1e-9);
    EXPECT_NEAR(mm("1 foot"), 304.8, 1e-9);
    EXPECT_DOUBLE_EQ(mm("25.4/2"), 12.7);
    // Words that end in a unit's letters are not that unit.
    EXPECT_TRUE(rejects("20 loft"));
    EXPECT_TRUE(rejects("2 pin"));
    EXPECT_TRUE(rejects("20 mmm"));
}

TEST(Units, FeetAndInches)
{
    EXPECT_NEAR(mm("1'"), 304.8, 1e-9);
    EXPECT_NEAR(mm("1' 2\""), 355.6, 1e-9);
    EXPECT_NEAR(mm("1'2\""), 355.6, 1e-9);
    EXPECT_NEAR(mm("5 ft 6 in"), 1676.4, 1e-9);
    EXPECT_NEAR(mm("5ft 6.5in"), 1689.1, 1e-9);
    EXPECT_NEAR(mm("1' 2"), 355.6, 1e-9) << "a bare number after feet is inches";
    EXPECT_NEAR(mm("1' 2", LengthUnit::Centimeter), 355.6, 1e-9) << "... whatever the display unit";
    EXPECT_NEAR(mm("1\xE2\x80\xB2 2\xE2\x80\xB3"), 355.6, 1e-9) << "the prime and double prime signs";
    // Mixed units with an operator (OpenShape3D: 0.00125 ft + 0.025 in = 1.016 mm).
    EXPECT_NEAR(mm("0.00125 ft + 0.025 in"), 1.016, 1e-12);
    EXPECT_NEAR(mm("0.1 cm + 0.2 mm", LengthUnit::Inch), 1.2, 1e-12);
    EXPECT_DOUBLE_EQ(mm("1 cm - 2 mm"), 8.0);
    EXPECT_NEAR(mm("2 * 1' 6\""), 914.4, 1e-9) << "feet and inches are one factor";
    EXPECT_NEAR(mm("1' - 2\""), 254.0, 1e-9);
    // Fractions of an inch are arithmetic (TD-135): grouped, or as decimals.
    EXPECT_NEAR(mm("(1/2)\""), 12.7, 1e-9);
    EXPECT_NEAR(mm("1.5\""), 38.1, 1e-9);
    EXPECT_TRUE(rejects("1/2\"")) << "a number divided by a length";
    EXPECT_TRUE(rejects("1 1/2\""));
    // After feet only inches may follow without an operator.
    EXPECT_TRUE(rejects("1' 2 cm"));
    EXPECT_TRUE(rejects("1' 2'"));
    EXPECT_TRUE(rejects("3mm 2in"));
    EXPECT_TRUE(rejects("1\" 2\""));
    const auto after = parseLength("1' 2 cm", LengthUnit::Millimeter);
    EXPECT_EQ(after.error, "After feet, give the inches: 1' 2\"");
    // There is no feet display unit: the document's unit reads mm, cm, m or in only.
    EXPECT_FALSE(unitFromSymbol("ft").has_value());
    EXPECT_FALSE(unitFromSymbol("deg").has_value()) << "an angle, not a length";
}

TEST(Units, EveryOperatorTheKeypadShowsEvaluates)
{
    // The keypad types ASCII (* / -), but its keys show the times, division
    // and minus signs, and a handwriting recognizer or a paste may give those.
    EXPECT_DOUBLE_EQ(mm("6*7"), 42.0);
    EXPECT_DOUBLE_EQ(mm("84/2"), 42.0);
    EXPECT_DOUBLE_EQ(mm("50-8"), 42.0);
    EXPECT_DOUBLE_EQ(mm("40+2"), 42.0);
    EXPECT_DOUBLE_EQ(mm("(1+5)*7"), 42.0);
    EXPECT_DOUBLE_EQ(mm("-42"), -42.0);
    EXPECT_DOUBLE_EQ(mm("42 mm"), 42.0);
    EXPECT_DOUBLE_EQ(mm("6\xC3\x97" "7"), 42.0);      // times sign
    EXPECT_DOUBLE_EQ(mm("84\xC3\xB7" "2"), 42.0);     // division sign
    EXPECT_DOUBLE_EQ(mm("50\xE2\x88\x92" "8"), 42.0); // minus sign
    EXPECT_DOUBLE_EQ(mm("\xE2\x88\x92" "42"), -42.0); // minus sign in front
    EXPECT_DOUBLE_EQ(mm("6 \xE2\x8B\x85 7"), 42.0);   // dot operator
    EXPECT_DOUBLE_EQ(mm("95 mm \xC3\x97 2"), 190.0);
    EXPECT_DOUBLE_EQ(mm("= (1+2) cm"), 30.0) << "a leading '=' is allowed";
    // OpenShape3D reads "1+2 cm" as (1+2) cm: a trailing unit for the whole
    // expression. Ours binds a unit to its own number (1 mm + 2 cm), as
    // mixed units need: "1in - 2mm", "0.1 cm + 0.2 mm". Parentheses group.
    EXPECT_DOUBLE_EQ(mm("1+2 cm"), 21.0);
    EXPECT_NEAR(*parseAngle("90\xC3\xB7" "2").millimeters, kPiForTests / 4, 1e-12);
}

TEST(Units, InvalidInputIsRefusedWithAMessage)
{
    for (const char* text : {"", "   ", "2+", "12+", "1/0", "unknown_dimension", "(", "*3", "=", "1.2.3", "+"}) {
        const auto r = parseLength(text, LengthUnit::Millimeter);
        EXPECT_FALSE(r.millimeters.has_value()) << text;
        EXPECT_FALSE(r.error.empty()) << text;
    }
    EXPECT_EQ(parseLength("", LengthUnit::Millimeter).error, "Type a value.");
    EXPECT_EQ(parseLength("1/0", LengthUnit::Millimeter).error, "Division by zero");
    // A character outside ASCII is named whole in the message (valid UTF-8).
    EXPECT_EQ(parseLength("5\xE2\x82\xAC", LengthUnit::Millimeter).error, "Unexpected '\xE2\x82\xAC'");
    // ... and corrected, it parses.
    EXPECT_DOUBLE_EQ(mm("12+1"), 13.0);
}

TEST(Units, AnAngleUnitIsRefusedOnALengthAndALengthUnitOnAnAngle)
{
    for (const char* text : {"1 deg", "10+5 deg", "= 2 deg", "45\xC2\xB0", "1 cm + 2 deg", "1 rad"}) {
        const auto r = parseLength(text, LengthUnit::Millimeter);
        EXPECT_FALSE(r.millimeters.has_value()) << text;
        EXPECT_EQ(r.error, "This is a length: type it in mm, cm, m, in or ft, not in degrees.") << text;
    }
    for (const char* text : {"10 mm", "2 cm", "1 m", "(10+5) mm", "3 in", "1 ft", "2\""}) {
        const auto r = parseAngle(text);
        EXPECT_FALSE(r.millimeters.has_value()) << text << " was taken as " << r.millimeters.value_or(0);
        EXPECT_EQ(r.error.rfind("This is an angle: type it in degrees", 0), 0u) << text << ": " << r.error;
    }
    EXPECT_EQ(parseAngle("10 mm").error, "This is an angle: type it in degrees, not in mm.");
    // Degrees in their spellings, and arithmetic on them.
    EXPECT_NEAR(*parseAngle("90 deg").millimeters, kPiForTests / 2, 1e-12);
    EXPECT_NEAR(*parseAngle("90 degrees").millimeters, kPiForTests / 2, 1e-12);
    EXPECT_NEAR(*parseAngle("45\xC2\xB0 * 2").millimeters, kPiForTests / 2, 1e-12);
    EXPECT_NEAR(*parseAngle("(30+15)\xC2\xB0").millimeters, kPiForTests / 4, 1e-12);
    EXPECT_NEAR(*parseAngle("90 - 1 rad").millimeters, kPiForTests / 2 - 1.0, 1e-12);
    EXPECT_FALSE(parseAngle("45\xC2\xB0 * 2\xC2\xB0").millimeters.has_value()) << "two angles do not multiply";
}

// ---- Named values: a project's measurements typed by name (docs/MEASURE.md) ----------

namespace {

// shelf_depth 243 mm, wall 3 mm, width 40 mm, corner_angle 90 degrees, Length_3 12.5 mm.
std::optional<NamedValue> testNames(std::string_view name)
{
    struct Entry {
        const char* name;
        ValueKind kind;
        double value;
    };
    static constexpr Entry kEntries[] = {{"shelf_depth", ValueKind::Length, 243.0}, {"wall", ValueKind::Length, 3.0},
                                         {"width", ValueKind::Length, 40.0},        {"corner_angle", ValueKind::Angle, 90.0},
                                         {"Length_3", ValueKind::Length, 12.5},     {"_gap2", ValueKind::Length, 0.4}};
    for (const Entry& e : kEntries)
        if (sameValueName(e.name, name))
            return NamedValue{e.kind, e.value};
    return std::nullopt;
}

double named(std::string_view text, LengthUnit unit = LengthUnit::Millimeter)
{
    const auto r = parseLength(text, unit, testNames);
    EXPECT_TRUE(r.millimeters.has_value()) << "input '" << text << "' error: " << r.error;
    return r.millimeters.value_or(-12345.0);
}

double namedDegrees(std::string_view text)
{
    const auto r = parseAngle(text, testNames);
    EXPECT_TRUE(r.millimeters.has_value()) << "input '" << text << "' error: " << r.error;
    return r.millimeters.value_or(-12345.0) * 180.0 / kPiForTests;
}

std::string namedError(std::string_view text, bool angle = false)
{
    const auto r = angle ? parseAngle(text, testNames) : parseLength(text, LengthUnit::Millimeter, testNames);
    EXPECT_FALSE(r.millimeters.has_value()) << text;
    return r.error;
}

} // namespace

TEST(Units, ANameIsAValue)
{
    EXPECT_DOUBLE_EQ(named("shelf_depth"), 243.0);
    EXPECT_DOUBLE_EQ(named("shelf_depth - 2"), 241.0);
    EXPECT_DOUBLE_EQ(named("2 * wall"), 6.0);
    EXPECT_DOUBLE_EQ(named("wall*2"), 6.0);
    EXPECT_DOUBLE_EQ(named("(width + 0.4) / 2"), 20.2);
    EXPECT_DOUBLE_EQ(named("width - 2 * wall"), 34.0) << "times before minus";
    EXPECT_DOUBLE_EQ(named("(width - 2) * 2"), 76.0);
    EXPECT_DOUBLE_EQ(named("-wall"), -3.0);
    EXPECT_DOUBLE_EQ(named("= shelf_depth"), 243.0);
    EXPECT_DOUBLE_EQ(named("_gap2 + 1"), 1.4) << "a name may start with _ and hold digits";
    EXPECT_DOUBLE_EQ(named("shelf_depth \xE2\x88\x92 2 \xC3\x97 wall"), 237.0) << "the keypad's signs";
    EXPECT_DOUBLE_EQ(named("width/4"), 10.0);
}

TEST(Units, NamesMatchIgnoringCase)
{
    EXPECT_DOUBLE_EQ(named("SHELF_DEPTH"), 243.0);
    EXPECT_DOUBLE_EQ(named("Wall + 1"), 4.0);
    EXPECT_DOUBLE_EQ(named("length_3"), 12.5);
    EXPECT_TRUE(sameValueName("Shelf_Depth", "shelf_depth"));
    EXPECT_FALSE(sameValueName("shelf", "shelf_depth"));
}

TEST(Units, ANameCountsInMillimetersABareNumberBesideItInTheDisplayUnit)
{
    // wall is 3 mm; the 2 is in the document's unit, as a number alone is.
    EXPECT_NEAR(named("wall + 2", LengthUnit::Inch), 3.0 + 50.8, 1e-12);
    EXPECT_NEAR(named("wall + 2", LengthUnit::Centimeter), 23.0, 1e-12);
    EXPECT_NEAR(named("wall + 2 mm", LengthUnit::Inch), 5.0, 1e-12);
    EXPECT_NEAR(named("2 * wall", LengthUnit::Inch), 6.0, 1e-12) << "a factor has no unit";
    EXPECT_NEAR(named("shelf_depth", LengthUnit::Meter), 243.0, 1e-12) << "never converted";
    EXPECT_NEAR(named("wall + 1in"), 28.4, 1e-12);
    EXPECT_NEAR(named("wall + 1' 2\""), 358.6, 1e-9);
}

TEST(Units, AnAngleNameInAnAngle)
{
    EXPECT_NEAR(namedDegrees("corner_angle"), 90.0, 1e-12);
    EXPECT_NEAR(namedDegrees("corner_angle / 2"), 45.0, 1e-12);
    EXPECT_NEAR(namedDegrees("180 - corner_angle"), 90.0, 1e-12);
    EXPECT_NEAR(namedDegrees("corner_angle + 15deg"), 105.0, 1e-12);
    EXPECT_NEAR(namedDegrees("Corner_Angle*1.5"), 135.0, 1e-12);
}

TEST(Units, TheWrongKindOfNameIsRefused)
{
    EXPECT_EQ(namedError("corner_angle"), "This is a length, and corner_angle is an angle.");
    EXPECT_EQ(namedError("wall + corner_angle"), "This is a length, and corner_angle is an angle.");
    EXPECT_EQ(namedError("wall", true), "This is an angle, and wall is a length.");
    EXPECT_EQ(namedError("corner_angle / wall", true), "This is an angle, and wall is a length.");
    // Two lengths still do not multiply, and nothing divides by a length.
    EXPECT_EQ(namedError("wall * width"), "Cannot multiply or divide two lengths");
    EXPECT_EQ(namedError("10 / wall"), "Cannot divide by a length");
    EXPECT_EQ(namedError("wall * 2mm"), "Cannot multiply or divide two lengths");
}

TEST(Units, AnUnknownNameIsSaidPlainly)
{
    EXPECT_EQ(namedError("shelf"), "No measurement called 'shelf' in this project");
    EXPECT_EQ(namedError("shelf_depth - x"), "No measurement called 'x' in this project");
    EXPECT_EQ(namedError("depth", true), "No measurement called 'depth' in this project");
    // An operator forgotten between a number and a name.
    EXPECT_EQ(namedError("2 wall"), "Put an operator before wall: 2 * wall, 2 + wall");
    EXPECT_EQ(namedError("2wall"), "Put an operator before wall: 2 * wall, 2 + wall");
    EXPECT_EQ(namedError("2 shelf_depth"), "Put an operator before shelf_depth: 2 * shelf_depth, 2 + shelf_depth");
    // A unit after a name: the name has its own.
    EXPECT_EQ(namedError("wall mm"), "No unit after a name: wall is a length already.");
    EXPECT_EQ(namedError("corner_angle deg", true), "No unit after a name: corner_angle is an angle already.");
    EXPECT_EQ(namedError("corner_angle\xC2\xB0", true), "No unit after a name: corner_angle is an angle already.");
    // Units still read as units beside names.
    EXPECT_DOUBLE_EQ(named("2 mm + wall"), 5.0);
    EXPECT_NEAR(named("2in - wall"), 47.8, 1e-12);
    // Bad input around names.
    EXPECT_FALSE(parseLength("wall +", LengthUnit::Millimeter, testNames).millimeters.has_value());
    EXPECT_FALSE(parseLength("(wall", LengthUnit::Millimeter, testNames).millimeters.has_value());
    EXPECT_FALSE(parseLength("wall wall", LengthUnit::Millimeter, testNames).millimeters.has_value());
}

TEST(Units, WithoutNamesLettersAreRefusedAsBefore)
{
    EXPECT_EQ(parseLength("x", LengthUnit::Millimeter).error, "Unexpected 'x'");
    EXPECT_EQ(parseLength("shelf_depth - 2", LengthUnit::Millimeter).error, "Unexpected 's'");
    EXPECT_EQ(parseAngle("x").error, "Unexpected 'x'");
    // A resolver that knows nothing: every name is unknown.
    const NameResolver none = [](std::string_view) { return std::optional<NamedValue>(); };
    EXPECT_EQ(parseLength("wall", LengthUnit::Millimeter, none).error, "No measurement called 'wall' in this project");
    EXPECT_DOUBLE_EQ(*parseLength("2 in", LengthUnit::Millimeter, none).millimeters, 50.8);
    EXPECT_EQ(parseLength("5 furlongs", LengthUnit::Millimeter, none).error, "Unknown unit 'furlongs'");
}

TEST(Units, ValueNames)
{
    for (const char* good : {"shelf_depth", "w", "_x", "Length_3", "a1", "wall2"})
        EXPECT_TRUE(isValueName(good)) << good;
    for (const char* bad : {"", "3d", "shelf depth", "x-y", "mm", "in", "Deg", "ft", "rad", "inches", "m", "\xC3\xA9t\xC3\xA9"})
        EXPECT_FALSE(isValueName(bad)) << bad;
    EXPECT_FALSE(isValueName(std::string(kMaxValueNameLength + 1, 'a')));
    EXPECT_TRUE(isValueName(std::string(kMaxValueNameLength, 'a')));
    EXPECT_EQ(valueNameError("mm"), "'mm' is a unit: choose another name.");
    EXPECT_EQ(valueNameError("shelf depth"), "Names start with a letter and use letters, digits and _ (shelf_depth).");
    EXPECT_EQ(valueNameError(""), "Give it a name.");
    EXPECT_TRUE(isUnitWord("Inch"));
    EXPECT_FALSE(isUnitWord("wall"));
}

TEST(Units, DisplayNamesBecomeValueNames)
{
    EXPECT_EQ(valueNameFor("Length 3"), "length_3");
    EXPECT_EQ(valueNameFor("Shelf depth"), "shelf_depth");
    EXPECT_EQ(valueNameFor("  Shelf -- depth (left)  "), "shelf_depth_left");
    EXPECT_EQ(valueNameFor("3 mm gap"), "_3_mm_gap");
    EXPECT_EQ(valueNameFor("mm"), "mm_value");
    EXPECT_EQ(valueNameFor("Deg"), "deg_value");
    EXPECT_EQ(valueNameFor(""), "measurement");
    EXPECT_EQ(valueNameFor("+++"), "measurement");
    EXPECT_EQ(valueNameFor("H\xC3\xB6he"), "hohe") << "accents dropped";
    EXPECT_EQ(valueNameFor("Fu\xC3\x9F" "breite"), "fussbreite");
    EXPECT_EQ(valueNameFor("Tisch \xE2\x80\x93 Tiefe"), "tisch_tiefe") << "an en dash separates";
    EXPECT_EQ(valueNameFor("already_fine"), "already_fine");
    EXPECT_LE(valueNameFor(std::string(200, 'x')).size(), kMaxValueNameLength - 6);
    for (const char* display : {"Length 3", "Angle 12", "3 mm gap", "mm", "", "H\xC3\xB6he", "Caliper (fold)"})
        EXPECT_TRUE(isValueName(valueNameFor(display))) << display;
}
