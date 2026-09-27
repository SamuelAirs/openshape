// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

// Where on-screen overlays go so that they never hide what the user is
// working on: the value chip of an operation (away from the selection, the
// arrows and the tapped point) and the sketch's live values (away from the
// finger). Pure functions of screen rectangles, unit-tested headlessly.
namespace os::interact {

// An axis-aligned rectangle in screen space (logical px, y down). A point or
// a straight edge seen end-on is a rectangle of zero width or height.
struct ScreenRect {
    double left = 0, top = 0, right = 0, bottom = 0;

    static ScreenRect around(Vec2 p) { return {p.x, p.y, p.x, p.y}; }
    static ScreenRect at(Vec2 topLeft, Vec2 size) { return {topLeft.x, topLeft.y, topLeft.x + size.x, topLeft.y + size.y}; }
    double width() const { return right - left; }
    double height() const { return bottom - top; }
    Vec2 topLeft() const { return {left, top}; }
    Vec2 center() const { return {(left + right) / 2, (top + bottom) / 2}; }
    // Overlapping with some area (touching edges do not count). A
    // zero-width rectangle strictly inside the other one does intersect.
    bool intersects(const ScreenRect& o) const { return left < o.right && o.left < right && top < o.bottom && o.top < bottom; }
    // `o` lies inside (within `tolerance` px).
    bool contains(const ScreenRect& o, double tolerance = 0.5) const
    {
        return o.left >= left - tolerance && o.top >= top - tolerance && o.right <= right + tolerance && o.bottom <= bottom + tolerance;
    }
    ScreenRect inflated(double by) const { return {left - by, top - by, right + by, bottom + by}; }
    ScreenRect translated(Vec2 d) const { return {left + d.x, top + d.y, right + d.x, bottom + d.y}; }
    ScreenRect united(const ScreenRect& o) const;
    // The part inside `o`, or nullopt when they do not overlap at all.
    std::optional<ScreenRect> clippedTo(const ScreenRect& o) const;
    void include(Vec2 p);
};

// A window's safe-area insets (the Dynamic Island or notch, the home
// indicator, rounded corners), px.
struct SafeInsets {
    double top = 0, right = 0, bottom = 0, left = 0;

    // The part of a window of `viewport` size inside them.
    ScreenRect inside(Vec2 viewport) const
    {
        return {left, top, std::max(left, viewport.x - right), std::max(top, viewport.y - bottom)};
    }
    bool operator==(const SafeInsets&) const = default;
};

// How far apart two rectangles are: the distance between them when they do
// not overlap, otherwise minus how deep they overlap (the smaller of the
// horizontal and vertical overlap). Larger is farther.
double separation(const ScreenRect& a, const ScreenRect& b);

// ---- The operation's value chip --------------------------------------------------

// Where the chip went. Beside the arrow tip, in a corner of the free area,
// or docked as a bar (below the top bar / above the hint).
enum class ChipSpot { None, Right, Left, Above, Below, TopLeft, TopRight, BottomLeft, BottomRight, DockTop, DockBottom };
std::string_view toString(ChipSpot spot);

struct ChipPlacementInput {
    ScreenRect area;               // where the chip may go: the window inside the safe area
    std::vector<ScreenRect> avoid; // controls it must not cover (top bar, panels, Model/View buttons); empty ones are ignored
    Vec2 size;                     // the chip's width and height
    Vec2 tip;                      // the arrow tip, or where the value belongs (a hole)
    double fieldCenter = 24;       // from the chip's top to the middle of its value field: level with the tip beside it
    // The selection, the manipulator and the last press, on screen: the chip
    // stays a margin away from it. nullopt: nothing to keep clear.
    std::optional<ScreenRect> keepClear;
    bool compact = false; // a phone-sized window: always docked
    bool touch = false;   // a finger: larger margins
    bool frozen = false;  // a manipulator is being dragged: a docked chip keeps its side
    // The value is being typed on an on-screen keyboard (a touch window's
    // field has the focus): the keyboard rises over the bottom of the window,
    // so a docked chip goes below the top bar, whatever else it would pick.
    bool typing = false;
};

struct ChipPlacement {
    Vec2 position; // top-left corner
    ChipSpot spot = ChipSpot::None;
    bool clear = false; // stays the margin away from the keep-clear rectangle
};

// The margin kept around the keep-clear rectangle, and between the arrow tip
// and a chip beside it (the arrow head is about 9 px wide), px.
double chipMargin(bool touch);
double chipTipGap(bool touch);
// Space left between the chip and the controls it avoids, px.
inline constexpr double kChipPanelGap = 8;

// Compact windows dock the chip below the top bar or above the hint,
// whichever is farther from the keep-clear rectangle (the bottom when both
// are clear by the same amount), and keep that side while it stays clear,
// always while a manipulator is dragged; below the top bar while the value is
// typed on an on-screen keyboard. Regular windows try beside the tip
// (right, left, above, below: next to the tip, or on that side just past the
// selection when it reaches beyond the tip), then the free spots nearest
// each corner of the area, the one nearest the tip first (a complete search:
// when there is a free spot at all, one is found), and dock like a phone
// when none of those is clear; they keep the previous
// spot while it is still clear, so small moves of the arrow move the chip
// along with it instead of making it jump. `previous` is the spot chosen last
// for the same selection (None: choose afresh).
ChipPlacement placeValueChip(const ChipPlacementInput& input, ChipSpot previous = ChipSpot::None);

// ---- Live values beside a finger ------------------------------------------------

// A finger (or pen) at `finger` hides the labels next to it: the fingertip and
// the hand below it. Labels (centers, all about `labelSize`) whose box falls
// there move above the finger, stacked in their order, or beside it when
// there is no room above, and stay inside `bounds` (the window inside its
// safe area) from side to side. Others stay put.
void keepLabelsClearOfFinger(std::vector<Vec2>& centers, Vec2 labelSize, Vec2 finger, const ScreenRect& bounds);
// The region a finger at `finger` hides (for tests and callers that check).
ScreenRect fingerShadow(Vec2 finger);

} // namespace os::interact
