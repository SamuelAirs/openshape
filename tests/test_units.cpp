#include "core/Units.h"

#include <gtest/gtest.h>

using namespace os;

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
