#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace os {

// Canonical internal length unit is the millimeter. Every length stored in a
// document, passed to the kernel, or held by a command is in millimeters.
// Other units exist only at the input/display boundary.
enum class LengthUnit { Millimeter, Centimeter, Meter, Inch };

double millimetersPer(LengthUnit unit);
std::string_view unitSymbol(LengthUnit unit);
std::optional<LengthUnit> unitFromSymbol(std::string_view symbol);

double toMillimeters(double value, LengthUnit unit);
double fromMillimeters(double millimeters, LengthUnit unit);

struct LengthParseResult {
    std::optional<double> millimeters;
    std::string error; // human readable, empty on success
};

// Parses user length input. Accepts a number with an optional unit suffix
// ("25", "25mm", "1 in", "2.5cm") and simple arithmetic with parentheses
// ("20 + 5", "1in - 2mm", "(10+2)*3"). A number without a unit is interpreted
// in `defaultUnit` (the document's display unit). Multiplication/division
// take plain factors: "2 * 10mm" is valid, "10mm * 10mm" is rejected.
LengthParseResult parseLength(std::string_view text, LengthUnit defaultUnit);

// Parses an angle. Degrees by default; accepts "deg", "\xC2\xB0" and "rad"
// suffixes and the same arithmetic as lengths. Returns radians.
LengthParseResult parseAngle(std::string_view text);
std::string formatAngle(double radians, int decimals = 1);

// Formats a length for display, e.g. "25.00 mm". Trailing zeros are kept to
// `decimals` so values read as exact.
std::string formatLength(double millimeters, LengthUnit unit, int decimals = 2);

} // namespace os
