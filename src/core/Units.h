// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
// ("25", "25mm", "1 in", "2.5cm", "0.5 m", "2 ft", "1'", "3\"") and simple
// arithmetic with parentheses ("20 + 5", "1in - 2mm", "(10+2)*3"); the
// times, division and minus signs (U+00D7, U+00F7, U+2212: the keypad's
// labels, handwriting, a pasted formula) count as * / -, and a leading '='
// is allowed. Feet and inches written together are added: "1' 2\"",
// "5 ft 6 in", "1' 2" (a bare number after feet is inches). A number
// without a unit is interpreted in `defaultUnit` (the document's display
// unit); a typed unit beats it. Multiplication/division take plain
// factors: "2 * 10mm" is valid, "10mm * 10mm" is rejected. An angle unit
// ("45 deg") is refused with a message saying this is a length.
LengthParseResult parseLength(std::string_view text, LengthUnit defaultUnit);

// Parses an angle. Degrees by default; accepts "deg", "degrees",
// "\xC2\xB0" and "rad" suffixes and the same arithmetic as lengths. A length
// unit ("10 mm") is refused with a message saying this is an angle.
// Returns radians.
LengthParseResult parseAngle(std::string_view text);
std::string formatAngle(double radians, int decimals = 1);

// Formats a length for display, e.g. "25.00 mm". Trailing zeros are kept to
// `decimals` so values read as exact.
std::string formatLength(double millimeters, LengthUnit unit, int decimals = 2);

} // namespace os
