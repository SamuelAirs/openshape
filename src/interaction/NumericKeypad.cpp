// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/NumericKeypad.h"

#include <algorithm>
#include <cstdlib>

namespace os::interact {
namespace {

constexpr const char* kDegree = "\xC2\xB0";

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Where the unit the text ends with starts ("25 mm" -> 3, "45.0°" -> 4), or
// npos when it ends with none. A unit follows a number or a parenthesis
// (spaces between them allowed).
std::size_t trailingUnitStart(const std::string& text)
{
    std::size_t start = text.size();
    if (text.size() >= 2 && text.compare(text.size() - 2, 2, kDegree) == 0)
        start = text.size() - 2;
    else if (!text.empty() && text.back() == '"')
        start = text.size() - 1;
    else
        while (start > 0 && isLetter(text[start - 1]))
            --start;
    if (start == text.size())
        return std::string::npos;
    std::size_t before = start;
    while (before > 0 && text[before - 1] == ' ')
        --before;
    if (before == 0 || !(isDigit(text[before - 1]) || text[before - 1] == '.' || text[before - 1] == ')'))
        return std::string::npos;
    return start;
}

// The last character (a whole UTF-8 sequence).
void popCharacter(std::string& text)
{
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80)
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

void trimTrailingSpaces(std::string& text)
{
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
}

bool keyAllowed(std::string_view id, KeypadMode mode)
{
    const bool digit = id.size() == 1 && isDigit(id[0]);
    if (digit || id == "back" || id == "clear" || id == "next" || id == "done")
        return true;
    if (mode == KeypadMode::Count)
        return false;
    if (id == "." || id == "+" || id == "-" || id == "*" || id == "/" || id == "(" || id == ")")
        return true;
    if (id == "deg")
        return mode == KeypadMode::Angle;
    if (id == "mm" || id == "cm" || id == "in")
        return mode == KeypadMode::Length;
    return false;
}

KeypadKey k(std::string id, std::string label, int span = 1, bool accent = false)
{
    return KeypadKey{std::move(id), std::move(label), span, accent};
}

KeypadResult edited(std::string text) { return {{std::move(text), false}, KeypadAction::Edited}; }

} // namespace

std::optional<KeypadMode> keypadModeFromString(std::string_view name)
{
    if (name == "length")
        return KeypadMode::Length;
    if (name == "angle")
        return KeypadMode::Angle;
    if (name == "count")
        return KeypadMode::Count;
    return std::nullopt;
}

std::vector<std::vector<KeypadKey>> keypadLayout(KeypadMode mode, bool hasNext)
{
    const KeypadKey back{"back", "\xE2\x8C\xAB", 1, false};
    const KeypadKey clear{"clear", "C", 1, false};
    auto finish = [hasNext](std::vector<KeypadKey> row) {
        // The rest of the last row: Next (when there is a next value) and the check mark.
        int used = 0;
        for (const KeypadKey& key : row)
            used += key.span;
        const int left = kKeypadColumns - used;
        if (hasNext) {
            row.push_back({"next", "Next", left / 2, false});
            row.push_back({"done", "\xE2\x9C\x93", left - left / 2, true});
        } else {
            row.push_back({"done", "\xE2\x9C\x93", left, true});
        }
        return row;
    };
    if (mode == KeypadMode::Count) {
        return {
            {k("7", "7"), k("8", "8"), k("9", "9"), k(back.id, back.label, 2)},
            {k("4", "4"), k("5", "5"), k("6", "6"), k(clear.id, clear.label, 2)},
            {k("1", "1"), k("2", "2"), k("3", "3"), k("0", "0", 2)},
            finish({}),
        };
    }
    std::vector<std::vector<KeypadKey>> rows{
        {k("7", "7"), k("8", "8"), k("9", "9"), k("(", "("), k(")", ")")},
        {k("4", "4"), k("5", "5"), k("6", "6"), k("*", "\xC3\x97"), k("/", "\xC3\xB7")},
        {k("1", "1"), k("2", "2"), k("3", "3"), k("+", "+"), k("-", "\xE2\x88\x92")},
        {k(".", "."), k("0", "0"), clear, k(back.id, back.label, 2)},
    };
    if (mode == KeypadMode::Angle)
        rows.push_back(finish({k("deg", kDegree)}));
    else
        rows.push_back(finish({k("mm", "mm"), k("cm", "cm"), k("in", "in")}));
    return rows;
}

KeypadResult pressKeypadKey(const KeypadState& state, std::string_view id, KeypadMode mode)
{
    if (!keyAllowed(id, mode))
        return {state, KeypadAction::None};
    if (id == "next")
        return {state, KeypadAction::Next};
    if (id == "done")
        return {state, KeypadAction::Done};
    if (id == "clear")
        return edited({});
    if (id == "back") {
        if (state.replacing)
            return edited({}); // as backspace over a selection
        std::string text = state.text;
        if (const std::size_t unit = trailingUnitStart(text); unit != std::string::npos)
            text.erase(unit);
        else
            popCharacter(text);
        trimTrailingSpaces(text);
        return edited(std::move(text));
    }
    if (id == "mm" || id == "cm" || id == "in" || id == "deg") {
        // Keeps the number shown (20.00 mm, then in: 20.00 in).
        std::string text = state.text;
        if (const std::size_t unit = trailingUnitStart(text); unit != std::string::npos)
            text.erase(unit);
        if (text.empty())
            return {state, KeypadAction::None}; // a unit needs a number
        text += id == "deg" ? std::string(kDegree) : std::string(id);
        return edited(std::move(text));
    }
    std::string text = state.replacing ? std::string() : state.text;
    if (id == ".") {
        // One decimal point per number.
        std::size_t start = text.size();
        while (start > 0 && (isDigit(text[start - 1]) || text[start - 1] == '.'))
            --start;
        const std::string_view number = std::string_view(text).substr(start);
        if (number.find('.') != std::string_view::npos)
            return {state, KeypadAction::None};
        text += number.empty() ? "0." : ".";
        return edited(std::move(text));
    }
    text += id; // a digit, an operator or a parenthesis: as typed
    return edited(std::move(text));
}

KeypadResult typeIntoKeypad(const KeypadState& state, std::string_view characters)
{
    std::string typed;
    for (const char c : characters)
        if (static_cast<unsigned char>(c) >= 32 && c != 127)
            typed += c;
    if (typed.empty())
        return {state, KeypadAction::None};
    return edited((state.replacing ? std::string() : state.text) + typed);
}

LengthParseResult keypadValue(std::string_view text, KeypadMode mode, LengthUnit unit)
{
    switch (mode) {
    case KeypadMode::Length:
        return parseLength(text, unit);
    case KeypadMode::Angle:
        return parseAngle(text);
    case KeypadMode::Count: {
        const std::string s(text);
        char* rest = nullptr;
        const long n = std::strtol(s.c_str(), &rest, 10);
        if (s.empty() || rest == s.c_str() || *rest != '\0' || n < 1)
            return {std::nullopt, "Type a whole number."};
        return {double(n), {}};
    }
    }
    return {std::nullopt, "Type a number."};
}

KeypadPlacement placeKeypad(const KeypadPlacementInput& in)
{
    const double w = in.size.x;
    const double h = in.size.y;
    const ScreenRect& area = in.area;
    const auto clampX = [&](double x) { return std::max(area.left, std::min(x, area.right - w)); };
    const auto clampY = [&](double y) { return std::max(area.top, std::min(y, area.bottom - h)); };
    const ScreenRect& t = in.target;
    const KeypadPlacement docked{{area.left + std::max(0.0, (area.width() - w) / 2), area.bottom - h}, true, false};
    if (in.compact && area.width() > area.height()) {
        // A phone held sideways: a bottom row as tall as the keypad would
        // leave no room for the model; it goes to a bottom corner, on the
        // side away from the value box (the Model panel is on the right).
        const Vec2 leftCorner{area.left, area.bottom - h}, rightCorner{area.right - w, area.bottom - h};
        const bool leftFirst = t.center().x > area.center().x;
        for (const Vec2 corner : {leftFirst ? leftCorner : rightCorner, leftFirst ? rightCorner : leftCorner})
            if (!ScreenRect::at(corner, in.size).intersects(t.inflated(kKeypadGap - 1)))
                return {corner, true, true};
        return {leftFirst ? leftCorner : rightCorner, true, false};
    }
    if (in.compact)
        return docked;

    // Each side of the value box, the keypad centered on it, then flush
    // with one end and with the other (past a control near one end).
    std::vector<Vec2> candidates;
    for (const double y : {t.bottom + kKeypadGap, t.top - kKeypadGap - h}) // below, above
        for (const double x : {t.center().x - w / 2, t.left, t.right - w})
            candidates.push_back({clampX(x), y});
    for (const double x : {t.right + kKeypadGap, t.left - kKeypadGap - w}) // right, left
        for (const double y : {t.top, t.center().y - h / 2, t.bottom - h})
            candidates.push_back({x, clampY(y)});
    // The corners of the area.
    for (const Vec2 corner : {Vec2{area.right - w, area.bottom - h}, Vec2{area.left, area.bottom - h}, Vec2{area.right - w, area.top},
                              Vec2{area.left, area.top}})
        candidates.push_back(corner);
    const ScreenRect targetZone = t.inflated(kKeypadGap - 1);
    const std::optional<ScreenRect> keepZone =
        in.keepClear ? std::optional<ScreenRect>(in.keepClear->inflated(kKeypadKeepClearMargin)) : std::nullopt;
    const auto fits = [&](const ScreenRect& r) { return area.contains(r); };
    const auto offTarget = [&](const ScreenRect& r) { return !r.intersects(targetZone) && !(keepZone && r.intersects(*keepZone)); };
    const auto offControls = [&](const ScreenRect& r) {
        for (const ScreenRect& control : in.avoid)
            if (control.width() > 0 && control.height() > 0 && r.intersects(control))
                return false;
        return true;
    };
    for (const Vec2& p : candidates) {
        const ScreenRect r = ScreenRect::at(p, in.size);
        if (fits(r) && offTarget(r) && offControls(r))
            return {p, false, true};
    }
    for (const Vec2& p : candidates) {
        const ScreenRect r = ScreenRect::at(p, in.size);
        if (fits(r) && offTarget(r))
            return {p, false, false};
    }
    return docked;
}

} // namespace os::interact
