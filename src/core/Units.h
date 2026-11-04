// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <functional>
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
//
// With `names`, an identifier (a letter or '_', then letters, digits and
// '_') is a named value: a project's measurement ("shelf_depth - 2",
// "2 * wall", "(width + 0.4) / 2"). A length name counts in millimeters
// (a bare number beside it stays in `defaultUnit`); an angle name in a
// length is refused, as an angle unit is; a name `names` does not know
// gives "No measurement called 'x' in this project". The resolver decides
// how names match (the document's: ignoring case).
enum class ValueKind { Length, Angle };
struct NamedValue {
    ValueKind kind = ValueKind::Length;
    double value = 0.0; // millimeters for a length, degrees for an angle
};
using NameResolver = std::function<std::optional<NamedValue>(std::string_view name)>;
LengthParseResult parseLength(std::string_view text, LengthUnit defaultUnit, const NameResolver& names = {});

// Parses an angle. Degrees by default; accepts "deg", "degrees",
// "\xC2\xB0" and "rad" suffixes and the same arithmetic as lengths. A length
// unit ("10 mm") is refused with a message saying this is an angle.
// Returns radians. With `names`, an angle name counts in degrees ("corner /
// 2"); a length name is refused.
LengthParseResult parseAngle(std::string_view text, const NameResolver& names = {});
std::string formatAngle(double radians, int decimals = 1);

// Names for values typed by name (a project's measurements). A name is a
// letter or '_', then letters, digits and '_' (at most kMaxValueNameLength
// characters), and not a unit the parsers read ("mm", "in", "deg", "ft"...:
// "2 in" must stay two inches).
inline constexpr std::size_t kMaxValueNameLength = 64;
bool isValueName(std::string_view name);
bool isUnitWord(std::string_view word);
// Why `name` cannot name a value, or empty: "Names start with a letter...".
std::string valueNameError(std::string_view name);
// A display name as a value name: "Length 3" -> "length_3", "Shelf depth"
// -> "shelf_depth", "3 mm gap" -> "_3_mm_gap"; lower case, every run of other
// characters one '_'; a unit word gets "_value" ("mm" -> "mm_value");
// nothing left -> "measurement".
std::string valueNameFor(std::string_view displayName);
// Case-insensitive ASCII comparison (value names match ignoring case).
bool sameValueName(std::string_view a, std::string_view b);

// Formats a length for display, e.g. "25.00 mm". Trailing zeros are kept to
// `decimals` so values read as exact.
std::string formatLength(double millimeters, LengthUnit unit, int decimals = 2);

} // namespace os
