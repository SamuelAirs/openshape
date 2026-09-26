// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/OverlayPlacement.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace os::interact {

ScreenRect ScreenRect::united(const ScreenRect& o) const
{
    return {std::min(left, o.left), std::min(top, o.top), std::max(right, o.right), std::max(bottom, o.bottom)};
}

std::optional<ScreenRect> ScreenRect::clippedTo(const ScreenRect& o) const
{
    const ScreenRect r{std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom)};
    if (r.right < r.left || r.bottom < r.top)
        return std::nullopt;
    return r;
}

void ScreenRect::include(Vec2 p)
{
    left = std::min(left, p.x);
    top = std::min(top, p.y);
    right = std::max(right, p.x);
    bottom = std::max(bottom, p.y);
}

double separation(const ScreenRect& a, const ScreenRect& b)
{
    // Positive: that far apart along the axis; negative: overlapping that much.
    const double dx = std::max(a.left - b.right, b.left - a.right);
    const double dy = std::max(a.top - b.bottom, b.top - a.bottom);
    if (dx > 0 && dy > 0)
        return std::hypot(dx, dy);
    return std::max(dx, dy);
}

std::string_view toString(ChipSpot spot)
{
    switch (spot) {
    case ChipSpot::None: return "none";
    case ChipSpot::Right: return "right";
    case ChipSpot::Left: return "left";
    case ChipSpot::Above: return "above";
    case ChipSpot::Below: return "below";
    case ChipSpot::TopLeft: return "topLeft";
    case ChipSpot::TopRight: return "topRight";
    case ChipSpot::BottomLeft: return "bottomLeft";
    case ChipSpot::BottomRight: return "bottomRight";
    case ChipSpot::DockTop: return "dockTop";
    case ChipSpot::DockBottom: return "dockBottom";
    }
    return "none";
}

double chipMargin(bool touch) { return touch ? 20.0 : 10.0; }
double chipTipGap(bool touch) { return chipMargin(touch) + 14.0; }

namespace {

bool isDock(ChipSpot spot) { return spot == ChipSpot::DockTop || spot == ChipSpot::DockBottom; }

// A blocked docked chip changes sides for a clear one, or for one farther
// away by more than this (no flicker while the view turns).
constexpr double kDockHysteresis = 16;

class Placer {
public:
    explicit Placer(const ChipPlacementInput& in) : in_(in)
    {
        if (in.keepClear)
            blocked_ = in.keepClear->inflated(chipMargin(in.touch));
        // Controls, with the gap the chip keeps from them.
        for (const ScreenRect& r : in.avoid)
            if (r.width() > 0 && r.height() > 0)
                avoid_.push_back(r.inflated(kChipPanelGap));
    }

    ScreenRect rectAt(Vec2 p) const { return ScreenRect::at(p, in_.size); }
    bool fits(const ScreenRect& r) const { return in_.area.contains(r) && !hits(avoid_, r); }
    bool clearOf(const ScreenRect& r) const { return !blocked_ || !blocked_->intersects(r); }
    bool good(Vec2 p) const { return fits(rectAt(p)) && clearOf(rectAt(p)); }

    // Slid into the area (a chip beside a tip near the window's edge slides along it).
    Vec2 clamped(Vec2 p) const
    {
        const double maxX = std::max(in_.area.left, in_.area.right - in_.size.x);
        const double maxY = std::max(in_.area.top, in_.area.bottom - in_.size.y);
        return {std::clamp(p.x, in_.area.left, maxX), std::clamp(p.y, in_.area.top, maxY)};
    }

    std::optional<Vec2> position(ChipSpot spot) const
    {
        const double gap = chipTipGap(in_.touch);
        const Vec2 tip = in_.tip;
        const Vec2 size = in_.size;
        // Next to the tip; where that covers the selection, on the same side
        // just past it (a face beside its arrow: next to the face, level with the tip).
        auto beside = [&](Vec2 nextToTip, Vec2 pastSelection) {
            const Vec2 p = clamped(nextToTip);
            return good(p) || !blocked_ ? p : clamped(pastSelection);
        };
        const ScreenRect past = blocked_.value_or(ScreenRect::around(tip));
        switch (spot) {
        case ChipSpot::Right:
            return beside({tip.x + gap, tip.y - in_.fieldCenter}, {std::max(tip.x + gap, past.right), tip.y - in_.fieldCenter});
        case ChipSpot::Left:
            return beside({tip.x - gap - size.x, tip.y - in_.fieldCenter},
                          {std::min(tip.x - gap, past.left) - size.x, tip.y - in_.fieldCenter});
        case ChipSpot::Above:
            return beside({tip.x - size.x / 2, tip.y - gap - size.y}, {tip.x - size.x / 2, std::min(tip.y - gap, past.top) - size.y});
        case ChipSpot::Below:
            return beside({tip.x - size.x / 2, tip.y + gap}, {tip.x - size.x / 2, std::max(tip.y + gap, past.bottom)});
        case ChipSpot::TopLeft: return corner(true, true);
        case ChipSpot::TopRight: return corner(true, false);
        case ChipSpot::BottomLeft: return corner(false, true);
        case ChipSpot::BottomRight: return corner(false, false);
        case ChipSpot::DockTop: return dockPosition(true);
        case ChipSpot::DockBottom: return dockPosition(false);
        case ChipSpot::None: break;
        }
        return std::nullopt;
    }

    // Docked: the first spot beside the controls from the area's top (below
    // the top bar) or bottom (above the hint) edge, from the left; the bare
    // corner when the controls leave no room (a tiny window).
    Vec2 dockPosition(bool top) const
    {
        if (const auto p = slot(top, true, avoid_))
            return *p;
        return clamped({in_.area.left, top ? in_.area.top : in_.area.bottom - in_.size.y});
    }

    // The free spot nearest to one corner of the area: clear of the controls
    // and of the keep-clear rectangle.
    std::optional<Vec2> corner(bool top, bool fromLeft) const
    {
        std::vector<ScreenRect> obstacles = avoid_;
        if (blocked_)
            obstacles.push_back(*blocked_);
        return slot(top, fromLeft, obstacles);
    }

    ChipPlacement result(ChipSpot spot, Vec2 p) const { return {p, spot, clearOf(rectAt(p))}; }

    // The first spot for the chip clear of `obstacles`, searched in rows from
    // the area's top (or bottom) edge and, in each row, from its left (or
    // right) edge. Only the area's and the obstacles' edges need trying: any
    // free spot slides up (down) and then left (right) until it touches one,
    // so when there is a free spot at all, this finds one.
    std::optional<Vec2> slot(bool top, bool fromLeft, const std::vector<ScreenRect>& obstacles) const
    {
        const Vec2 size = in_.size;
        std::vector<double> ys{top ? in_.area.top : in_.area.bottom - size.y};
        std::vector<double> xs{fromLeft ? in_.area.left : in_.area.right - size.x};
        for (const ScreenRect& o : obstacles) {
            ys.push_back(top ? o.bottom : o.top - size.y);
            xs.push_back(fromLeft ? o.right : o.left - size.x);
        }
        if (top)
            std::sort(ys.begin(), ys.end());
        else
            std::sort(ys.begin(), ys.end(), std::greater<>());
        if (fromLeft)
            std::sort(xs.begin(), xs.end());
        else
            std::sort(xs.begin(), xs.end(), std::greater<>());
        for (const double y : ys)
            for (const double x : xs)
                if (const ScreenRect r = rectAt({x, y}); in_.area.contains(r) && !hits(obstacles, r))
                    return Vec2{x, y};
        return std::nullopt;
    }

    static bool hits(const std::vector<ScreenRect>& obstacles, const ScreenRect& r)
    {
        return std::any_of(obstacles.begin(), obstacles.end(), [&](const ScreenRect& o) { return o.intersects(r); });
    }

    ChipPlacement dock(ChipSpot previous) const
    {
        const Vec2 top = dockPosition(true);
        const Vec2 bottom = dockPosition(false);
        auto at = [&](ChipSpot spot) { return result(spot, spot == ChipSpot::DockTop ? top : bottom); };
        if (!in_.keepClear)
            return at(previous == ChipSpot::DockTop ? ChipSpot::DockTop : ChipSpot::DockBottom);
        const double topDistance = separation(rectAt(top), *in_.keepClear);
        const double bottomDistance = separation(rectAt(bottom), *in_.keepClear);
        if (isDock(previous)) {
            // Stay on the side chosen when the operation started: always
            // while the arrow is dragged, otherwise while that side is clear.
            const ChipSpot other = previous == ChipSpot::DockTop ? ChipSpot::DockBottom : ChipSpot::DockTop;
            const double kept = previous == ChipSpot::DockTop ? topDistance : bottomDistance;
            const double moved = previous == ChipSpot::DockTop ? bottomDistance : topDistance;
            if (in_.frozen || at(previous).clear)
                return at(previous);
            if (at(other).clear || moved > kept + kDockHysteresis)
                return at(other);
            return at(previous);
        }
        // A clear side; else (both clear, or neither) the one farther from the
        // selection and the manipulator; the bottom (near the thumb and the
        // hint) when both are as far.
        if (const bool topClear = at(ChipSpot::DockTop).clear; topClear != at(ChipSpot::DockBottom).clear)
            return at(topClear ? ChipSpot::DockTop : ChipSpot::DockBottom);
        return at(topDistance > bottomDistance + 1 ? ChipSpot::DockTop : ChipSpot::DockBottom);
    }

    ChipPlacement regular(ChipSpot previous) const
    {
        // The spot chosen before, while it is still clear: the chip moves
        // along with the arrow instead of jumping between sides.
        if (previous != ChipSpot::None && !isDock(previous))
            if (const auto p = position(previous); p && good(*p))
                return result(previous, *p);
        for (const ChipSpot spot : {ChipSpot::Right, ChipSpot::Left, ChipSpot::Above, ChipSpot::Below})
            if (const auto p = position(spot); p && good(*p))
                return result(spot, *p);
        // The free area's corners, nearest to the tip first.
        struct Corner {
            double distance;
            ChipSpot spot;
            Vec2 p;
        };
        std::vector<Corner> corners;
        for (const ChipSpot spot : {ChipSpot::TopLeft, ChipSpot::TopRight, ChipSpot::BottomLeft, ChipSpot::BottomRight})
            if (const auto p = position(spot); p && good(*p))
                corners.push_back({(rectAt(*p).center() - in_.tip).length(), spot, *p});
        if (!corners.empty()) {
            const auto nearest = std::min_element(corners.begin(), corners.end(),
                                                  [](const Corner& a, const Corner& b) { return a.distance < b.distance; });
            return result(nearest->spot, nearest->p);
        }
        return dock(previous);
    }

private:
    const ChipPlacementInput& in_;
    std::optional<ScreenRect> blocked_;
    std::vector<ScreenRect> avoid_;
};

} // namespace

ChipPlacement placeValueChip(const ChipPlacementInput& input, ChipSpot previous)
{
    const Placer placer(input);
    if (input.compact)
        return placer.dock(isDock(previous) ? previous : ChipSpot::None);
    return placer.regular(previous);
}

// ---- Live values beside a finger --------------------------------------------------

namespace {
// The fingertip covers about 44 px; the finger and the hand come from below.
constexpr double kShadowHalfWidth = 56;
constexpr double kShadowAbove = 36;
constexpr double kShadowBelow = 240;
// Clear of the shadow by this much.
constexpr double kFingerGap = 14;
// Moved labels stay below this (the top bar's height and the safe area).
constexpr double kTopRoom = 110;
} // namespace

ScreenRect fingerShadow(Vec2 finger)
{
    return {finger.x - kShadowHalfWidth, finger.y - kShadowAbove, finger.x + kShadowHalfWidth, finger.y + kShadowBelow};
}

void keepLabelsClearOfFinger(std::vector<Vec2>& centers, Vec2 labelSize, Vec2 finger, Vec2 viewport)
{
    const ScreenRect shadow = fingerShadow(finger);
    auto box = [&](Vec2 c) { return ScreenRect::at({c.x - labelSize.x / 2, c.y - labelSize.y / 2}, labelSize); };
    std::vector<bool> hidden(centers.size(), false);
    int count = 0;
    for (std::size_t i = 0; i < centers.size(); ++i)
        if (box(centers[i]).intersects(shadow)) {
            hidden[i] = true;
            ++count;
        }
    if (count == 0)
        return;
    const double step = labelSize.y + 6;
    // Above the finger when the stack fits below the top bar, else beside it
    // (on the side with more room), from the fingertip's height down.
    const double firstAbove = shadow.top - kFingerGap - labelSize.y / 2;
    const bool above = firstAbove - (count - 1) * step - labelSize.y / 2 >= kTopRoom;
    const bool toLeft = finger.x > viewport.x / 2;
    const double besideX = toLeft ? shadow.left - kFingerGap - labelSize.x / 2 : shadow.right + kFingerGap + labelSize.x / 2;
    const double firstBeside = std::max(finger.y, kTopRoom + labelSize.y / 2);
    std::vector<ScreenRect> taken;
    for (std::size_t i = 0; i < centers.size(); ++i)
        if (!hidden[i])
            taken.push_back(box(centers[i]));
    int moved = 0;
    for (std::size_t i = 0; i < centers.size(); ++i) {
        if (!hidden[i])
            continue;
        Vec2 c = above ? Vec2{finger.x, firstAbove - moved * step} : Vec2{besideX, firstBeside + moved * step};
        // Not onto a label that stayed (a few steps further at most).
        for (int k = 0; k < 6 && std::any_of(taken.begin(), taken.end(), [&](const ScreenRect& t) { return t.intersects(box(c)); }); ++k)
            c.y += above ? -step : step;
        centers[i] = c;
        taken.push_back(box(c));
        ++moved;
    }
}

} // namespace os::interact
