#include "core/Units.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>

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

// Recursive-descent parser. Each value carries a flag saying whether it is a
// length (has a unit, or is a bare number interpreted as a length) or a pure
// scalar factor. Grammar:
//   expr   := term (('+'|'-') term)*
//   term   := factor (('*'|'/') factor)*
//   factor := ('+'|'-') factor | '(' expr ')' unit? | number unit?
struct Value {
    double v = 0.0;      // in millimeters when isLength, otherwise a plain number
    bool isLength = false;
};

class Parser {
public:
    Parser(std::string_view text, LengthUnit defaultUnit) : text_(text), defaultUnit_(defaultUnit) {}

    LengthParseResult run()
    {
        LengthParseResult result;
        skipSpace();
        if (pos_ >= text_.size()) {
            result.error = "Enter a value";
            return result;
        }
        Value value = expr();
        skipSpace();
        if (error_.empty() && pos_ != text_.size())
            error_ = "Unexpected '" + std::string(1, text_[pos_]) + "'";
        if (!error_.empty()) {
            result.error = error_;
            return result;
        }
        if (!std::isfinite(value.v)) {
            result.error = "Value is not a finite number";
            return result;
        }
        // A pure scalar (e.g. "2*3") is interpreted in the default unit.
        result.millimeters = value.isLength ? value.v : toMillimeters(value.v, defaultUnit_);
        return result;
    }

private:
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
        if (!a.isLength && !b.isLength) {
            out.v = subtract ? a.v - b.v : a.v + b.v;
            return out;
        }
        const double av = a.isLength ? a.v : toMillimeters(a.v, defaultUnit_);
        const double bv = b.isLength ? b.v : toMillimeters(b.v, defaultUnit_);
        out.v = subtract ? av - bv : av + bv;
        out.isLength = true;
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
        Value left = factor();
        while (error_.empty()) {
            const bool mul = accept('*');
            const bool div = !mul && accept('/');
            if (!mul && !div)
                break;
            Value right = factor();
            if (left.isLength && right.isLength) {
                error_ = "Cannot multiply or divide two lengths";
                break;
            }
            if (div && right.isLength) {
                error_ = "Cannot divide by a length";
                break;
            }
            if (div && right.v == 0.0) {
                error_ = "Division by zero";
                break;
            }
            Value out;
            out.v = mul ? left.v * right.v : left.v / right.v;
            out.isLength = left.isLength || right.isLength;
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
            return applyUnit(inner);
        }
        return number();
    }

    Value number()
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
            error_ = pos_ < text_.size() ? "Unexpected '" + std::string(1, text_[pos_]) + "'" : "Expected a number";
            return {};
        }
        double value = 0.0;
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (ec != std::errc() || ptr != digits.data() + digits.size()) {
            error_ = "Invalid number '" + digits + "'";
            return {};
        }
        Value v;
        v.v = value;
        return applyUnit(v);
    }

    Value applyUnit(Value v)
    {
        skipSpace();
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '"') {
            ++pos_;
        } else {
            while (pos_ < text_.size() && std::isalpha(static_cast<unsigned char>(text_[pos_])))
                ++pos_;
        }
        if (pos_ == start)
            return v;
        const auto symbol = text_.substr(start, pos_ - start);
        const auto unit = unitFromSymbol(symbol);
        if (!unit) {
            error_ = "Unknown unit '" + std::string(symbol) + "'";
            return {};
        }
        if (v.isLength) {
            error_ = "Value already has a unit";
            return {};
        }
        Value out;
        out.v = toMillimeters(v.v, *unit);
        out.isLength = true;
        return out;
    }

    std::string_view text_;
    LengthUnit defaultUnit_;
    std::size_t pos_ = 0;
    std::string error_;
};

} // namespace

LengthParseResult parseLength(std::string_view text, LengthUnit defaultUnit)
{
    return Parser(text, defaultUnit).run();
}

LengthParseResult parseAngle(std::string_view text)
{
    // Reuse the length expression parser: rewrite angle units into plain
    // arithmetic on degrees and parse as a unitless (default-unit) value.
    std::string s(text);
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
            s.replace(pos, from.size(), to);
    };
    replaceAll("\xC2\xB0", "");
    replaceAll("deg", "");
    replaceAll("rad", "*57.29577951308232");
    LengthParseResult result = parseLength(s, LengthUnit::Millimeter);
    if (result.millimeters)
        result.millimeters = *result.millimeters * 3.14159265358979323846 / 180.0;
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
