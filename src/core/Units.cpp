// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Units.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

namespace os {

double millimetersPer(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Millimeter: return 1.0;
    case LengthUnit::Centimeter: return 10.0;
    case LengthUnit::Meter: return 1000.0;
    case LengthUnit::Inch: return 25.4;
    }
    return 1.0;
}

std::string_view unitSymbol(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Millimeter: return "mm";
    case LengthUnit::Centimeter: return "cm";
    case LengthUnit::Meter: return "m";
    case LengthUnit::Inch: return "in";
    }
    return "mm";
}

std::optional<LengthUnit> unitFromSymbol(std::string_view symbol)
{
    std::string s;
    for (char c : symbol)
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (s == "mm")
        return LengthUnit::Millimeter;
    if (s == "cm")
        return LengthUnit::Centimeter;
    if (s == "m")
        return LengthUnit::Meter;
    if (s == "in" || s == "inch" || s == "inches" || s == "\"")
        return LengthUnit::Inch;
    return std::nullopt;
}

double toMillimeters(double value, LengthUnit unit)
{
    return value * millimetersPer(unit);
}

double fromMillimeters(double millimeters, LengthUnit unit)
{
    return millimeters / millimetersPer(unit);
}

namespace {

constexpr double kPiUnits = 3.14159265358979323846;

// The units a value may be typed in. Feet are typed only: there is no feet
// display unit (LengthUnit), so unitFromSymbol, which reads the document's
// display unit, does not know them.
enum class Quantity { Length, Angle };
struct TypedUnit {
    Quantity quantity = Quantity::Length;
    double factor = 1.0; // millimeters (a length) or degrees (an angle) per unit
    bool foot = false;
    bool inch = false;
};

std::optional<TypedUnit> typedUnit(std::string_view symbol)
{
    std::string s;
    for (char c : symbol)
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (s == "ft" || s == "foot" || s == "feet" || s == "'" || s == "\xE2\x80\xB2") // U+2032 prime
        return TypedUnit{Quantity::Length, 304.8, true, false};
    if (s == "\xE2\x80\xB3") // U+2033 double prime
        return TypedUnit{Quantity::Length, 25.4, false, true};
    if (const auto unit = unitFromSymbol(s))
        return TypedUnit{Quantity::Length, millimetersPer(*unit), false, *unit == LengthUnit::Inch};
    if (s == "deg" || s == "degree" || s == "degrees" || s == "\xC2\xB0")
        return TypedUnit{Quantity::Angle, 1.0, false, false};
    if (s == "rad" || s == "radian" || s == "radians")
        return TypedUnit{Quantity::Angle, 180.0 / kPiUnits, false, false};
    return std::nullopt;
}

// The operators a keypad, a handwriting recognizer (the Apple Pencil) or a
// pasted formula may use besides + - * /: the times, division and minus
// signs, as ASCII.
constexpr std::pair<std::string_view, char> kOperatorSigns[] = {
    {"\xC3\x97", '*'},     // U+00D7 multiplication sign
    {"\xC3\xB7", '/'},     // U+00F7 division sign
    {"\xE2\x88\x92", '-'}, // U+2212 minus sign
    {"\xE2\x88\x97", '*'}, // U+2217 asterisk operator
    {"\xE2\x8B\x85", '*'}, // U+22C5 dot operator
    {"\xE2\x88\x95", '/'}, // U+2215 division slash
};

std::string asciiOperators(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        bool replaced = false;
        for (const auto& [sign, ascii] : kOperatorSigns) {
            if (text.compare(i, sign.size(), sign) == 0) {
                out.push_back(ascii);
                i += sign.size();
                replaced = true;
                break;
            }
        }
        if (!replaced)
            out.push_back(text[i++]);
    }
    return out;
}

// The character at `pos`: a whole UTF-8 sequence (for messages).
std::string characterAt(std::string_view text, std::size_t pos)
{
    std::size_t end = pos + 1;
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        ++end;
    return std::string(text.substr(pos, end - pos));
}

// Recursive-descent parser. Each value carries a flag saying whether it
// has a unit (a length in millimeters, or an angle in degrees) or is a pure
// scalar factor. Grammar:
//   expr   := term (('+'|'-') term)*
//   term   := factor (('*'|'/') factor)*
//   factor := ('+'|'-') factor | '(' expr ')' unit? | number unit? (number inch-unit?)
// The last part: feet and inches written together, 1' 2" (1 ft 2 in).
struct Value {
    double v = 0.0;       // in millimeters (degrees for an angle) when hasUnit, otherwise a plain number
    bool hasUnit = false;
};

class Parser {
public:
    Parser(std::string_view text, Quantity quantity, LengthUnit defaultUnit)
        : source_(asciiOperators(text)), text_(source_), quantity_(quantity), defaultUnit_(defaultUnit)
    {
    }

    // Millimeters for a length, degrees for an angle.
    LengthParseResult run()
    {
        LengthParseResult result;
        skipSpace();
        // A leading '=' (a spreadsheet habit, and OpenShape3D's formulas) is allowed.
        if (pos_ < text_.size() && text_[pos_] == '=') {
            ++pos_;
            skipSpace();
        }
        if (pos_ >= text_.size()) {
            result.error = "Type a value.";
            return result;
        }
        Value value = expr();
        skipSpace();
        if (error_.empty() && pos_ != text_.size())
            error_ = "Unexpected '" + characterAt(text_, pos_) + "'";
        if (!error_.empty()) {
            result.error = error_;
            return result;
        }
        if (!std::isfinite(value.v)) {
            result.error = "Value is not a finite number";
            return result;
        }
        // A pure scalar (e.g. "2*3") is interpreted in the default unit.
        result.millimeters = value.hasUnit ? value.v : inDefaultUnit(value.v);
        return result;
    }

private:
    double inDefaultUnit(double v) const { return quantity_ == Quantity::Length ? toMillimeters(v, defaultUnit_) : v; }

    void skipSpace()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])))
            ++pos_;
    }

    bool accept(char c)
    {
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    Value combineAdd(Value a, Value b, bool subtract)
    {
        // Plain + plain stays plain (so "(20+5)mm" works). Adding a plain
        // number to a length treats the number as a default-unit length.
        Value out;
        if (!a.hasUnit && !b.hasUnit) {
            out.v = subtract ? a.v - b.v : a.v + b.v;
            return out;
        }
        const double av = a.hasUnit ? a.v : inDefaultUnit(a.v);
        const double bv = b.hasUnit ? b.v : inDefaultUnit(b.v);
        out.v = subtract ? av - bv : av + bv;
        out.hasUnit = true;
        return out;
    }

    Value expr()
    {
        Value left = term();
        while (error_.empty()) {
            if (accept('+'))
                left = combineAdd(left, term(), false);
            else if (accept('-'))
                left = combineAdd(left, term(), true);
            else
                break;
        }
        return left;
    }

    Value term()
    {
        const char* what = quantity_ == Quantity::Length ? "length" : "angle";
        Value left = factor();
        while (error_.empty()) {
            const bool mul = accept('*');
            const bool div = !mul && accept('/');
            if (!mul && !div)
                break;
            Value right = factor();
            if (!error_.empty())
                break;
            if (left.hasUnit && right.hasUnit) {
                error_ = std::string("Cannot multiply or divide two ") + what + "s";
                break;
            }
            if (div && right.hasUnit) {
                error_ = std::string("Cannot divide by a ") + what;
                break;
            }
            if (div && right.v == 0.0) {
                error_ = "Division by zero";
                break;
            }
            Value out;
            out.v = mul ? left.v * right.v : left.v / right.v;
            out.hasUnit = left.hasUnit || right.hasUnit;
            left = out;
        }
        return left;
    }

    Value factor()
    {
        if (!error_.empty())
            return {};
        if (accept('-')) {
            Value v = factor();
            v.v = -v.v;
            return v;
        }
        if (accept('+'))
            return factor();
        if (accept('(')) {
            Value inner = expr();
            if (!accept(')')) {
                if (error_.empty())
                    error_ = "Missing ')'";
                return {};
            }
            return applyUnit(inner).value;
        }
        return number(false);
    }

    // `inches`: the inches after feet (1' 2"): a bare number is inches too.
    Value number(bool inches)
    {
        skipSpace();
        const std::size_t start = pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.'))
            ++pos_;
        // Accept a comma decimal separator when it is the only separator (e.g. "2,5").
        std::string digits(text_.substr(start, pos_ - start));
        if (pos_ < text_.size() && text_[pos_] == ',' && digits.find('.') == std::string::npos) {
            const std::size_t fracStart = ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                ++pos_;
            digits += "." + std::string(text_.substr(fracStart, pos_ - fracStart));
        }
        if (digits.empty() || digits == ".") {
            error_ = pos_ < text_.size() ? "Unexpected '" + characterAt(text_, pos_) + "'" : "Expected a number";
            return {};
        }
        // Locale-independent (Qt sets the C locale from the environment on
        // Unix). Not std::from_chars: Apple's libc++ lacks it for double.
        std::istringstream in(digits);
        in.imbue(std::locale::classic());
        double value = 0.0;
        in >> value;
        if (in.fail() || in.peek() != std::char_traits<char>::eof()) {
            error_ = "Invalid number '" + digits + "'";
            return {};
        }
        Value v;
        v.v = value;
        const Applied applied = applyUnit(v);
        if (!error_.empty())
            return {};
        if (inches) {
            if (applied.unit && !applied.unit->inch) {
                error_ = "After feet, give the inches: 1' 2\"";
                return {};
            }
            return applied.unit ? applied.value : Value{value * 25.4, true};
        }
        // Feet, then inches without an operator between them: 1' 2", 5 ft 6 in.
        if (applied.unit && applied.unit->foot) {
            skipSpace();
            if (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
                const Value rest = number(true);
                if (!error_.empty())
                    return {};
                return Value{applied.value.v + rest.v, true};
            }
        }
        return applied.value;
    }

    struct Applied {
        Value value;
        std::optional<TypedUnit> unit; // the unit read, if any
    };

    Applied applyUnit(Value v)
    {
        skipSpace();
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '"' || text_[pos_] == '\'')) {
            ++pos_;
        } else if (text_.compare(pos_, 2, "\xC2\xB0") == 0) { // the degree sign
            pos_ += 2;
        } else if (text_.compare(pos_, 3, "\xE2\x80\xB2") == 0 || text_.compare(pos_, 3, "\xE2\x80\xB3") == 0) { // primes
            pos_ += 3;
        } else {
            while (pos_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[pos_])))
                ++pos_;
        }
        if (pos_ == start)
            return {v, std::nullopt};
        const auto symbol = text_.substr(start, pos_ - start);
        const auto unit = typedUnit(symbol);
        if (!unit) {
            error_ = "Unknown unit '" + std::string(symbol) + "'";
            return {};
        }
        if (unit->quantity != quantity_) {
            // (OpenShape3D's rule: a length unit never resizes an angle, nor an angle unit a length.)
            error_ = quantity_ == Quantity::Length ? "This is a length: type it in mm, cm, m, in or ft, not in degrees."
                                                   : "This is an angle: type it in degrees, not in " + std::string(symbol) + ".";
            return {};
        }
        if (v.hasUnit) {
            error_ = "Value already has a unit";
            return {};
        }
        return {Value{v.v * unit->factor, true}, unit};
    }

    std::string source_;
    std::string_view text_;
    Quantity quantity_;
    LengthUnit defaultUnit_;
    std::size_t pos_ = 0;
    std::string error_;
};

} // namespace

LengthParseResult parseLength(std::string_view text, LengthUnit defaultUnit)
{
    return Parser(text, Quantity::Length, defaultUnit).run();
}

LengthParseResult parseAngle(std::string_view text)
{
    LengthParseResult result = Parser(text, Quantity::Angle, LengthUnit::Millimeter).run();
    if (result.millimeters)
        result.millimeters = *result.millimeters * kPiUnits / 180.0;
    return result;
}

std::string formatAngle(double radians, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f\xC2\xB0", decimals, radians * 180.0 / 3.14159265358979323846);
    return buffer;
}

std::string formatLength(double millimeters, LengthUnit unit, int decimals)
{
    double value = fromMillimeters(millimeters, unit);
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals))
        value = 0.0; // avoid "-0.00"
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f %s", decimals, value, std::string(unitSymbol(unit)).c_str());
    return buffer;
}

} // namespace os
