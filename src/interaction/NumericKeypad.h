// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Units.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace os::interact {

// The app's own numeric keypad, shown on a touch screen instead of the
// system keyboard for values (the value box, sketch dimensions and live
// values, the Model panel's numbers), as Shapr3D does: the owner could not
// type a size on an iPad without opening the keyboard and switching it to
// numbers (2026-09-27). Qt-free: the UI (NumericKeypad.qml) shows the keys
// laid out here and passes each tap through pressKeypadKey, which edits the
// text; the value field shows the text and takes it as if it were typed.

// What the value is: which keys the keypad has.
enum class KeypadMode {
    Length, // digits, arithmetic, mm / cm / in
    Angle,  // digits, arithmetic, degrees
    Count,  // whole numbers only (a pattern's count, a polygon's sides)
};
std::optional<KeypadMode> keypadModeFromString(std::string_view name); // "length", "angle", "count"

struct KeypadKey {
    std::string id;    // what pressKeypadKey takes: "0".."9", ".", "+", "-", "*", "/", "(", ")",
                       // "mm", "cm", "in", "deg", "back", "clear", "next", "done"
    std::string label; // UTF-8: "7", "\xC3\x97" (times), "\xE2\x8C\xAB" (backspace), ...
    int span = 1;      // columns it takes
    bool accent = false;
};
// The keys, row by row, kKeypadColumns wide. `hasNext`: the value has a
// next one (a rectangle's height after its width, a hole's depth after its
// diameter): a Next key.
constexpr int kKeypadColumns = 5;
std::vector<std::vector<KeypadKey>> keypadLayout(KeypadMode mode, bool hasNext);

struct KeypadState {
    std::string text;
    // The text shown is the value as it was (selected in the field): the
    // next digit replaces it, as typing over a selection does.
    bool replacing = true;
};

enum class KeypadAction {
    None,   // the key does nothing here (a unit on a count, a second decimal point)
    Edited, // the text changed: the field takes it as typed
    Next,   // on to the next value (the field takes its text at once)
    Done,   // the check mark: apply
};

struct KeypadResult {
    KeypadState state;
    KeypadAction action = KeypadAction::None;
};

// A key tapped. Digits and operators are added at the end; a unit replaces
// the unit the text ends with (25 mm, then in: 25 in); backspace takes the
// last character, or the whole unit it ends with (never leaving "25m", which
// would be meters); clear empties the text.
KeypadResult pressKeypadKey(const KeypadState& state, std::string_view keyId, KeypadMode mode);
// Characters typed on a hardware keyboard while the keypad edits a value:
// added as typed (units spelled out, "1in"), over the text while it is
// replaced. Control characters are ignored.
KeypadResult typeIntoKeypad(const KeypadState& state, std::string_view characters);

// The text as a value: millimeters for a length (a number alone in
// `unit`), radians for an angle, the whole number for a count. The fields
// themselves parse what they are given; this is what the keypad's text means.
LengthParseResult keypadValue(std::string_view text, KeypadMode mode, LengthUnit unit);

} // namespace os::interact
