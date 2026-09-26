// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/TouchWording.h"

#include <array>
#include <cctype>
#include <utility>

namespace os::interact {

namespace {

// UTF-8 pieces used below.
#define DOT "\xC2\xB7"      // · between the parts of a hint
#define MINUS "\xE2\x88\x92" // − as on the on-screen counter
#define CHECK "\xE2\x9C\x93" // ✓ the value chip's Apply
#define CROSS "\xE2\x9C\x95" // ✕ the value chip's Cancel
#define DEGREE "\xC2\xB0"

// Whole sentences and clauses with a hand-written touch version, most
// specific first. (Sources: SketchSession::hintText and its messages,
// Operation::prompt, InteractionController::runTool and messages, and the
// hints in Main.qml.)
constexpr std::array<std::pair<std::string_view, std::string_view>, 36> kPhrases{{
    // ---- Sketch hints
    {"Click the line to mirror across (a construction line works well) " DOT " Esc cancels",
     "Tap the line to mirror across (a construction line works well) " DOT " tap Mirror again to cancel"},
    {"type the total angle, Tab for the count " DOT " - / + change the count " DOT " Enter or Apply adds them",
     MINUS " / + change the count " DOT " Apply adds them"},
    {" (or type the spacing), Tab for the count " DOT " - / + change the count " DOT " Enter or Apply adds them",
     " " DOT " " MINUS " / + change the count " DOT " Apply adds them"},
    {"Move to the side to offset to and click " DOT " or type a distance and press Enter " DOT " Esc cancels",
     "Tap on the side to offset to " DOT " tap Offset again to cancel"},
    {"Move to set the width and click, or type a width and press Enter", "Tap to set the width"},
    {"Click the opposite corner, or type width, Tab, height, Enter",
     "Tap the opposite corner (then tap a dimension to type an exact size)"},
    {"Click or drag to draw a rectangle", "Tap two corners, or drag, to draw a rectangle"},
    {"Click a corner, or type width, Tab, height, Enter", "Tap a corner (then tap a dimension to type an exact size)"},
    {"The pointer sets the middle of a side " DOT " type the size across flats, Tab for the sides " DOT " +/- change the sides",
     "Tap the middle of a side to set the size " DOT " " MINUS " / + change the sides"},
    {"+/- change the number of sides", MINUS " / + change the number of sides"},
    {"Click to set the size, or type a diameter and press Enter", "Tap to set the size (then tap its dimension to type an exact one)"},
    {"Click the next point " DOT " type a length " DOT " Esc ends the line", "Tap the next point " DOT " tap Line again to end the line"},
    {"Click where the arc ends, or type a radius and press Enter " DOT " Esc ends",
     "Tap where the arc ends " DOT " tap Tangent arc again to stop"},
    {"Move to bend the arc and click, or type a radius and press Enter", "Tap a point the arc passes through"},
    {"Delete removes the constraint " DOT " click elsewhere to keep it", "Delete constraint removes it " DOT " tap elsewhere to keep it"},
    // ---- Sketch messages
    {"Move away from the center to give the rectangle some width and height.",
     "Tap farther from the center to give the rectangle some width and height."},
    {"Move away from the center to give the polygon a size.", "Tap farther from the center to give the polygon a size."},
    {"Move to the side of the curve's direction to bend the arc.", "Tap off to the side of the curve's direction to bend the arc."},
    {"Move the pointer away from the centers to give the slot a width.", "Tap farther from the centers to give the slot a width."},
    // ---- Selecting bodies, faces and edges (taps add to the selection on touch)
    {"Select two bodies: double-click one, then Shift+double-click the other (or Shift-click them in the Model panel).",
     "Select two bodies: double-tap one, then double-tap the other (or tap both in the Model panel)."},
    {"Select the body to keep first, then the body to cut away with Shift+double-click (or Shift-click in the Model panel).",
     "Select the body to keep first, then double-tap the body to cut away (or tap both in the Model panel)."},
    {"(Shift-click adds more)", "(tap more to add them)"},
    {"(Shift-click the second)", "(tap one, then the other)"},
    {"Shift-click adds profiles", "tap more profiles to add them"},
    {"Shift-click to add more edges", "tap more edges to add them"},
    {"Shift-click to open more faces", "tap more faces to open them"},
    {" " DOT " Duplicate (Ctrl+D) makes a copy to drag away " DOT " Shift+double-click another body to combine them",
     " " DOT " Duplicate makes a copy to drag away " DOT " double-tap another body to combine them"},
    // ---- Applying and cancelling: the value chip's ✓ and ✕
    {"Enter to apply " DOT " Esc to cancel " DOT " click elsewhere to apply and continue",
     CHECK " applies " DOT " " CROSS " cancels " DOT " tap elsewhere to apply and continue"},
    {"Enter or Apply mirrors it", "Apply mirrors it"},
    {"Delete removes the face instead", "Delete face removes it instead"},
    {"Esc clears the selection", "tap empty space to clear the selection"},
    {" " DOT " Esc cancels", " " DOT " tap empty space to cancel"},
    {"Enter applies", CHECK " applies"},
    {"(15" DEGREE " steps, Alt for 1" DEGREE ")", "(15" DEGREE " steps)"},
    // ---- Navigating
    {"drag to orbit " DOT " Shift/middle-drag to pan " DOT " scroll to zoom", "drag to orbit " DOT " two fingers pan, pinch zooms"},
    {" and press Enter", ""},
}};

// Word-level rules for everything else, applied after the phrases.
constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kWords{{
    {"Shift+double-click", "double-tap"},
    {"Shift-click", "tap"},
    {"Double-click", "Double-tap"},
    {"double-click", "double-tap"},
    {"Right-click", "Tap the tool again"},
    {"right-click", "tap the tool again"},
    {"clicking", "tapping"},
    {"clicked", "tapped"},
    {"clicks", "taps"},
    {"Click", "Tap"},
    {"click", "tap"},
    {"hover", "tap"},
}};

#undef DOT
#undef MINUS
#undef CHECK
#undef CROSS
#undef DEGREE

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    if (from.empty())
        return;
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

std::string touchWording(std::string_view text)
{
    std::string out(text);
    for (const auto& [from, to] : kPhrases)
        replaceAll(out, from, to);
    for (const auto& [from, to] : kWords)
        replaceAll(out, from, to);
    return out;
}

bool mentionsMouseOrKeyboard(std::string_view text)
{
    const std::string l = lower(text);
    for (std::string_view phrase : {"middle-drag", "middle button", "right-click", "scroll", "hover", "mouse", "keyboard"})
        if (l.find(phrase) != std::string::npos)
            return true;
    // Whole words only ("center" is fine, "Enter" is not; "alternative" is
    // fine, "Alt" is not).
    static constexpr std::array<std::string_view, 13> kWordsBanned{
        "click", "clicks", "clicked", "clicking", "shift", "esc", "enter", "tab", "ctrl", "alt", "pointer", "wheel", "press"};
    std::size_t i = 0;
    while (i < l.size()) {
        while (i < l.size() && !std::isalpha(static_cast<unsigned char>(l[i])))
            ++i;
        std::size_t j = i;
        while (j < l.size() && std::isalpha(static_cast<unsigned char>(l[j])))
            ++j;
        const std::string_view word(l.data() + i, j - i);
        for (std::string_view banned : kWordsBanned)
            if (word == banned)
                return true;
        i = j;
    }
    return false;
}

} // namespace os::interact
