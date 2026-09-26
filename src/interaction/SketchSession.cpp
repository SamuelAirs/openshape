// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/SketchSession.h"

#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "core/Units.h"
#include "document/SketchProfiles.h"
#include "geometry/Tessellation.h"
#include "interaction/Manipulator.h"
#include "interaction/OverlayPlacement.h"
#include "sketch/SketchEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace os::interact {

namespace {

constexpr double kInferenceDegrees = 4.0;
constexpr int kCircleSegments = 72;

std::uint64_t nextRegionKey()
{
    static std::uint64_t counter = 1ull << 61;
    return ++counter;
}

// "60", "12.5", "0.25": trailing zeros trimmed, for compact sketch labels.
std::string trimmed(double value, LengthUnit unit)
{
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.3f", fromMillimeters(value, unit));
    std::string s = buffer;
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    if (s == "-0")
        s = "0";
    return s;
}

double sign(double v)
{
    return v < 0 ? -1.0 : 1.0;
}

// Characters (code points) in UTF-8 text, to estimate a label's width.
std::size_t utf8Length(std::string_view text)
{
    return std::size_t(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

// Angle of `p` around `c`, and a counterclockwise sweep in [0, 2pi).
double angleOf(Vec2 c, Vec2 p)
{
    return std::atan2(p.y - c.y, p.x - c.x);
}
double ccw(double from, double to)
{
    double d = std::fmod(to - from, 2 * kPi);
    return d < 0 ? d + 2 * kPi : d;
}

// The glyph drawn for a (non-dimension) constraint; nullptr for dimensions.
const char* constraintGlyph(sketch::ConstraintKind kind)
{
    using K = sketch::ConstraintKind;
    switch (kind) {
    case K::Horizontal: return "H";
    case K::Vertical: return "V";
    case K::Parallel: return "\xE2\x88\xA5";      // parallel to
    case K::Perpendicular: return "\xE2\x8A\xA5"; // up tack
    case K::Equal: return "=";
    case K::Tangent: return "T";
    case K::Concentric: return "\xE2\x97\x8E";    // bullseye
    case K::Coincident: return "\xE2\x97\x8F";    // black circle
    case K::Midpoint: return "M";
    case K::PointOnLine:
    case K::PointOnCircle: return "on";
    case K::Symmetric: return "\xE2\x86\x94"; // left right arrow
    case K::Distance:
    case K::HorizontalDistance:
    case K::VerticalDistance:
    case K::Diameter:
    case K::Radius:
    case K::Angle: return nullptr;
    }
    return nullptr;
}

// Circle through three points; nullopt when they are (nearly) collinear.
std::optional<std::pair<Vec2, double>> circleThrough(Vec2 a, Vec2 b, Vec2 c)
{
    const double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    const double scale = std::max({(b - a).length(), (c - a).length(), 1e-9});
    if (std::abs(d) < 1e-6 * scale * scale)
        return std::nullopt;
    const double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
    const Vec2 center{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
                      (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
    return std::make_pair(center, (a - center).length());
}

} // namespace

SketchSession::SketchSession(doc::Document& document, cmd::UndoStack& undoStack, const Uuid& sketchId)
    : document_(document), undoStack_(undoStack), sketchId_(sketchId)
{
    syncFromDocument();
}

bool SketchSession::isValid() const
{
    return document_.sketch(sketchId_) != nullptr;
}

void SketchSession::syncFromDocument()
{
    const sketch::Sketch* current = document_.sketch(sketchId_);
    if (!current)
        return;
    const std::uint64_t revision = document_.sketchRevision(sketchId_);
    if (revision == syncedRevision_)
        return;
    working_ = *current;
    syncedRevision_ = revision;
    cancelModes(); // the curves they work on may be gone
    if (working_.solveReport().degreesOfFreedom < 0)
        (void)sketch::solve(working_); // loaded from file: compute DOF for display
    std::erase_if(selected_, [&](sketch::EntityId id) {
        return !working_.point(id) && !working_.line(id) && !working_.circle(id) && !working_.arc(id)
            && !working_.constraint(id);
    });
    regionsChanged();
}

void SketchSession::setTool(SketchTool tool)
{
    tool_ = tool;
    resetShape();
    cancelOffset();
    cancelModes();
    trimCursor_.reset();
    hoveredGlyph_ = sketch::kNoEntity;
    if (tool != SketchTool::Select)
        selected_.clear();
}

// ---- Coordinates, snapping, picking -----------------------------------------------

std::optional<Vec2> SketchSession::toLocal(Vec2 screen, const Camera& camera) const
{
    return working_.plane().intersect(camera.rayAt(screen));
}

Vec2 SketchSession::toScreen(Vec2 local, const Camera& camera) const
{
    return camera.project(working_.plane().toWorld(local));
}

SketchSession::Snap SketchSession::snapAt(Vec2 screen, const Camera& camera, PointerDevice device) const
{
    Snap snap;
    const auto local = toLocal(screen, camera);
    if (!local)
        return snap;
    const double tolerance = InputProfile::forDevice(device).pickTolerance + 4;
    snap.position = *local;

    // 1. Existing points (the origin included).
    double best = tolerance;
    for (const auto& [id, p] : working_.points()) {
        const double d = (toScreen(p.position, camera) - screen).length();
        if (d < best) {
            best = d;
            snap.position = p.position;
            snap.point = id;
            snap.kind = id == sketch::kOriginId ? SnapKind::Origin : SnapKind::Point;
        }
    }
    if (snap.point != sketch::kNoEntity)
        return snap;

    // 2. Line midpoints (position only; no constraint is added).
    for (const auto& [id, l] : working_.lines()) {
        const Vec2 mid = (working_.point(l.start)->position + working_.point(l.end)->position) * 0.5;
        const double d = (toScreen(mid, camera) - screen).length();
        if (d < best) {
            best = d;
            snap.position = mid;
            snap.kind = SnapKind::Midpoint;
        }
    }
    if (snap.kind == SnapKind::Midpoint)
        return snap;

    // 3. Horizontal / vertical inference relative to the shape's first point.
    const double pixel = camera.pixelSize(working_.plane().toWorld(*local));
    const double step = snapIncrement(pixel, 10.0);
    Vec2 p = *local;
    if (anchor_ && (tool_ == SketchTool::Line || tool_ == SketchTool::Polygon)) {
        const Vec2 d = p - anchor_->position;
        if (d.length() > 4 * pixel) {
            const double angle = std::atan2(std::abs(d.y), std::abs(d.x)) * 180.0 / kPi;
            if (angle < kInferenceDegrees) {
                p.y = anchor_->position.y;
                snap.horizontal = true;
            } else if (angle > 90.0 - kInferenceDegrees) {
                p.x = anchor_->position.x;
                snap.vertical = true;
            }
        }
    }
    // 4. Round free coordinates to a zoom-dependent grid (unless turned off).
    if (gridSnap_) {
        if (!snap.horizontal)
            p.y = snapValue(p.y, step);
        if (!snap.vertical)
            p.x = snapValue(p.x, step);
        if (snap.horizontal)
            p.x = snapValue(p.x, step);
        if (snap.vertical)
            p.y = snapValue(p.y, step);
    }
    snap.position = p;
    snap.kind = gridSnap_ ? SnapKind::Grid : SnapKind::None;
    return snap;
}

sketch::EntityId SketchSession::pickCurve(Vec2 screen, const Camera& camera, PointerDevice device) const
{
    return pickEntity(screen, camera, device, true);
}

sketch::EntityId SketchSession::pickEntity(Vec2 screen, const Camera& camera, PointerDevice device, bool curvesOnly) const
{
    const double tolerance = InputProfile::forDevice(device).pickTolerance + 2;
    sketch::EntityId best = sketch::kNoEntity;
    double bestDistance = tolerance;
    // Points win over curves so endpoints stay grabbable.
    if (!curvesOnly) {
        for (const auto& [id, p] : working_.points()) {
            const double d = (toScreen(p.position, camera) - screen).length();
            if (d < bestDistance) {
                bestDistance = d;
                best = id;
            }
        }
        if (best != sketch::kNoEntity)
            return best;
    }
    for (const auto& [id, l] : working_.lines()) {
        const double d = distanceToSegment2D(screen, toScreen(working_.point(l.start)->position, camera),
                                             toScreen(working_.point(l.end)->position, camera));
        if (d < bestDistance) {
            bestDistance = d;
            best = id;
        }
    }
    for (const auto& [id, c] : working_.circles()) {
        const Vec2 center = working_.point(c.center)->position;
        const auto local = toLocal(screen, camera);
        if (!local)
            continue;
        const double pixel = camera.pixelSize(working_.plane().toWorld(center));
        const double d = std::abs((*local - center).length() - c.radius) / pixel;
        if (d < bestDistance) {
            bestDistance = d;
            best = id;
        }
    }
    for (const auto& [id, a] : working_.arcs()) {
        const auto local = toLocal(screen, camera);
        if (!local)
            continue;
        const Vec2 center = working_.point(a.center)->position;
        const double a0 = angleOf(center, working_.point(a.start)->position);
        const double sweep = ccw(a0, angleOf(center, working_.point(a.end)->position));
        if (ccw(a0, angleOf(center, *local)) > sweep)
            continue; // beside the arc, not on it
        const double pixel = camera.pixelSize(working_.plane().toWorld(center));
        const double d = std::abs((*local - center).length() - working_.arcRadius(id)) / pixel;
        if (d < bestDistance) {
            bestDistance = d;
            best = id;
        }
    }
    return best;
}

// ---- Shapes -------------------------------------------------------------------------

void SketchSession::beginShape(const Snap& at)
{
    anchor_ = at;
    inputs_.clear();
    focusedInput_ = 0;
    switch (tool_) {
    case SketchTool::Rectangle:
    case SketchTool::CenterRectangle:
        inputs_ = {{"width", "W", "", false, 0}, {"height", "H", "", false, 0}};
        break;
    case SketchTool::Circle:
        inputs_ = {{"diameter", "\xC3\x98", "", false, 0}};
        break;
    case SketchTool::Line:
        inputs_ = {{"length", "L", "", false, 0}};
        break;
    case SketchTool::Polygon: // the size across flats, then (Tab) the number of sides
        inputs_ = {{"size", "The size", "", false, 0}, {"sides", "Sides", "", false, 0}};
        break;
    case SketchTool::TangentArc:
        inputs_ = {{"radius", "R", "", false, 0}};
        break;
    case SketchTool::Arc:  // the radius input appears once the end is placed
    case SketchTool::Slot: // the width input appears once the second center is placed
    case SketchTool::Trim:
    case SketchTool::Select:
        break;
    }
}

void SketchSession::resetShape()
{
    anchor_.reset();
    arcEnd_.reset();
    chainStart_ = sketch::kNoEntity;
    tangentStart_ = {};
    inputs_.clear();
    focusedInput_ = 0;
}

std::optional<double> SketchSession::input(const std::string& key) const
{
    for (const auto& in : inputs_)
        if (in.key == key && in.locked)
            return in.value;
    return std::nullopt;
}

Vec2 SketchSession::constrainedCursor() const
{
    if (!anchor_)
        return cursor_.position;
    const Vec2 a = anchor_->position;
    Vec2 c = cursor_.position;
    switch (tool_) {
    case SketchTool::Rectangle:
        if (const auto w = input("width"))
            c.x = a.x + sign(c.x - a.x) * *w;
        if (const auto h = input("height"))
            c.y = a.y + sign(c.y - a.y) * *h;
        break;
    case SketchTool::CenterRectangle: // the anchor is the center: typed sizes are full widths
        if (const auto w = input("width"))
            c.x = a.x + sign(c.x - a.x) * *w / 2;
        if (const auto h = input("height"))
            c.y = a.y + sign(c.y - a.y) * *h / 2;
        break;
    case SketchTool::Line:
        if (const auto len = input("length")) {
            Vec2 d = c - a;
            const double l = d.length();
            d = l > 1e-9 ? d * (1.0 / l) : Vec2{1, 0};
            c = a + d * *len;
        }
        break;
    case SketchTool::Circle:
        if (const auto dia = input("diameter")) {
            Vec2 d = c - a;
            const double l = d.length();
            d = l > 1e-9 ? d * (1.0 / l) : Vec2{1, 0};
            c = a + d * (*dia / 2);
        }
        break;
    case SketchTool::Polygon: // the cursor is the middle of a side: half the size across flats away
        if (const auto size = input("size")) {
            Vec2 d = c - a;
            const double l = d.length();
            d = l > 1e-9 ? d * (1.0 / l) : Vec2{1, 0};
            c = a + d * (*size / 2);
        }
        break;
    case SketchTool::Arc:
    case SketchTool::Slot:
    case SketchTool::TangentArc: // tangentArcShape() applies the typed radius
    case SketchTool::Trim:
    case SketchTool::Select:
        break;
    }
    return c;
}

std::optional<SketchSession::TangentStart> SketchSession::tangentStartAt(sketch::EntityId point, bool* ambiguous) const
{
    // The one curve ending at the point. Construction curves (a center
    // rectangle's diagonal, a center line) count only when no profile curve
    // ends there. Where two profile curves meet (a corner), which one to
    // continue is unclear: none.
    if (ambiguous)
        *ambiguous = false;
    const auto* at = working_.point(point);
    if (!at)
        return std::nullopt;
    const Vec2 p = at->position;
    std::vector<TangentStart> profile, guides;
    for (const auto& [id, l] : working_.lines()) {
        if (l.start != point && l.end != point)
            continue;
        const Vec2 d = p - working_.point(l.start == point ? l.end : l.start)->position;
        if (d.length() > 1e-9)
            (l.construction ? guides : profile).push_back({id, d * (1.0 / d.length())});
    }
    for (const auto& [id, a] : working_.arcs()) {
        if (a.start != point && a.end != point)
            continue;
        const Vec2 r = p - working_.point(a.center)->position;
        if (r.length() < 1e-9)
            continue;
        const Vec2 ccwTangent = Vec2{-r.y, r.x} * (1.0 / r.length());
        // Leaving the arc's end we travel counterclockwise; leaving its start, clockwise.
        (a.construction ? guides : profile).push_back({id, a.end == point ? ccwTangent : ccwTangent * -1.0});
    }
    const auto& candidates = profile.empty() ? guides : profile;
    if (candidates.size() == 1)
        return candidates.front();
    if (ambiguous)
        *ambiguous = candidates.size() > 1;
    return std::nullopt;
}

std::optional<SketchSession::ArcShape> SketchSession::tangentArcShape() const
{
    if (!anchor_ || tangentStart_.curve == sketch::kNoEntity)
        return std::nullopt;
    const Vec2 s = anchor_->position, t = tangentStart_.direction, left{-t.y, t.x};
    const Vec2 pointer = cursor_.position;
    ArcShape arc;
    Vec2 end;
    // Which way it turns: toward the pointer's side of the tangent.
    const double side = (pointer - s).dot(left);
    if (const auto r = input("radius")) {
        arc.center = s + left * (side < 0 ? -*r : *r);
        const Vec2 out = pointer - arc.center;
        if (out.length() < 1e-9)
            return std::nullopt;
        end = arc.center + out * (*r / out.length());
        arc.radius = *r;
    } else {
        const Vec2 v = pointer - s;
        if (std::abs(side) < 1e-6 * std::max(v.length(), 1e-9) || v.length() < 1e-9)
            return std::nullopt; // straight ahead or behind: no finite arc
        const double signedRadius = v.dot(v) / (2 * side);
        arc.center = s + left * signedRadius;
        arc.radius = std::abs(signedRadius);
        end = pointer;
    }
    if ((end - s).length() < 1e-9)
        return std::nullopt;
    // Turning left is counterclockwise from the anchor; turning right is
    // clockwise, i.e. counterclockwise from the far end back to the anchor.
    arc.swapped = side < 0;
    arc.start = arc.swapped ? end : s;
    arc.end = arc.swapped ? s : end;
    return arc;
}

std::optional<SketchSession::SlotShape> SketchSession::slotShape() const
{
    if (!anchor_ || !arcEnd_)
        return std::nullopt;
    SlotShape slot{anchor_->position, arcEnd_->position, 0};
    const Vec2 axis = slot.b - slot.a;
    const double length = axis.length();
    if (length < 1e-9)
        return std::nullopt;
    if (const auto width = input("slot")) {
        slot.radius = *width / 2;
    } else {
        // The pointer's distance from the line through both centers.
        const Vec2 d = cursor_.position - slot.a;
        slot.radius = std::abs(axis.x * d.y - axis.y * d.x) / length;
    }
    if (slot.radius < 1e-6)
        return std::nullopt;
    return slot;
}

std::optional<SketchSession::ArcShape> SketchSession::arcShape() const
{
    if (!anchor_ || !arcEnd_)
        return std::nullopt;
    const Vec2 a = anchor_->position, b = arcEnd_->position, bulge = cursor_.position;
    ArcShape arc;
    Vec2 onArc = bulge; // a point the arc must pass through (decides its direction)
    if (const auto r = input("radius")) {
        // Center on the far side of the chord from the pointer: the arc bulges
        // toward the pointer (the shorter arc for a radius above half the chord).
        const Vec2 m = (a + b) * 0.5, chord = b - a;
        const double half = chord.length() / 2;
        if (*r < half - 1e-9)
            return std::nullopt;
        Vec2 n{-chord.y, chord.x};
        n = n * (1.0 / std::max(n.length(), 1e-12));
        if ((bulge - m).x * n.x + (bulge - m).y * n.y < 0)
            n = n * -1.0;
        const double h = std::sqrt(std::max(*r * *r - half * half, 0.0));
        arc.center = m - n * h;
        arc.radius = *r;
        onArc = arc.center + n * *r;
    } else {
        const auto circle = circleThrough(a, b, bulge);
        if (!circle)
            return std::nullopt;
        arc.center = circle->first;
        arc.radius = circle->second;
    }
    const double a0 = angleOf(arc.center, a);
    arc.swapped = ccw(a0, angleOf(arc.center, onArc)) > ccw(a0, angleOf(arc.center, b));
    arc.start = arc.swapped ? b : a;
    arc.end = arc.swapped ? a : b;
    return arc;
}

bool SketchSession::finishShape(const Snap& endSnap)
{
    if (!anchor_)
        return false;
    Snap end = endSnap;
    cursor_ = endSnap;
    const bool typed = std::any_of(inputs_.begin(), inputs_.end(), [](const Input& i) { return i.locked; });
    if (typed) {
        end.position = constrainedCursor();
        end.point = sketch::kNoEntity; // typed values win over snapping
    }
    const Snap start = *anchor_;
    sketch::Sketch next = working_;
    constexpr double kTiny = 1e-6;

    switch (tool_) {
    case SketchTool::Rectangle: {
        const Vec2 a = start.position, b = end.position;
        if (std::abs(b.x - a.x) < kTiny || std::abs(b.y - a.y) < kTiny) {
            message("Drag out a rectangle with some width and height.");
            return false;
        }
        const auto ids = sketch::addRectangle(next, a, b, start.point);
        if (end.point != sketch::kNoEntity)
            next.addConstraint({sketch::ConstraintKind::Coincident, ids.corners[2], end.point});
        if (input("width"))
            next.addConstraint({sketch::ConstraintKind::HorizontalDistance, ids.corners[0], ids.corners[1], b.x - a.x});
        if (input("height"))
            next.addConstraint({sketch::ConstraintKind::VerticalDistance, ids.corners[1], ids.corners[2], b.y - a.y});
        if (!commit(std::move(next), "Rectangle"))
            return false;
        resetShape();
        return true;
    }
    case SketchTool::CenterRectangle: {
        const Vec2 center = start.position, corner = end.position;
        if (std::abs(corner.x - center.x) < kTiny || std::abs(corner.y - center.y) < kTiny) {
            message("Move away from the center to give the rectangle some width and height.");
            return false;
        }
        const auto ids = sketch::addCenterRectangle(next, center, corner, start.point);
        if (ids.center == sketch::kNoEntity)
            return false;
        const auto& corners = ids.rectangle.corners;
        if (end.point != sketch::kNoEntity && end.point != ids.center)
            next.addConstraint({sketch::ConstraintKind::Coincident, corners[2], end.point});
        const Vec2 low = next.point(corners[0])->position;
        if (input("width"))
            next.addConstraint({sketch::ConstraintKind::HorizontalDistance, corners[0], corners[1], corner.x - low.x});
        if (input("height"))
            next.addConstraint({sketch::ConstraintKind::VerticalDistance, corners[1], corners[2], corner.y - low.y});
        if (!commit(std::move(next), "Center rectangle"))
            return false;
        resetShape();
        return true;
    }
    case SketchTool::Polygon: {
        if ((end.position - start.position).length() < kTiny) {
            message("Move away from the center to give the polygon a size.");
            return false;
        }
        const auto ids = sketch::addPolygon(next, start.position, end.position, polygonSides_, start.point);
        if (ids.sides.empty())
            return false;
        // The pointer straight beside or above the center (shown as a guide)
        // makes the first side vertical or horizontal.
        if (end.horizontal)
            next.addConstraint({sketch::ConstraintKind::Vertical, ids.sides[0]});
        else if (end.vertical)
            next.addConstraint({sketch::ConstraintKind::Horizontal, ids.sides[0]});
        if (const auto size = input("size"))
            next.addConstraint({sketch::ConstraintKind::Diameter, ids.inner, sketch::kNoEntity, *size});
        if (!commit(std::move(next), "Polygon"))
            return false;
        resetShape();
        return true;
    }
    case SketchTool::Circle: {
        const double radius = (end.position - start.position).length();
        if (radius < kTiny) {
            message("Drag out a circle with some size.");
            return false;
        }
        const sketch::EntityId center = start.point != sketch::kNoEntity ? start.point : next.addPoint(start.position);
        const sketch::EntityId circle = next.addCircle(center, radius);
        if (const auto d = input("diameter"))
            next.addConstraint({sketch::ConstraintKind::Diameter, circle, sketch::kNoEntity, *d});
        if (!commit(std::move(next), "Circle"))
            return false;
        resetShape();
        return true;
    }
    case SketchTool::Line: {
        if ((end.position - start.position).length() < kTiny || (end.point != sketch::kNoEntity && end.point == start.point)) {
            return false;
        }
        const sketch::EntityId a = start.point != sketch::kNoEntity ? start.point : next.addPoint(start.position);
        const sketch::EntityId b = end.point != sketch::kNoEntity ? end.point : next.addPoint(end.position);
        const sketch::EntityId line = next.addLine(a, b);
        if (end.horizontal && !typed)
            next.addConstraint({sketch::ConstraintKind::Horizontal, line});
        if (end.vertical && !typed)
            next.addConstraint({sketch::ConstraintKind::Vertical, line});
        if (const auto len = input("length"))
            next.addConstraint({sketch::ConstraintKind::Distance, a, b, *len});
        if (!commit(std::move(next), "Line"))
            return false;
        // Continue the chain from the new end point, unless it closed a loop.
        if (chainStart_ == sketch::kNoEntity)
            chainStart_ = a;
        if (end.point != sketch::kNoEntity && end.point == chainStart_) {
            resetShape();
        } else {
            Snap nextStart;
            nextStart.position = working_.point(b)->position;
            nextStart.point = b;
            nextStart.kind = SnapKind::Point;
            const sketch::EntityId keepChain = chainStart_;
            beginShape(nextStart);
            chainStart_ = keepChain;
        }
        return true;
    }
    case SketchTool::Arc: {
        if (!arcEnd_) {
            // Second click: where the arc ends. The third click (or a typed
            // radius) bends it.
            if ((end.position - start.position).length() < kTiny || (end.point != sketch::kNoEntity && end.point == start.point))
                return false;
            arcEnd_ = end;
            inputs_ = {{"radius", "R", "", false, 0}};
            focusedInput_ = 0;
            return true;
        }
        cursor_ = endSnap; // the bulge point is where the pointer is, not snapped by typing
        const auto arc = arcShape();
        if (!arc) {
            message(input("radius") ? "The radius must be at least half the distance between the ends."
                                    : "Move the pointer off the line between the ends to bend the arc.");
            return false;
        }
        const Snap& first = arc->swapped ? *arcEnd_ : start;
        const Snap& last = arc->swapped ? start : *arcEnd_;
        const sketch::EntityId s = first.point != sketch::kNoEntity ? first.point : next.addPoint(arc->start);
        const sketch::EntityId e = last.point != sketch::kNoEntity ? last.point : next.addPoint(arc->end);
        const sketch::EntityId center = next.addPoint(arc->center);
        const sketch::EntityId id = next.addArc(center, s, e);
        if (id == sketch::kNoEntity)
            return false;
        if (const auto r = input("radius"))
            next.addConstraint({sketch::ConstraintKind::Radius, id, sketch::kNoEntity, *r});
        if (!commit(std::move(next), "Arc"))
            return false;
        resetShape();
        return true;
    }
    case SketchTool::TangentArc: {
        cursor_ = endSnap;
        const auto arc = tangentArcShape();
        if (!arc) {
            message("Move to the side of the curve's direction to bend the arc.");
            return false;
        }
        const Vec2 farEnd = arc->swapped ? arc->start : arc->end;
        const bool reuseEnd = !typed && end.point != sketch::kNoEntity && end.point != start.point;
        const sketch::EntityId s = start.point;
        const sketch::EntityId e = reuseEnd ? end.point : next.addPoint(farEnd);
        const sketch::EntityId center = next.addPoint(arc->center);
        const sketch::EntityId id = next.addArc(center, arc->swapped ? e : s, arc->swapped ? s : e);
        if (id == sketch::kNoEntity)
            return false;
        next.addConstraint({sketch::ConstraintKind::Tangent, tangentStart_.curve, id});
        if (const auto r = input("radius"))
            next.addConstraint({sketch::ConstraintKind::Radius, id, sketch::kNoEntity, *r});
        if (!commit(std::move(next), "Tangent arc"))
            return false;
        // Continue from the far end, tangent to the new arc (Esc ends the chain).
        const sketch::EntityId farPoint = e;
        const auto continued = tangentStartAt(farPoint);
        if (reuseEnd || !continued || !working_.point(farPoint)) {
            resetShape();
            return true;
        }
        Snap nextStart;
        nextStart.position = working_.point(farPoint)->position;
        nextStart.point = farPoint;
        nextStart.kind = SnapKind::Point;
        beginShape(nextStart);
        tangentStart_ = *continued;
        return true;
    }
    case SketchTool::Slot: {
        if (!arcEnd_) {
            // Second click: the other center. The pointer (or a typed width)
            // then sets the width.
            if ((end.position - start.position).length() < kTiny || (end.point != sketch::kNoEntity && end.point == start.point))
                return false;
            arcEnd_ = end;
            inputs_ = {{"slot", "W", "", false, 0}};
            focusedInput_ = 0;
            return true;
        }
        cursor_ = endSnap;
        const auto slot = slotShape();
        if (!slot) {
            message("Move the pointer away from the centers to give the slot a width.");
            return false;
        }
        const auto ids = sketch::addSlot(next, slot->a, slot->b, slot->radius, start.point, arcEnd_->point);
        if (ids.arcs[0] == sketch::kNoEntity)
            return false;
        if (const auto width = input("slot"))
            next.addConstraint({sketch::ConstraintKind::Radius, ids.arcs[0], sketch::kNoEntity, *width / 2});
        if (!commit(std::move(next), "Slot"))
            return false;
        resetShape();
        return true;
    }
    case SketchTool::Trim:
    case SketchTool::Select:
        break;
    }
    return false;
}

bool SketchSession::commit(sketch::Sketch next, const std::string& label)
{
    const sketch::SolveReport report = sketch::solve(next);
    if (!report.ok) {
        message(report.message.empty() ? "That change conflicts with existing constraints." : report.message);
        return false;
    }
    const Status status = undoStack_.push(std::make_unique<cmd::EditSketchCommand>(next, label), document_);
    if (!status) {
        message(status.userMessage());
        return false;
    }
    syncFromDocument();
    if (onCommitted)
        onCommitted();
    return true;
}

void SketchSession::message(const std::string& text) const
{
    OS_LOG(Info, Sketch) << "user message: " << text;
    if (onMessage)
        onMessage(text);
}

void SketchSession::regionsChanged()
{
    regions_.clear();
    regionMeshes_.clear();
    regionKeys_.clear();
    auto regions = doc::sketchRegions(working_);
    if (regions) {
        regions_ = std::move(regions.value());
        for (const auto& r : regions_) {
            regionMeshes_.push_back(std::make_shared<const geom::Mesh>(geom::tessellate(r.face)));
            regionKeys_.push_back(nextRegionKey());
        }
    }
}

// ---- Input -----------------------------------------------------------------------------

bool SketchSession::pointerPress(const PointerEvent& event, const Camera& camera)
{
    notePointer(event);
    if (event.button != PointerButton::Left)
        return false;
    pressed_ = true;
    dragging_ = false;
    pressScreen_ = event.position;
    pressEvent_ = event;
    dragPoint_ = sketch::kNoEntity;

    if (isOffsetting() || isMirroring() || isPatterning())
        return true; // the release applies the offset / picks the line / places the pattern
    if (tool_ == SketchTool::Trim) {
        if (pickCurve(event.position, camera, event.device) == sketch::kNoEntity) {
            pressed_ = false;
            return false; // empty space: let the controller orbit
        }
        return true;
    }
    if (tool_ == SketchTool::Select) {
        const sketch::EntityId hit = pickEntity(event.position, camera, event.device);
        if (hit == sketch::kNoEntity) {
            // A constraint glyph, only when no point or curve is within reach
            // (a glyph's tap target never hides geometry); the release selects it.
            if (glyphAt(event.position, camera) != sketch::kNoEntity)
                return true;
            pressed_ = false;
            return false; // let the controller orbit / clear selection
        }
        if (const auto* p = working_.point(hit); p && !p->fixed) {
            dragPoint_ = hit;
            dragStart_ = working_;
        }
        return true;
    }

    const Snap snap = snapAt(event.position, camera, event.device);
    cursor_ = snap;
    cursorValid_ = true;
    if (!anchor_ && tool_ == SketchTool::TangentArc) {
        bool ambiguous = false;
        const auto start = snap.point != sketch::kNoEntity ? tangentStartAt(snap.point, &ambiguous) : std::nullopt;
        if (!start) {
            message(ambiguous ? "Curves meet there: start a tangent arc on a free end of a line or an arc."
                              : "Start a tangent arc on the end of a line or an arc.");
            return true;
        }
        beginShape(snap);
        tangentStart_ = *start;
        return true;
    }
    if (!anchor_)
        beginShape(snap);
    return true;
}

void SketchSession::pointerMove(const PointerEvent& event, const Camera& camera)
{
    notePointer(event);
    if (isOffsetting()) {
        updateOffset(toLocal(event.position, camera));
        return;
    }
    if (isMirroring() || isPatterning()) {
        hover(event, camera);
        return;
    }
    if (!pressed_) {
        hover(event, camera);
        return;
    }
    const double threshold = InputProfile::forDevice(pressEvent_.device).dragThreshold;
    if (!dragging_ && (event.position - pressScreen_).length() >= threshold)
        dragging_ = true;

    if (tool_ == SketchTool::Select) {
        if (dragging_ && dragPoint_ != sketch::kNoEntity) {
            const auto local = toLocal(event.position, camera);
            if (!local)
                return;
            sketch::Sketch trial = dragStart_;
            if (sketch::solveDragging(trial, dragPoint_, *local).ok) {
                working_ = std::move(trial);
                regionsChanged();
            }
        }
        return;
    }
    cursor_ = snapAt(event.position, camera, event.device);
    cursorValid_ = true;
}

void SketchSession::pointerRelease(const PointerEvent& event, const Camera& camera)
{
    notePointer(event);
    if (!pressed_)
        return;
    pressed_ = false;

    if (isOffsetting()) {
        updateOffset(toLocal(event.position, camera));
        (void)commitOffset();
        dragging_ = false;
        return;
    }
    if (isMirroring()) {
        const sketch::EntityId axis = pickCurve(event.position, camera, event.device);
        if (axis != sketch::kNoEntity && working_.line(axis))
            (void)applyMirror(axis);
        else
            message("Click a straight line to mirror across.");
        dragging_ = false;
        return;
    }
    if (isPatterning()) {
        patternClick(snapAt(event.position, camera, event.device));
        dragging_ = false;
        return;
    }
    if (tool_ == SketchTool::Trim) {
        const double threshold = InputProfile::forDevice(event.device).dragThreshold;
        const sketch::EntityId curve = pickCurve(event.position, camera, event.device);
        const auto local = toLocal(event.position, camera);
        if ((event.position - pressScreen_).length() < threshold && curve != sketch::kNoEntity && local) {
            sketch::Sketch next = working_;
            if (const Status status = sketch::trimAt(next, curve, *local); !status)
                message(status.userMessage());
            else if (commit(std::move(next), "Trim"))
                hovered_ = sketch::kNoEntity;
        }
        dragging_ = false;
        return;
    }
    if (tool_ == SketchTool::Select) {
        if (dragging_ && dragPoint_ != sketch::kNoEntity) {
            sketch::Sketch moved = working_;
            working_ = dragStart_;
            if (!commit(std::move(moved), "Move point"))
                regionsChanged();
        } else {
            sketch::EntityId hit = pickEntity(event.position, camera, event.device);
            if (hit == sketch::kNoEntity)
                hit = glyphAt(event.position, camera);
            const bool additive = InputProfile::forDevice(event.device).additiveSelection || event.modifiers.shift
                               || event.modifiers.control;
            select(hit, additive);
        }
        dragPoint_ = sketch::kNoEntity;
        dragging_ = false;
        return;
    }

    // Drawing tools: a press-drag-release draws the whole shape in one gesture;
    // a click leaves the first point placed and waits for the second click.
    const Snap snap = snapAt(event.position, camera, event.device);
    const bool isSecondClick = !dragging_ && anchor_ && (anchor_->position - snap.position).length() > 1e-9
                            && (toScreen(anchor_->position, camera) - event.position).length()
                                   > InputProfile::forDevice(event.device).dragThreshold;
    if (dragging_ || isSecondClick)
        finishShape(snap);
    dragging_ = false;
}

void SketchSession::hover(const PointerEvent& event, const Camera& camera)
{
    notePointer(event);
    if (isOffsetting()) {
        updateOffset(toLocal(event.position, camera));
        return;
    }
    if (isMirroring()) {
        const sketch::EntityId line = pickCurve(event.position, camera, event.device);
        const bool usable = line != sketch::kNoEntity && working_.line(line)
                         && std::find(mirrorSource_.begin(), mirrorSource_.end(), line) == mirrorSource_.end();
        const sketch::EntityId axis = usable ? line : sketch::kNoEntity;
        if (axis != mirrorAxis_) {
            mirrorAxis_ = axis;
            if (axis != sketch::kNoEntity)
                updatePreview([&](sketch::Sketch& s) { return sketch::mirrorCurves(s, mirrorSource_, axis); });
            else
                previewCurves_.clear();
        }
        hovered_ = axis;
        return;
    }
    if (isPatterning()) {
        cursor_ = snapAt(event.position, camera, event.device);
        cursorValid_ = true;
        return;
    }
    if (tool_ == SketchTool::Trim) {
        hovered_ = pickCurve(event.position, camera, event.device);
        trimCursor_ = toLocal(event.position, camera);
        cursorValid_ = false;
        return;
    }
    if (tool_ == SketchTool::Select) {
        hovered_ = pickEntity(event.position, camera, event.device);
        hoveredGlyph_ = hovered_ == sketch::kNoEntity ? glyphAt(event.position, camera) : sketch::kNoEntity;
        cursorValid_ = false;
        return;
    }
    cursor_ = snapAt(event.position, camera, event.device);
    cursorValid_ = true;
}

void SketchSession::leave()
{
    hovered_ = sketch::kNoEntity;
    hoveredGlyph_ = sketch::kNoEntity;
    cursorValid_ = false;
}

bool SketchSession::keyPress(Key key)
{
    switch (key) {
    case Key::Escape:
        if (isOffsetting()) {
            cancelOffset();
            return true;
        }
        if (isMirroring() || isPatterning()) {
            cancelModes();
            return true;
        }
        if (anchor_) {
            resetShape();
            return true;
        }
        if (tool_ != SketchTool::Select) {
            setTool(SketchTool::Select);
            return true;
        }
        if (!selected_.empty()) {
            selected_.clear();
            return true;
        }
        return false;
    case Key::Enter:
        if (anchor_ || isOffsetting() || isPatterning()) {
            (void)commitTool();
            return true;
        }
        return false;
    case Key::Delete:
    case Key::Backspace:
        if (!selected_.empty()) {
            (void)triggerAction("delete");
            return true;
        }
        return false;
    case Key::Other:
        break;
    }
    return false;
}

// ---- Counters -------------------------------------------------------------------------------

std::optional<SketchCounter> SketchSession::counter() const
{
    if (isPatterning())
        return SketchCounter{std::to_string(pattern_.count) + " in total", pattern_.count};
    if (tool_ == SketchTool::Polygon)
        return SketchCounter{std::to_string(polygonSides_) + " sides", polygonSides_};
    return std::nullopt;
}

bool SketchSession::stepCounter(int delta)
{
    if (isPatterning()) {
        const int count = std::clamp(pattern_.count + delta, 2, sketch::kMaxPatternCount);
        if (count == pattern_.count)
            return false;
        pattern_.count = count;
        for (auto& in : inputs_)
            if (in.key == "count") {
                in.text.clear();
                in.locked = false;
            }
        updatePatternPreview();
        return true;
    }
    if (tool_ != SketchTool::Polygon)
        return false;
    const int sides = std::clamp(polygonSides_ + delta, sketch::kMinPolygonSides, sketch::kMaxPolygonSides);
    if (sides == polygonSides_)
        return false;
    polygonSides_ = sides;
    for (auto& in : inputs_)
        if (in.key == "sides") { // the typed count gives way to the new one
            in.text.clear();
            in.locked = false;
        }
    return true;
}

// ---- Typed values -------------------------------------------------------------------------

std::string SketchSession::typeIntoInput(const std::string& text)
{
    if (inputs_.empty())
        return "Start drawing a shape first.";
    return setInput(inputs_[focusedInput_].key, text);
}

std::string SketchSession::setInput(const std::string& key, const std::string& text)
{
    for (std::size_t i = 0; i < inputs_.size(); ++i) {
        Input& in = inputs_[i];
        if (in.key != key)
            continue;
        focusedInput_ = i;
        in.text = text;
        if (text.empty()) {
            in.locked = false;
            return {};
        }
        if (key == "sides" || key == "count") {
            // A count, not a length.
            const bool sides = key == "sides";
            const long low = sides ? sketch::kMinPolygonSides : 2, high = sides ? sketch::kMaxPolygonSides : sketch::kMaxPatternCount;
            char* rest = nullptr;
            const long n = std::strtol(text.c_str(), &rest, 10);
            if (rest == text.c_str() || *rest != '\0' || n < low || n > high) {
                in.locked = false;
                return std::string(sides ? "The number of sides" : "The count") + " must be a whole number from "
                     + std::to_string(low) + " to " + std::to_string(high) + ".";
            }
            if (sides)
                polygonSides_ = int(n);
            else
                pattern_.count = int(n);
            in.locked = true;
            in.value = double(n);
            updatePatternPreview();
            return {};
        }
        if (key == "angle") {
            // Degrees; a whole turn spaces the copies evenly.
            char* rest = nullptr;
            const double degrees = std::strtod(text.c_str(), &rest);
            while (rest && *rest == ' ')
                ++rest;
            if (rest == text.c_str() || (*rest != '\0' && std::string(rest) != "\xC2\xB0" && std::string(rest) != "deg")
                || !(degrees > 0) || degrees > 360) {
                in.locked = false;
                return "The angle must be more than 0 and at most 360 degrees.";
            }
            in.locked = true;
            in.value = degrees * kPi / 180.0;
            pattern_.angle = degrees >= 360 - 1e-9 ? 2 * kPi : in.value;
            updatePatternPreview();
            return {};
        }
        const auto parsed = parseLength(text, document_.displayUnit());
        if (!parsed.millimeters)
            return parsed.error;
        if (*parsed.millimeters <= 0)
            return in.label + " must be greater than zero.";
        in.locked = true;
        in.value = *parsed.millimeters;
        if (isOffsetting())
            updateOffset(std::nullopt); // show the typed distance
        if (key == "spacing" && pattern_.step.length() > 1e-12) {
            pattern_.step = pattern_.step * (in.value / pattern_.step.length());
            updatePatternPreview();
        }
        return {};
    }
    return "Unknown value.";
}

void SketchSession::focusNextInput()
{
    if (!inputs_.empty())
        focusedInput_ = (focusedInput_ + 1) % inputs_.size();
}

Status SketchSession::commitTool()
{
    if (isPatterning())
        return commitPattern() ? okStatus()
                               : Status::failure(ErrorCode::InvalidArgument, "Unable to repeat these curves.", "commitPattern failed");
    if (isOffsetting())
        return commitOffset() ? okStatus()
                              : Status::failure(ErrorCode::InvalidArgument, "Unable to offset.", "commitOffset failed");
    if (!anchor_)
        return Status::failure(ErrorCode::InvalidArgument, "Nothing is being drawn.", "commitTool without anchor");
    if (!finishShape(cursor_))
        return Status::failure(ErrorCode::InvalidArgument, "Unable to create this shape.", "finishShape failed");
    return okStatus();
}

std::string SketchSession::setDimension(sketch::EntityId constraintId, const std::string& text)
{
    const sketch::SketchConstraint* c = working_.constraint(constraintId);
    if (!c || !c->isDimension())
        return "That dimension no longer exists.";
    if (c->kind == sketch::ConstraintKind::Angle) {
        // Degrees, as shown.
        std::string digits = text;
        for (const std::string_view unit : {std::string_view("\xC2\xB0"), std::string_view("deg")})
            if (const auto at = digits.find(unit); at != std::string::npos)
                digits.erase(at, unit.size());
        char* rest = nullptr;
        const double degrees = std::strtod(digits.c_str(), &rest);
        while (rest && *rest == ' ')
            ++rest;
        if (rest == digits.c_str() || *rest != '\0' || !(degrees > 0) || !(degrees < 180))
            return "Angles must be more than 0 and less than 180 degrees.";
        const auto value = sketch::directionAngleFor(working_, c->a, c->b, degrees * kPi / 180.0);
        if (!value)
            return "These lines are parallel: there is no angle to set.";
        sketch::Sketch next = working_;
        next.constraint(constraintId)->value = *value;
        if (!commit(std::move(next), "Edit angle"))
            return "The sketch cannot take that angle.";
        return {};
    }
    const auto parsed = parseLength(text, document_.displayUnit());
    if (!parsed.millimeters)
        return parsed.error;
    if (*parsed.millimeters <= 0)
        return "Dimensions must be greater than zero.";
    sketch::Sketch next = working_;
    sketch::SketchConstraint* nc = next.constraint(constraintId);
    // Signed distances keep their direction; the user edits the magnitude.
    nc->value = sign(nc->value) * *parsed.millimeters;
    if (!commit(std::move(next), "Edit dimension"))
        return "The sketch cannot take that value.";
    return {};
}

// ---- Selection & actions ------------------------------------------------------------------

void SketchSession::select(sketch::EntityId id, bool additive)
{
    if (id == sketch::kNoEntity) {
        selected_.clear();
        return;
    }
    // Constraints and geometry are never selected together (their actions differ).
    const bool isConstraint = working_.constraint(id) != nullptr;
    if (!selected_.empty() && (working_.constraint(selected_.front()) != nullptr) != isConstraint)
        selected_.clear();
    const auto it = std::find(selected_.begin(), selected_.end(), id);
    if (additive) {
        if (it != selected_.end())
            selected_.erase(it);
        else
            selected_.push_back(id);
    } else {
        selected_ = {id};
    }
}

bool SketchSession::constraintSelected() const
{
    return !selected_.empty() && working_.constraint(selected_.front()) != nullptr;
}

std::vector<ContextAction> SketchSession::contextActions() const
{
    std::vector<ContextAction> actions;
    if (isMirroring()) {
        actions.push_back({"mirror", "Mirror", true}); // click again to cancel
        return actions;
    }
    if (isPatterning()) {
        actions.push_back({"pattern:linear", "Linear", !pattern_.circular});
        actions.push_back({"pattern:circular", "Circular", pattern_.circular});
        actions.push_back({"apply", "Apply", false});
        actions.push_back({"pattern", "Pattern", true}); // click again to cancel
        return actions;
    }
    if (selected_.empty())
        return actions;
    if (constraintSelected()) {
        actions.push_back({"delete", selected_.size() == 1 ? "Delete constraint" : "Delete constraints", false});
        return actions;
    }
    std::size_t points = 0, lines = 0, circles = 0, arcs = 0;
    for (auto id : selected_) {
        points += working_.point(id) ? 1 : 0;
        lines += working_.line(id) ? 1 : 0;
        circles += working_.circle(id) ? 1 : 0;
        arcs += working_.arc(id) ? 1 : 0;
    }
    const std::size_t round = circles + arcs;
    if (lines >= 1 && points == 0 && circles == 0) {
        actions.push_back({"horizontal", "Horizontal", false});
        actions.push_back({"vertical", "Vertical", false});
        if (lines == 1)
            actions.push_back({"length", "Length", false});
    }
    if (circles == 1 && selected_.size() == 1)
        actions.push_back({"diameter", "Diameter", false});
    if (arcs == 1 && selected_.size() == 1)
        actions.push_back({"radius", "Radius", false});
    if (points == 2 && selected_.size() == 2) {
        actions.push_back({"coincident", "Coincident", false});
        // Position one point relative to another (e.g. a hole from the origin).
        actions.push_back({"hdistance", "Horizontal distance", false});
        actions.push_back({"vdistance", "Vertical distance", false});
    }
    if (selected_.size() == 2) {
        if (lines == 2) {
            actions.push_back({"parallel", "Parallel", false});
            actions.push_back({"perpendicular", "Perpendicular", false});
            actions.push_back({"equal", "Equal", false});
            if (sketch::lineIntersection(working_, selected_[0], selected_[1]))
                actions.push_back({"angle", "Angle", false});
        } else if (round == 2) {
            actions.push_back({"equal", "Equal", false});
            actions.push_back({"concentric", "Concentric", false});
            actions.push_back({"tangent", "Tangent", false});
        } else if (lines == 1 && round == 1) {
            actions.push_back({"tangent", "Tangent", false});
        } else if (lines == 1 && points == 1) {
            actions.push_back({"online", "On line", false});
            actions.push_back({"midpoint", "Midpoint", false});
        } else if (round == 1 && points == 1) {
            actions.push_back({"oncircle", "On circle", false});
        }
    }
    // Corners where two lines meet can be rounded.
    if (points == selected_.size()) {
        bool corners = true;
        for (auto id : selected_)
            corners = corners && sketch::suggestedFilletRadius(working_, id).has_value();
        if (corners)
            actions.push_back({"fillet", "Fillet", false});
    }
    if (lines + round > 0 && points == 0) {
        actions.push_back({"offset", "Offset", isOffsetting()});
        actions.push_back({"mirror", "Mirror", false});
        actions.push_back({"pattern", "Pattern", false});
    }
    if (lines + round > 0) {
        // Construction curves guide the drawing but never become profiles.
        bool allConstruction = true;
        for (auto id : selected_) {
            if (const auto* l = working_.line(id))
                allConstruction = allConstruction && l->construction;
            if (const auto* c = working_.circle(id))
                allConstruction = allConstruction && c->construction;
            if (const auto* a = working_.arc(id))
                allConstruction = allConstruction && a->construction;
        }
        actions.push_back({"construction", "Construction", allConstruction});
    }
    const bool onlyOrigin = selected_.size() == 1 && selected_.front() == sketch::kOriginId;
    if (!onlyOrigin)
        actions.push_back({"delete", "Delete", false});
    return actions;
}

Status SketchSession::triggerAction(const std::string& id)
{
    sketch::Sketch next = working_;
    std::string label;
    if (id == "horizontal" || id == "vertical") {
        for (auto e : selected_)
            if (working_.line(e))
                next.addConstraint({id == "horizontal" ? sketch::ConstraintKind::Horizontal : sketch::ConstraintKind::Vertical, e});
        label = id == "horizontal" ? "Horizontal" : "Vertical";
    } else if (id == "length" && selected_.size() == 1 && working_.line(selected_.front())) {
        const auto* l = working_.line(selected_.front());
        const double len = (working_.point(l->end)->position - working_.point(l->start)->position).length();
        next.addConstraint({sketch::ConstraintKind::Distance, l->start, l->end, len});
        label = "Length";
    } else if (id == "diameter" && selected_.size() == 1 && working_.circle(selected_.front())) {
        next.addConstraint({sketch::ConstraintKind::Diameter, selected_.front(), sketch::kNoEntity,
                            working_.circle(selected_.front())->radius * 2});
        label = "Diameter";
    } else if ((id == "hdistance" || id == "vdistance") && selected_.size() == 2 && working_.point(selected_[0])
               && working_.point(selected_[1])) {
        const Vec2 a = working_.point(selected_[0])->position;
        const Vec2 b = working_.point(selected_[1])->position;
        const bool horizontal = id == "hdistance";
        next.addConstraint({horizontal ? sketch::ConstraintKind::HorizontalDistance : sketch::ConstraintKind::VerticalDistance,
                            selected_[0], selected_[1], horizontal ? b.x - a.x : b.y - a.y});
        label = horizontal ? "Horizontal distance" : "Vertical distance";
    } else if (id == "angle" && selected_.size() == 2 && working_.line(selected_[0]) && working_.line(selected_[1])) {
        const auto now = sketch::lineDirectionAngle(working_, selected_[0], selected_[1]);
        if (!now || !sketch::lineIntersection(working_, selected_[0], selected_[1]))
            return Status::failure(ErrorCode::InvalidArgument, "Parallel lines have no angle between them.", "angle: parallel");
        next.addConstraint({sketch::ConstraintKind::Angle, selected_[0], selected_[1], *now});
        label = "Angle";
    } else if (id == "coincident" && selected_.size() == 2) {
        next.addConstraint({sketch::ConstraintKind::Coincident, selected_[0], selected_[1]});
        label = "Coincident";
    } else if ((id == "parallel" || id == "perpendicular" || id == "equal" || id == "concentric" || id == "tangent"
                || id == "online" || id == "midpoint")
               && selected_.size() == 2) {
        // Order the pair the way the constraint expects: line before circle,
        // point before line.
        sketch::EntityId a = selected_[0], b = selected_[1];
        if ((working_.isRound(a) && working_.line(b)) || (working_.line(a) && working_.point(b)))
            std::swap(a, b);
        using K = sketch::ConstraintKind;
        const K kind = id == "parallel" ? K::Parallel : id == "perpendicular" ? K::Perpendicular : id == "equal" ? K::Equal
                     : id == "concentric" ? K::Concentric : id == "tangent" ? K::Tangent : id == "online" ? K::PointOnLine
                                                                                          : K::Midpoint;
        if (next.addConstraint({kind, a, b}) == sketch::kNoEntity)
            return Status::failure(ErrorCode::InvalidArgument, "That constraint does not apply to this selection.",
                                   "sketch constraint rejected: " + id);
        static const std::map<std::string, std::string> labels{
            {"parallel", "Parallel"}, {"perpendicular", "Perpendicular"}, {"equal", "Equal"}, {"concentric", "Concentric"},
            {"tangent", "Tangent"},   {"online", "On line"},             {"midpoint", "Midpoint"}};
        label = labels.at(id);
    } else if (id == "oncircle" && selected_.size() == 2) {
        const sketch::EntityId p = working_.point(selected_[0]) ? selected_[0] : selected_[1];
        const sketch::EntityId round = p == selected_[0] ? selected_[1] : selected_[0];
        if (next.addConstraint({sketch::ConstraintKind::PointOnCircle, p, round}) == sketch::kNoEntity)
            return Status::failure(ErrorCode::InvalidArgument, "That constraint does not apply to this selection.",
                                   "sketch constraint rejected: oncircle");
        label = "On circle";
    } else if (id == "fillet") {
        // A suggested radius for each corner; its R dimension is then editable.
        for (auto corner : selected_) {
            const auto radius = sketch::suggestedFilletRadius(next, corner);
            const auto arc = radius ? sketch::filletCorner(next, corner, *radius)
                                    : Result<sketch::EntityId>::failure(ErrorCode::InvalidArgument,
                                                                        "Select corners where two lines meet.", "fillet");
            if (!arc)
                return Status::failure(ErrorCode::InvalidArgument, arc.userMessage(), arc.developerMessage());
        }
        label = selected_.size() == 1 ? "Fillet" : "Fillets";
    } else if (id == "offset") {
        if (isOffsetting()) {
            cancelOffset();
            return okStatus();
        }
        for (auto e : selected_)
            if (working_.line(e) || working_.isRound(e))
                offsetSource_.push_back(e);
        if (offsetSource_.empty())
            return Status::failure(ErrorCode::InvalidArgument, "Select the curves to offset.", "offset without curves");
        resetShape();
        inputs_ = {{"offset", "D", "", false, 0}};
        focusedInput_ = 0;
        updateOffset(std::nullopt);
        return okStatus();
    } else if (id == "mirror") {
        if (isMirroring()) {
            cancelModes();
            return okStatus();
        }
        cancelOffset();
        for (auto e : selected_)
            if (working_.line(e) || working_.isRound(e))
                mirrorSource_.push_back(e);
        if (mirrorSource_.empty())
            return Status::failure(ErrorCode::InvalidArgument, "Select the curves to mirror.", "mirror without curves");
        resetShape();
        mirrorAxis_ = sketch::kNoEntity;
        return okStatus();
    } else if (id == "pattern" || id == "pattern:linear" || id == "pattern:circular") {
        if (id == "pattern" && isPatterning()) {
            cancelModes();
            return okStatus();
        }
        if (!isPatterning()) {
            cancelOffset();
            for (auto e : selected_)
                if (working_.line(e) || working_.isRound(e))
                    patternSource_.push_back(e);
            if (patternSource_.empty())
                return Status::failure(ErrorCode::InvalidArgument, "Select the curves to repeat.", "pattern without curves");
            resetShape();
        }
        startPattern(id == "pattern:circular");
        return okStatus();
    } else if (id == "apply" && isPatterning()) {
        return commitPattern() ? okStatus()
                               : Status::failure(ErrorCode::InvalidArgument, "Unable to repeat these curves.", "commitPattern failed");
    } else if (id == "radius" && selected_.size() == 1 && working_.arc(selected_.front())) {
        next.addConstraint({sketch::ConstraintKind::Radius, selected_.front(), sketch::kNoEntity,
                            working_.arcRadius(selected_.front())});
        label = "Radius";
    } else if (id == "construction") {
        bool allConstruction = true;
        for (auto e : selected_) {
            if (const auto* l = working_.line(e))
                allConstruction = allConstruction && l->construction;
            if (const auto* c = working_.circle(e))
                allConstruction = allConstruction && c->construction;
            if (const auto* a = working_.arc(e))
                allConstruction = allConstruction && a->construction;
        }
        for (auto e : selected_)
            next.setConstruction(e, !allConstruction);
        label = allConstruction ? "Normal geometry" : "Construction";
    } else if (id == "delete") {
        for (auto e : selected_)
            next.remove(e);
        label = "Delete";
    } else {
        return Status::failure(ErrorCode::InvalidArgument, "Unknown action.", "sketch action " + id);
    }
    if (!commit(std::move(next), label))
        return Status::failure(ErrorCode::InvalidArgument, "That constraint conflicts with the sketch.", "sketch action failed");
    if (id == "delete" || id == "fillet")
        selected_.clear();
    return okStatus();
}

// ---- Mirror and pattern ---------------------------------------------------------------------

void SketchSession::cancelModes()
{
    if (mirrorSource_.empty() && patternSource_.empty())
        return;
    mirrorSource_.clear();
    mirrorAxis_ = sketch::kNoEntity;
    patternSource_.clear();
    previewCurves_.clear();
    if (!anchor_ && !isOffsetting())
        inputs_.clear();
}

void SketchSession::updatePreview(std::function<Result<std::vector<sketch::EntityId>>(sketch::Sketch&)> edit)
{
    preview_ = working_;
    auto made = edit(preview_);
    previewCurves_ = made ? std::move(made.value()) : std::vector<sketch::EntityId>{};
}

bool SketchSession::applyMirror(sketch::EntityId axis)
{
    sketch::Sketch next = working_;
    const auto made = sketch::mirrorCurves(next, mirrorSource_, axis);
    if (!made) {
        message(made.userMessage());
        return false;
    }
    if (!commit(std::move(next), "Mirror"))
        return false;
    cancelModes();
    selected_.clear();
    return true;
}

void SketchSession::startPattern(bool circular)
{
    // The selection's extent: a linear pattern starts one width plus 5 mm
    // apart along X; a circular one turns six times about the origin.
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (const auto id : patternSource_) {
        std::vector<Vec2> pts;
        if (const auto* l = working_.line(id)) {
            pts = {working_.point(l->start)->position, working_.point(l->end)->position};
        } else {
            const Vec2 c = working_.circle(id) ? working_.point(working_.circle(id)->center)->position
                                               : working_.point(working_.arc(id)->center)->position;
            const double r = working_.circle(id) ? working_.circle(id)->radius : working_.arcRadius(id);
            pts = {c - Vec2{r, r}, c + Vec2{r, r}};
        }
        for (const Vec2 p : pts) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    patternOrigin_ = {(minX + maxX) / 2, (minY + maxY) / 2};
    pattern_ = {};
    pattern_.circular = circular;
    pattern_.count = circular ? 6 : 3;
    pattern_.step = {std::ceil(maxX - minX + 5.0 - 1e-9), 0};
    pattern_.center = {0, 0};
    pattern_.angle = 2 * kPi;
    inputs_ = {{circular ? "angle" : "spacing", circular ? "Angle" : "Spacing", "", false, 0}, {"count", "Count", "", false, 0}};
    focusedInput_ = 0;
    updatePatternPreview();
}

void SketchSession::updatePatternPreview()
{
    if (!isPatterning())
        return;
    updatePreview([&](sketch::Sketch& s) { return sketch::patternCurves(s, patternSource_, pattern_); });
}

void SketchSession::patternClick(const Snap& at)
{
    if (pattern_.circular) {
        pattern_.center = at.position; // points (the origin, a hole's center) snap
        updatePatternPreview();
        return;
    }
    // Where the next copy goes: the step from the selection's middle, kept
    // straight across or up when it nearly is.
    Vec2 step = at.position - patternOrigin_;
    if (at.point == sketch::kNoEntity) {
        const double angle = std::atan2(std::abs(step.y), std::abs(step.x)) * 180.0 / kPi;
        if (angle < kInferenceDegrees)
            step.y = 0;
        else if (angle > 90.0 - kInferenceDegrees)
            step.x = 0;
    }
    if (step.length() < 1e-9)
        return;
    if (const auto spacing = input("spacing"))
        step = step * (*spacing / step.length());
    pattern_.step = step;
    updatePatternPreview();
}

bool SketchSession::commitPattern()
{
    sketch::Sketch next = working_;
    const auto made = sketch::patternCurves(next, patternSource_, pattern_);
    if (!made) {
        message(made.userMessage());
        return false;
    }
    if (!commit(std::move(next), "Pattern"))
        return false;
    cancelModes();
    selected_.clear();
    return true;
}

// ---- Offset -------------------------------------------------------------------------------

namespace {

double distanceToCurve(const geom::PlanarCurve& c, Vec2 p)
{
    const Vec2 center{c.center.x, c.center.y};
    if (c.kind == geom::PlanarCurve::Kind::Segment) {
        const Vec2 a{c.start.x, c.start.y}, b{c.end.x, c.end.y}, d = b - a;
        const double t = std::clamp((p - a).dot(d) / std::max(d.dot(d), 1e-18), 0.0, 1.0);
        return (p - (a + d * t)).length();
    }
    return std::abs((p - center).length() - c.radius); // arcs: as full circles (good enough to pick a side)
}

double distanceToCurves(const std::vector<geom::PlanarCurve>& curves, Vec2 p)
{
    double best = std::numeric_limits<double>::max();
    for (const auto& c : curves)
        best = std::min(best, distanceToCurve(c, p));
    return best;
}

} // namespace

void SketchSession::updateOffset(std::optional<Vec2> pointer)
{
    if (pointer)
        offsetPointer_ = pointer;
    offsetPreview_.clear();
    std::vector<geom::PlanarCurve> source;
    for (const auto id : offsetSource_) {
        geom::PlanarCurve c;
        auto at = [&](sketch::EntityId point) {
            const Vec2 p = working_.point(point)->position;
            return Vec3{p.x, p.y, 0};
        };
        if (const auto* l = working_.line(id)) {
            c.kind = geom::PlanarCurve::Kind::Segment;
            c.start = at(l->start);
            c.end = at(l->end);
        } else if (const auto* k = working_.circle(id)) {
            c.kind = geom::PlanarCurve::Kind::Circle;
            c.center = at(k->center);
            c.radius = k->radius;
        } else if (const auto* a = working_.arc(id)) {
            c.kind = geom::PlanarCurve::Kind::Arc;
            c.center = at(a->center);
            c.start = at(a->start);
            c.end = at(a->end);
            c.radius = working_.arcRadius(id);
        } else {
            continue;
        }
        source.push_back(c);
    }
    // Typed distance, else how far the pointer is from the curves.
    const auto typed = input("offset");
    const double distance = typed ? *typed : offsetPointer_ ? distanceToCurves(source, *offsetPointer_) : 1.0;
    if (source.empty() || distance < 1e-6)
        return;
    // The side: whichever result passes nearer the pointer.
    const geom::PlaneFrame frame;
    auto plus = geom::offsetCurves(frame, source, distance);
    auto minus = geom::offsetCurves(frame, source, -distance);
    const bool usePlus = plus && (!minus || !offsetPointer_
                                  || distanceToCurves(plus.value(), *offsetPointer_) <= distanceToCurves(minus.value(), *offsetPointer_));
    if (usePlus) {
        offsetPreview_ = std::move(plus.value());
        offsetDistance_ = distance;
    } else if (minus) {
        offsetPreview_ = std::move(minus.value());
        offsetDistance_ = -distance;
    }
}

bool SketchSession::commitOffset()
{
    if (offsetPreview_.empty()) {
        message("These curves cannot be offset that far. Try a smaller distance, or select one connected chain.");
        return false;
    }
    sketch::Sketch next = working_;
    // Ends shared by neighbouring curves become one point; arcs reuse their
    // original centers (so they stay concentric).
    std::vector<std::pair<Vec2, sketch::EntityId>> made;
    auto pointAt = [&](const Vec3& world, bool reuseExisting) {
        const Vec2 p{world.x, world.y};
        for (const auto& [q, id] : made)
            if ((q - p).length() < 1e-6)
                return id;
        if (reuseExisting)
            for (const auto& [id, point] : next.points())
                if ((point.position - p).length() < 1e-9)
                    return id;
        const sketch::EntityId id = next.addPoint(p);
        made.emplace_back(p, id);
        return id;
    };
    for (const auto& c : offsetPreview_) {
        switch (c.kind) {
        case geom::PlanarCurve::Kind::Segment:
            next.addLine(pointAt(c.start, false), pointAt(c.end, false));
            break;
        case geom::PlanarCurve::Kind::Arc:
            next.addArc(pointAt(c.center, true), pointAt(c.start, false), pointAt(c.end, false));
            break;
        case geom::PlanarCurve::Kind::Circle:
            next.addCircle(pointAt(c.center, true), c.radius);
            break;
        }
    }
    if (!commit(std::move(next), "Offset"))
        return false;
    cancelOffset();
    selected_.clear();
    return true;
}

void SketchSession::cancelOffset()
{
    if (offsetSource_.empty())
        return;
    offsetSource_.clear();
    offsetPreview_.clear();
    offsetPointer_.reset();
    if (!anchor_)
        inputs_.clear();
}

// ---- Presentation -------------------------------------------------------------------------

std::string SketchSession::statusText() const
{
    const auto& report = working_.solveReport();
    if (!report.ok)
        return "Conflicting constraints";
    if (!working_.hasGeometry())
        return "Empty sketch";
    if (report.degreesOfFreedom == 0)
        return "Fully defined";
    if (report.degreesOfFreedom > 0)
        return std::to_string(report.degreesOfFreedom) + (report.degreesOfFreedom == 1 ? " degree" : " degrees") + " of freedom";
    return {};
}

std::string SketchSession::hintText() const
{
    if (isMirroring())
        return "Click the line to mirror across (a construction line works well) \xC2\xB7 Esc cancels";
    if (isPatterning())
        return pattern_.circular
                 ? "Click the center to turn around \xC2\xB7 type the total angle, Tab for the count \xC2\xB7 "
                   "- / + change the count \xC2\xB7 Enter or Apply adds them"
                 : "Click where the next copy goes (or type the spacing), Tab for the count \xC2\xB7 "
                   "- / + change the count \xC2\xB7 Enter or Apply adds them";
    if (isOffsetting())
        return "Move to the side to offset to and click \xC2\xB7 or type a distance and press Enter \xC2\xB7 Esc cancels";
    switch (tool_) {
    case SketchTool::Slot:
        if (!anchor_)
            return "Click the center of one end";
        return arcEnd_ ? "Move to set the width and click, or type a width and press Enter" : "Click the center of the other end";
    case SketchTool::Trim:
        return "Click the piece of a curve to remove (up to where other curves cross it)";
    case SketchTool::Rectangle:
        return anchor_ ? "Click the opposite corner, or type width, Tab, height, Enter" : "Click or drag to draw a rectangle";
    case SketchTool::CenterRectangle:
        return anchor_ ? "Click a corner, or type width, Tab, height, Enter" : "Click the center of the rectangle";
    case SketchTool::Polygon:
        return anchor_ ? "The pointer sets the middle of a side \xC2\xB7 type the size across flats, Tab for the sides "
                         "\xC2\xB7 +/- change the sides"
                       : "Click the center of the polygon \xC2\xB7 +/- change the number of sides";
    case SketchTool::Circle:
        return anchor_ ? "Click to set the size, or type a diameter and press Enter" : "Click the center";
    case SketchTool::Line:
        return anchor_ ? "Click the next point \xC2\xB7 type a length \xC2\xB7 Esc ends the line" : "Click the start point";
    case SketchTool::TangentArc:
        return anchor_ ? "Click where the arc ends, or type a radius and press Enter \xC2\xB7 Esc ends"
                       : "Click the free end of a line or arc to continue it with a tangent arc";
    case SketchTool::Arc:
        if (!anchor_)
            return "Click where the arc starts";
        return arcEnd_ ? "Move to bend the arc and click, or type a radius and press Enter" : "Click where the arc ends";
    case SketchTool::Select:
        break;
    }
    if (constraintSelected())
        return "Delete removes the constraint \xC2\xB7 click elsewhere to keep it";
    if (!selected_.empty())
        return "Add constraints below \xC2\xB7 drag points to adjust \xC2\xB7 Delete removes";
    return "Pick a tool to draw \xC2\xB7 click a dimension to edit it \xC2\xB7 drag points to move them";
}

std::vector<SketchLabel> SketchSession::labels(const Camera& camera) const
{
    std::vector<SketchLabel> out;
    const LengthUnit unit = document_.displayUnit();
    const sketch::Plane& plane = working_.plane();
    auto screen = [&](Vec2 local) { return toScreen(local, camera); };

    // Committed dimensions. Angle labels slide further into their angle
    // (along the bisector) when another label is in the way.
    std::vector<std::pair<std::size_t, Vec2>> angleLabels; // index in `out`, screen step along the bisector
    for (const auto& [id, c] : working_.constraints()) {
        if (!c.isDimension())
            continue;
        SketchLabel label;
        label.kind = SketchLabel::Kind::Dimension;
        label.constraint = id;
        const double pixel = camera.pixelSize(plane.origin);
        const double offset = 22 * pixel;
        switch (c.kind) {
        case sketch::ConstraintKind::HorizontalDistance: {
            const Vec2 a = working_.point(c.a)->position, b = working_.point(c.b)->position;
            label.screen = screen({(a.x + b.x) / 2, std::min(a.y, b.y) - offset});
            label.text = trimmed(std::abs(c.value), unit);
            break;
        }
        case sketch::ConstraintKind::VerticalDistance: {
            const Vec2 a = working_.point(c.a)->position, b = working_.point(c.b)->position;
            label.screen = screen({std::max(a.x, b.x) + offset, (a.y + b.y) / 2});
            label.text = trimmed(std::abs(c.value), unit);
            break;
        }
        case sketch::ConstraintKind::Distance: {
            const Vec2 a = working_.point(c.a)->position, b = working_.point(c.b)->position;
            Vec2 d = b - a;
            const double len = d.length();
            const Vec2 n = len > 1e-9 ? Vec2{-d.y / len, d.x / len} : Vec2{0, 1};
            label.screen = screen((a + b) * 0.5 + n * offset);
            label.text = trimmed(c.value, unit);
            break;
        }
        case sketch::ConstraintKind::Diameter: {
            const auto* circle = working_.circle(c.a);
            const Vec2 center = working_.point(circle->center)->position;
            label.screen = screen(center + Vec2{circle->radius * 0.7071 + offset * 0.7, circle->radius * 0.7071 + offset * 0.7});
            label.text = "\xC3\x98" + trimmed(c.value, unit);
            break;
        }
        case sketch::ConstraintKind::Angle: {
            // At the corner, a little way into the angle.
            const auto corner = sketch::lineIntersection(working_, c.a, c.b);
            const auto visible = sketch::visibleAngle(working_, c.a, c.b, c.value);
            if (!corner || !visible)
                continue;
            auto towardMiddle = [&](sketch::EntityId lineId) {
                const auto* l = working_.line(lineId);
                Vec2 d = (working_.point(l->start)->position + working_.point(l->end)->position) * 0.5 - *corner;
                return d.length() > 1e-12 ? d * (1.0 / d.length()) : Vec2{1, 0};
            };
            Vec2 bisector = towardMiddle(c.a) + towardMiddle(c.b);
            bisector = bisector.length() > 1e-9 ? bisector * (1.0 / bisector.length()) : Vec2{0, 1};
            label.screen = screen(*corner + bisector * (2.2 * offset));
            angleLabels.emplace_back(out.size(), screen(*corner + bisector * (3.2 * offset)) - label.screen);
            char text[32];
            std::snprintf(text, sizeof text, "%.4g\xC2\xB0", *visible * 180.0 / kPi);
            label.text = text;
            break;
        }
        case sketch::ConstraintKind::Radius: {
            const auto* arc = working_.arc(c.a);
            const Vec2 center = working_.point(arc->center)->position;
            const double a0 = angleOf(center, working_.point(arc->start)->position);
            const double mid = a0 + ccw(a0, angleOf(center, working_.point(arc->end)->position)) / 2;
            const double r = working_.arcRadius(c.a) + offset;
            label.screen = screen(center + Vec2{std::cos(mid), std::sin(mid)} * r);
            label.text = "R" + trimmed(c.value, unit);
            break;
        }
        default:
            continue;
        }
        out.push_back(label);
    }
    for (const auto& [index, step] : angleLabels) {
        auto crowded = [&](Vec2 p) {
            for (std::size_t i = 0; i < out.size(); ++i)
                if (i != index && std::abs(out[i].screen.x - p.x) < 40 && std::abs(out[i].screen.y - p.y) < 24)
                    return true;
            return false;
        };
        for (int k = 0; k < 4 && crowded(out[index].screen); ++k)
            out[index].screen = out[index].screen + step;
    }

    // Constraint glyphs (not while a shape is being drawn: they would clutter it).
    if (!anchor_ && !isOffsetting() && !isMirroring() && !isPatterning())
        addConstraintIcons(out, camera);

    // Live inputs of the shape being drawn.
    if (anchor_ && cursorValid_) {
        // A center rectangle spans from the corner opposite the cursor.
        const Vec2 a = tool_ == SketchTool::CenterRectangle ? anchor_->position * 2.0 - constrainedCursor() : anchor_->position;
        const Vec2 c = constrainedCursor();
        for (std::size_t i = 0; i < inputs_.size(); ++i) {
            const Input& in = inputs_[i];
            SketchLabel label;
            label.kind = SketchLabel::Kind::Input;
            label.key = in.key;
            label.focused = i == focusedInput_;
            label.locked = in.locked;
            double measured = 0;
            if (in.key == "width") {
                measured = std::abs(c.x - a.x);
                label.screen = screen({(a.x + c.x) / 2, std::min(a.y, c.y)}) + Vec2{0, 26};
            } else if (in.key == "height") {
                measured = std::abs(c.y - a.y);
                label.screen = screen({std::max(a.x, c.x), (a.y + c.y) / 2}) + Vec2{44, 0};
            } else if (in.key == "diameter") {
                measured = 2 * (c - a).length();
                label.screen = screen(c) + Vec2{40, -18};
            } else if (in.key == "length") {
                measured = (c - a).length();
                label.screen = screen((a + c) * 0.5) + Vec2{0, -26};
            } else if (in.key == "radius") {
                const auto arc = tool_ == SketchTool::TangentArc ? tangentArcShape() : arcShape();
                measured = arc ? arc->radius : 0;
                label.screen = screen(cursor_.position) + Vec2{40, -18};
            } else if (in.key == "slot") {
                const auto slot = slotShape();
                measured = slot ? 2 * slot->radius : 0;
                label.screen = screen(cursor_.position) + Vec2{40, -18};
            } else if (in.key == "size") {
                measured = 2 * (c - a).length();
                label.caption = polygonSides_ % 2 == 0 ? "across flats" : "inner \xC3\x98";
                label.screen = screen(c) + Vec2{56, -18};
            } else if (in.key == "sides") {
                label.caption = "sides";
                label.screen = screen(a) + Vec2{0, 30};
                label.text = in.locked ? in.text : std::to_string(polygonSides_);
                out.push_back(label);
                continue;
            }
            label.text = in.locked ? in.text : trimmed(measured, unit);
            out.push_back(label);
        }
    }
    // The offset distance, beside the pointer.
    if (isOffsetting() && !inputs_.empty()) {
        const Input& in = inputs_.front();
        SketchLabel label;
        label.kind = SketchLabel::Kind::Input;
        label.key = in.key;
        label.focused = true;
        label.locked = in.locked;
        const Vec2 at = offsetPointer_.value_or(Vec2{});
        label.screen = screen(at) + Vec2{40, -18};
        label.text = in.locked ? in.text : trimmed(std::abs(offsetDistance_), unit);
        out.push_back(label);
    }

    // The pattern's typed values: spacing (or angle) and count.
    if (isPatterning()) {
        for (std::size_t i = 0; i < inputs_.size(); ++i) {
            const Input& in = inputs_[i];
            SketchLabel label;
            label.kind = SketchLabel::Kind::Input;
            label.key = in.key;
            label.focused = i == focusedInput_;
            label.locked = in.locked;
            if (in.key == "spacing") {
                label.text = in.locked ? in.text : trimmed(pattern_.step.length(), unit);
                label.caption = "apart";
                label.screen = screen(patternOrigin_ + pattern_.step * 0.5) + Vec2{0, -26};
            } else if (in.key == "angle") {
                char degrees[32];
                std::snprintf(degrees, sizeof degrees, "%g", pattern_.angle * 180.0 / kPi);
                label.text = (in.locked ? in.text : std::string(degrees)) + "\xC2\xB0";
                label.caption = "in all";
                label.screen = screen(pattern_.center) + Vec2{0, -30};
            } else {
                label.text = in.locked ? in.text : std::to_string(pattern_.count);
                label.caption = "in total";
                label.screen = screen(pattern_.circular ? pattern_.center : patternOrigin_) + Vec2{0, 30};
            }
            out.push_back(label);
        }
    }

    // Inference hint next to the cursor.
    if (cursorValid_ && tool_ != SketchTool::Select) {
        std::string hint;
        switch (cursor_.kind) {
        case SnapKind::Origin: hint = "Origin"; break;
        case SnapKind::Point: hint = "Endpoint"; break;
        case SnapKind::Midpoint: hint = "Midpoint"; break;
        default:
            if (cursor_.horizontal)
                hint = "Horizontal";
            else if (cursor_.vertical)
                hint = "Vertical";
        }
        if (!hint.empty()) {
            SketchLabel label;
            label.kind = SketchLabel::Kind::Hint;
            label.text = hint;
            label.screen = screen(cursor_.position) + Vec2{18, 18};
            out.push_back(label);
        }
    }

    // A finger (or pen) hides what is right beside the point it touches, and
    // the hand hides what is below it: there the live values and the
    // inference hint move above the finger (or beside it, near the top).
    if (pointerScreen_ && (largeTargets_ || pointerDevice_ != PointerDevice::Mouse)) {
        std::vector<std::size_t> live;
        std::vector<Vec2> centers;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (out[i].kind == SketchLabel::Kind::Input || out[i].kind == SketchLabel::Kind::Hint) {
                live.push_back(i);
                centers.push_back(out[i].screen);
            }
        keepLabelsClearOfFinger(centers, kLiveLabelSize, *pointerScreen_, camera.viewportSize);
        for (std::size_t k = 0; k < live.size(); ++k)
            out[live[k]].screen = centers[k];
    }
    return out;
}

void SketchSession::addConstraintIcons(std::vector<SketchLabel>& out, const Camera& camera) const
{
    using K = sketch::ConstraintKind;
    auto screen = [&](Vec2 local) { return toScreen(local, camera); };
    auto at = [&](sketch::EntityId point) { return working_.point(point)->position; };
    // Touch: 40 px tap targets (glyphTapHalfSize), so everything sits further apart.
    // A glyph's center stays out of pick reach of the points and curves
    // (pickEntity: 8 px with a mouse, 20 px with a finger), so tapping the
    // glyph itself always reaches it; geometry wins taps nearer to it.
    const double scale = largeTargets_ ? 1.6 : 1.0;
    const double kOffset = 14 * scale, kStep = 18 * scale, kClearance = 19 * scale, kLabelMargin = 12 * scale,
                 kPointClearance = 15 * scale, kCurveClearance = kOffset - 2;

    // Where the shape a line belongs to lies: the far ends of the curves
    // joining it (line glyphs go on the other side, outside the shape).
    auto neighbourhood = [&](const sketch::SketchLine& line) -> std::optional<Vec2> {
        Vec2 sum{0, 0};
        int count = 0;
        for (const auto& [id, other] : working_.lines()) {
            if (&other == &line)
                continue;
            for (const auto end : {line.start, line.end}) {
                if (other.start == end || other.end == end) {
                    sum = sum + screen(at(other.start == end ? other.end : other.start));
                    ++count;
                }
            }
        }
        for (const auto& [id, arc] : working_.arcs())
            for (const auto end : {line.start, line.end})
                if (arc.start == end || arc.end == end) {
                    sum = sum + screen(at(arc.center));
                    ++count;
                }
        if (count == 0)
            return std::nullopt;
        return sum * (1.0 / count);
    };

    struct Icon {
        sketch::EntityId constraint;
        const char* glyph;
        Vec2 position;
        Vec2 step; // where to slide when the spot is taken
    };
    std::vector<Icon> icons;
    std::vector<std::pair<sketch::EntityId, K>> shown; // one glyph per entity and kind (a polygon's equal sides)
    auto once = [&](sketch::EntityId entity, K kind) {
        if (std::find(shown.begin(), shown.end(), std::make_pair(entity, kind)) != shown.end())
            return false;
        shown.emplace_back(entity, kind);
        return true;
    };
    auto onLine = [&](sketch::EntityId cid, const char* glyph, sketch::EntityId lineId, double fraction) {
        const auto* l = working_.line(lineId);
        const Vec2 a = screen(at(l->start)), b = screen(at(l->end)), d = b - a;
        const double length = d.length();
        const Vec2 u = length > 1e-9 ? d * (1.0 / length) : Vec2{1, 0};
        Vec2 n{-u.y, u.x};
        const auto inside = neighbourhood(*l);
        if (inside ? (inside.value() - (a + d * 0.5)).dot(n) > 0 : n.y > 0) // else above (screen y grows down)
            n = n * -1.0;
        icons.push_back({cid, glyph, a + d * fraction + n * kOffset, u * kStep});
    };
    auto onRound = [&](sketch::EntityId cid, const char* glyph, sketch::EntityId roundId) {
        Vec2 center;
        double radius = 0, angle = 3 * kPi / 4;
        if (const auto* c = working_.circle(roundId)) {
            center = at(c->center);
            radius = c->radius;
        } else if (const auto* a = working_.arc(roundId)) {
            center = at(a->center);
            radius = working_.arcRadius(roundId);
            const double a0 = angleOf(center, at(a->start));
            angle = a0 + ccw(a0, angleOf(center, at(a->end))) / 2;
        }
        const Vec2 rim = screen(center + Vec2{std::cos(angle), std::sin(angle)} * radius);
        Vec2 outward = rim - screen(center);
        outward = outward.length() > 1e-9 ? outward * (1.0 / outward.length()) : Vec2{0, -1};
        icons.push_back({cid, glyph, rim + outward * kOffset, Vec2{-outward.y, outward.x} * kStep});
    };
    auto nearPoint = [&](sketch::EntityId cid, const char* glyph, Vec2 local) {
        icons.push_back({cid, glyph, screen(local) + Vec2{kOffset, -kOffset}, Vec2{kStep, 0}});
    };
    auto sharedEnd = [&](sketch::EntityId x, sketch::EntityId y) -> std::optional<sketch::EntityId> {
        auto ends = [&](sketch::EntityId e) -> std::vector<sketch::EntityId> {
            if (const auto* l = working_.line(e))
                return {l->start, l->end};
            if (const auto* a = working_.arc(e))
                return {a->start, a->end};
            return {};
        };
        for (const auto p : ends(x))
            for (const auto q : ends(y))
                if (p == q)
                    return p;
        return std::nullopt;
    };
    auto roundCenter = [&](sketch::EntityId e) {
        if (const auto* c = working_.circle(e))
            return at(c->center);
        return at(working_.arc(e)->center);
    };
    auto roundRadius = [&](sketch::EntityId e) {
        if (const auto* c = working_.circle(e))
            return c->radius;
        return working_.arcRadius(e);
    };

    for (const auto& [cid, c] : working_.constraints()) {
        const char* glyph = constraintGlyph(c.kind);
        if (!glyph)
            continue;
        switch (c.kind) {
        case K::Horizontal:
        case K::Vertical:
            if (once(c.a, c.kind))
                onLine(cid, glyph, c.a, 0.3);
            break;
        case K::Parallel:
        case K::Perpendicular:
        case K::Equal:
            for (const sketch::EntityId e : {c.a, c.b}) {
                if (!once(e, c.kind))
                    continue;
                if (working_.line(e))
                    onLine(cid, glyph, e, 0.7);
                else
                    onRound(cid, glyph, e);
            }
            break;
        case K::Tangent:
            if (const auto p = sharedEnd(c.a, c.b)) {
                nearPoint(cid, glyph, at(*p));
            } else if (const auto* l = working_.line(c.a)) {
                // Where the circle touches the line: the foot of the perpendicular from its center.
                const Vec2 a = at(l->start), d = at(l->end) - a, center = roundCenter(c.b);
                const double t = d.dot(d) > 1e-18 ? (center - a).dot(d) / d.dot(d) : 0.0;
                nearPoint(cid, glyph, a + d * t);
            } else {
                const Vec2 ca = roundCenter(c.a), cb = roundCenter(c.b), d = cb - ca;
                const double len = d.length();
                nearPoint(cid, glyph, len > 1e-9 ? ca + d * (roundRadius(c.a) / len) : ca);
            }
            break;
        case K::Concentric:
            nearPoint(cid, glyph, roundCenter(c.a));
            break;
        case K::Coincident:
        case K::Midpoint:
        case K::PointOnLine:
        case K::PointOnCircle:
            nearPoint(cid, glyph, at(c.a));
            break;
        case K::Symmetric: // on the axis, between the pair
            nearPoint(cid, glyph, (at(c.a) + at(c.b)) * 0.5);
            break;
        case K::Distance:
        case K::HorizontalDistance:
        case K::VerticalDistance:
        case K::Diameter:
        case K::Radius:
        case K::Angle:
            break;
        }
    }

    // Slide each glyph along its line (or sideways) until it is clear of the
    // glyphs placed before it, the dimension labels and the points (which
    // must stay grabbable). A glyph with no clear spot nearby is left out:
    // small geometry would drown in them; zooming in brings them back.
    std::vector<std::pair<Vec2, double>> taken;
    // The dimension labels are pills: about 7.5 px per character plus 16 px
    // of padding wide, 24 px high (SketchOverlay.qml). A glyph (18 px) keeps
    // clear of the whole pill, so neither covers the other.
    struct Box {
        Vec2 center;
        double halfWidth = 0, halfHeight = 0;
    };
    std::vector<Box> labelBoxes;
    for (const auto& label : out)
        labelBoxes.push_back({label.screen, (7.5 * double(utf8Length(label.text)) + 16) / 2, 12});
    for (const auto& [id, p] : working_.points())
        taken.emplace_back(screen(p.position), kPointClearance);
    // Curves too: a glyph's tap target must not cover a curve someone clicks.
    std::vector<std::pair<Vec2, Vec2>> segments;
    std::vector<std::pair<Vec2, double>> rings; // circles and arcs (as full circles: stricter)
    for (const auto& [id, l] : working_.lines())
        segments.emplace_back(screen(at(l.start)), screen(at(l.end)));
    auto ringOf = [&](Vec2 center, double radius) {
        const Vec2 c = screen(center);
        rings.emplace_back(c, (screen(center + Vec2{radius, 0}) - c).length());
    };
    for (const auto& [id, c] : working_.circles())
        ringOf(at(c.center), c.radius);
    for (const auto& [id, a] : working_.arcs())
        ringOf(at(a.center), working_.arcRadius(id));
    auto clear = [&](Vec2 p) {
        if (std::any_of(taken.begin(), taken.end(), [&](const auto& q) { return (p - q.first).length() < q.second; }))
            return false;
        for (const Box& box : labelBoxes)
            if (std::abs(p.x - box.center.x) < box.halfWidth + kLabelMargin
                && std::abs(p.y - box.center.y) < box.halfHeight + kLabelMargin)
                return false;
        for (const auto& [a, b] : segments)
            if (distanceToSegment2D(p, a, b) < kCurveClearance)
                return false;
        for (const auto& [c, r] : rings)
            if (std::abs((p - c).length() - r) < kCurveClearance)
                return false;
        return true;
    };
    for (const Icon& icon : icons) {
        std::optional<Vec2> place;
        for (const int k : {0, 1, -1, 2, -2, 3, -3}) {
            const Vec2 candidate = icon.position + icon.step * double(k);
            if (clear(candidate)) {
                place = candidate;
                break;
            }
        }
        const bool selected = std::find(selected_.begin(), selected_.end(), icon.constraint) != selected_.end();
        if (!place && !selected)
            continue;
        taken.emplace_back(place.value_or(icon.position), kClearance);
        SketchLabel label;
        label.kind = SketchLabel::Kind::Constraint;
        label.constraint = icon.constraint;
        label.text = icon.glyph;
        label.screen = place.value_or(icon.position);
        label.selected = selected;
        label.hot = tool_ == SketchTool::Select && icon.constraint == hoveredGlyph_;
        out.push_back(label);
    }
}

sketch::EntityId SketchSession::glyphAt(Vec2 screen, const Camera& camera) const
{
    const double half = glyphTapHalfSize();
    sketch::EntityId best = sketch::kNoEntity;
    double bestDistance = 1e300;
    for (const auto& label : labels(camera)) {
        if (label.kind != SketchLabel::Kind::Constraint)
            continue;
        const Vec2 d = screen - label.screen;
        if (std::abs(d.x) > half || std::abs(d.y) > half || d.length() >= bestDistance)
            continue;
        bestDistance = d.length();
        best = label.constraint;
    }
    return best;
}

RenderSketch SketchSession::renderData(const Camera& camera) const
{
    RenderSketch out;
    out.editing = true;
    const sketch::Plane& plane = working_.plane();
    const bool defined = working_.solveReport().ok && working_.solveReport().degreesOfFreedom == 0;
    const bool conflict = !working_.solveReport().ok;
    auto isSelected = [&](sketch::EntityId id) { return std::find(selected_.begin(), selected_.end(), id) != selected_.end(); };
    // A selected constraint shows what it holds.
    auto heldBySelected = [&](sketch::EntityId id) {
        for (const auto cid : selected_)
            if (const auto* c = working_.constraint(cid); c && (c->a == id || c->b == id || c->c == id))
                return true;
        return false;
    };
    auto styleOf = [&](sketch::EntityId id, bool construction) {
        if (isSelected(id))
            return SketchStyle::Selected;
        if (heldBySelected(id))
            return SketchStyle::Hovered;
        if (id == hovered_)
            return SketchStyle::Hovered;
        if (construction)
            return SketchStyle::Construction;
        if (conflict)
            return SketchStyle::Conflict;
        return defined ? SketchStyle::Defined : SketchStyle::Normal;
    };
    auto addCircle = [&](Vec2 center, double radius, SketchStyle style) {
        for (int i = 0; i < kCircleSegments; ++i) {
            const double a0 = 2 * kPi * i / kCircleSegments, a1 = 2 * kPi * (i + 1) / kCircleSegments;
            out.lines.push_back({plane.toWorld(center + Vec2{std::cos(a0), std::sin(a0)} * radius),
                                 plane.toWorld(center + Vec2{std::cos(a1), std::sin(a1)} * radius), style});
        }
    };

    for (const auto& [id, l] : working_.lines())
        out.lines.push_back({plane.toWorld(working_.point(l.start)->position), plane.toWorld(working_.point(l.end)->position),
                             styleOf(id, l.construction)});
    for (const auto& [id, c] : working_.circles())
        addCircle(working_.point(c.center)->position, c.radius, styleOf(id, c.construction));
    auto addArc = [&](Vec2 center, double radius, Vec2 from, Vec2 to, SketchStyle style) {
        const double a0 = angleOf(center, from), sweep = ccw(a0, angleOf(center, to));
        const int n = std::max(2, int(std::ceil(sweep / (2 * kPi) * kCircleSegments)));
        for (int i = 0; i < n; ++i) {
            const double t0 = a0 + sweep * i / n, t1 = a0 + sweep * (i + 1) / n;
            out.lines.push_back({plane.toWorld(center + Vec2{std::cos(t0), std::sin(t0)} * radius),
                                 plane.toWorld(center + Vec2{std::cos(t1), std::sin(t1)} * radius), style});
        }
    };
    for (const auto& [id, arc] : working_.arcs())
        addArc(working_.point(arc.center)->position, working_.arcRadius(id), working_.point(arc.start)->position,
               working_.point(arc.end)->position, styleOf(id, arc.construction));
    for (const auto& [id, p] : working_.points())
        out.points.push_back({plane.toWorld(p.position), styleOf(id, false)});

    // Rubber band of the shape being drawn.
    if (anchor_ && cursorValid_) {
        const Vec2 a = anchor_->position;
        const Vec2 c = constrainedCursor();
        switch (tool_) {
        case SketchTool::Rectangle: {
            const Vec2 corners[4] = {a, {c.x, a.y}, c, {a.x, c.y}};
            for (int i = 0; i < 4; ++i)
                out.lines.push_back({plane.toWorld(corners[i]), plane.toWorld(corners[(i + 1) % 4]), SketchStyle::Preview});
            break;
        }
        case SketchTool::CenterRectangle: {
            const Vec2 o = a * 2.0 - c; // the opposite corner
            const Vec2 corners[4] = {o, {c.x, o.y}, c, {o.x, c.y}};
            for (int i = 0; i < 4; ++i)
                out.lines.push_back({plane.toWorld(corners[i]), plane.toWorld(corners[(i + 1) % 4]), SketchStyle::Preview});
            out.lines.push_back({plane.toWorld(o), plane.toWorld(c), SketchStyle::Guide});
            break;
        }
        case SketchTool::Circle:
            addCircle(a, (c - a).length(), SketchStyle::Preview);
            break;
        case SketchTool::Polygon: {
            const auto corners = sketch::polygonCorners(a, c, polygonSides_);
            for (std::size_t i = 0; i < corners.size(); ++i)
                out.lines.push_back({plane.toWorld(corners[i]), plane.toWorld(corners[(i + 1) % corners.size()]),
                                     SketchStyle::Preview});
            if (!corners.empty()) {
                addCircle(a, (c - a).length(), SketchStyle::Guide);
                out.lines.push_back({plane.toWorld(a), plane.toWorld(c), SketchStyle::Guide});
            }
            break;
        }
        case SketchTool::Line:
            out.lines.push_back({plane.toWorld(a), plane.toWorld(c), SketchStyle::Preview});
            break;
        case SketchTool::Arc:
            if (!arcEnd_) {
                out.lines.push_back({plane.toWorld(a), plane.toWorld(c), SketchStyle::Guide}); // the chord so far
            } else if (const auto arc = arcShape()) {
                addArc(arc->center, arc->radius, arc->start, arc->end, SketchStyle::Preview);
                out.points.push_back({plane.toWorld(arcEnd_->position), SketchStyle::Preview});
            } else {
                out.lines.push_back({plane.toWorld(a), plane.toWorld(arcEnd_->position), SketchStyle::Guide});
            }
            break;
        case SketchTool::TangentArc: {
            // The direction it continues in, and the arc.
            const double reach = 60 * camera.pixelSize(plane.toWorld(a));
            out.lines.push_back({plane.toWorld(a), plane.toWorld(a + tangentStart_.direction * reach), SketchStyle::Guide});
            if (const auto arc = tangentArcShape())
                addArc(arc->center, arc->radius, arc->start, arc->end, SketchStyle::Preview);
            break;
        }
        case SketchTool::Slot:
            if (!arcEnd_) {
                out.lines.push_back({plane.toWorld(a), plane.toWorld(c), SketchStyle::Guide}); // the axis so far
            } else if (const auto slot = slotShape()) {
                const Vec2 axis = slot->b - slot->a;
                const Vec2 u = axis * (1.0 / axis.length()), v{-u.y, u.x};
                const double r = slot->radius;
                out.lines.push_back({plane.toWorld(slot->a + v * r), plane.toWorld(slot->b + v * r), SketchStyle::Preview});
                out.lines.push_back({plane.toWorld(slot->b - v * r), plane.toWorld(slot->a - v * r), SketchStyle::Preview});
                addArc(slot->b, r, slot->b - v * r, slot->b + v * r, SketchStyle::Preview);
                addArc(slot->a, r, slot->a + v * r, slot->a - v * r, SketchStyle::Preview);
                out.points.push_back({plane.toWorld(arcEnd_->position), SketchStyle::Preview});
            } else {
                out.lines.push_back({plane.toWorld(a), plane.toWorld(arcEnd_->position), SketchStyle::Guide});
            }
            break;
        case SketchTool::Trim:
        case SketchTool::Select:
            break;
        }
        out.points.push_back({plane.toWorld(a), SketchStyle::Preview});
    }
    // Trim: the piece a click would remove, in red.
    if (tool_ == SketchTool::Trim && hovered_ != sketch::kNoEntity && trimCursor_) {
        const auto piece = sketch::trimPreview(working_, hovered_, *trimCursor_);
        for (std::size_t i = 1; i < piece.size(); ++i)
            out.lines.push_back({plane.toWorld(piece[i - 1]), plane.toWorld(piece[i]), SketchStyle::Conflict});
    }
    // Mirror / pattern: the copies they would add, and where the pattern is anchored.
    if (!previewCurves_.empty() && (isMirroring() || isPatterning())) {
        for (const auto id : previewCurves_) {
            if (const auto* l = preview_.line(id))
                out.lines.push_back({plane.toWorld(preview_.point(l->start)->position),
                                     plane.toWorld(preview_.point(l->end)->position), SketchStyle::Preview});
            else if (const auto* c = preview_.circle(id))
                addCircle(preview_.point(c->center)->position, c->radius, SketchStyle::Preview);
            else if (const auto* a = preview_.arc(id))
                addArc(preview_.point(a->center)->position, preview_.arcRadius(id), preview_.point(a->start)->position,
                       preview_.point(a->end)->position, SketchStyle::Preview);
        }
    }
    if (isPatterning()) {
        if (pattern_.circular)
            out.points.push_back({plane.toWorld(pattern_.center), SketchStyle::Preview});
        else
            out.lines.push_back({plane.toWorld(patternOrigin_), plane.toWorld(patternOrigin_ + pattern_.step), SketchStyle::Guide});
        if (cursorValid_)
            out.points.push_back({plane.toWorld(cursor_.position),
                                  cursor_.point != sketch::kNoEntity ? SketchStyle::Hovered : SketchStyle::Preview});
    }
    // Offset: the curves a click would add.
    for (const auto& curve : offsetPreview_) {
        const Vec2 s{curve.start.x, curve.start.y}, e{curve.end.x, curve.end.y}, center{curve.center.x, curve.center.y};
        if (curve.kind == geom::PlanarCurve::Kind::Segment)
            out.lines.push_back({plane.toWorld(s), plane.toWorld(e), SketchStyle::Preview});
        else if (curve.kind == geom::PlanarCurve::Kind::Arc)
            addArc(center, curve.radius, s, e, SketchStyle::Preview);
        else
            addCircle(center, curve.radius, SketchStyle::Preview);
    }
    // Inference guides and the snapped cursor.
    if (cursorValid_ && tool_ != SketchTool::Select) {
        const double reach = 4000 * camera.pixelSize(plane.origin);
        const Vec2 p = cursor_.position;
        if (cursor_.horizontal && anchor_)
            out.lines.push_back({plane.toWorld(anchor_->position - Vec2{reach, 0}), plane.toWorld(p + Vec2{reach, 0}), SketchStyle::Guide});
        if (cursor_.vertical && anchor_)
            out.lines.push_back({plane.toWorld(anchor_->position - Vec2{0, reach}), plane.toWorld(p + Vec2{0, reach}), SketchStyle::Guide});
        out.points.push_back({plane.toWorld(p), cursor_.point != sketch::kNoEntity ? SketchStyle::Hovered : SketchStyle::Preview});
    }

    for (std::size_t i = 0; i < regionMeshes_.size(); ++i)
        out.regions.push_back({regionMeshes_[i], regionKeys_[i], SketchStyle::Normal});
    return out;
}

} // namespace os::interact
