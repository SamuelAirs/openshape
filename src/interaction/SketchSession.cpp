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
#include "sketch/SketchEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

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
    trimCursor_.reset();
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
    if (anchor_ && tool_ == SketchTool::Line) {
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
    // 4. Round free coordinates to a zoom-dependent grid.
    if (!snap.horizontal)
        p.y = snapValue(p.y, step);
    if (!snap.vertical)
        p.x = snapValue(p.x, step);
    if (snap.horizontal)
        p.x = snapValue(p.x, step);
    if (snap.vertical)
        p.y = snapValue(p.y, step);
    snap.position = p;
    snap.kind = SnapKind::Grid;
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
    case SketchTool::Arc:
    case SketchTool::Slot:
    case SketchTool::Trim:
    case SketchTool::Select:
        break;
    }
    return c;
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
    if (event.button != PointerButton::Left)
        return false;
    pressed_ = true;
    dragging_ = false;
    pressScreen_ = event.position;
    pressEvent_ = event;
    dragPoint_ = sketch::kNoEntity;

    if (isOffsetting())
        return true; // the release applies the offset
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
    if (!anchor_)
        beginShape(snap);
    return true;
}

void SketchSession::pointerMove(const PointerEvent& event, const Camera& camera)
{
    if (isOffsetting()) {
        updateOffset(toLocal(event.position, camera));
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
    if (!pressed_)
        return;
    pressed_ = false;

    if (isOffsetting()) {
        updateOffset(toLocal(event.position, camera));
        (void)commitOffset();
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
            const sketch::EntityId hit = pickEntity(event.position, camera, event.device);
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
    if (isOffsetting()) {
        updateOffset(toLocal(event.position, camera));
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
        cursorValid_ = false;
        return;
    }
    cursor_ = snapAt(event.position, camera, event.device);
    cursorValid_ = true;
}

void SketchSession::leave()
{
    hovered_ = sketch::kNoEntity;
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
        if (anchor_ || isOffsetting()) {
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
        const auto parsed = parseLength(text, document_.displayUnit());
        if (!parsed.millimeters)
            return parsed.error;
        if (*parsed.millimeters <= 0)
            return in.label + " must be greater than zero.";
        in.locked = true;
        in.value = *parsed.millimeters;
        if (isOffsetting())
            updateOffset(std::nullopt); // show the typed distance
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

std::vector<ContextAction> SketchSession::contextActions() const
{
    std::vector<ContextAction> actions;
    if (selected_.empty())
        return actions;
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
    if (lines + round > 0 && points == 0)
        actions.push_back({"offset", "Offset", isOffsetting()});
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
    case SketchTool::Circle:
        return anchor_ ? "Click to set the size, or type a diameter and press Enter" : "Click the center";
    case SketchTool::Line:
        return anchor_ ? "Click the next point \xC2\xB7 type a length \xC2\xB7 Esc ends the line" : "Click the start point";
    case SketchTool::Arc:
        if (!anchor_)
            return "Click where the arc starts";
        return arcEnd_ ? "Move to bend the arc and click, or type a radius and press Enter" : "Click where the arc ends";
    case SketchTool::Select:
        break;
    }
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

    // Committed dimensions.
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
                const auto arc = arcShape();
                measured = arc ? arc->radius : 0;
                label.screen = screen(cursor_.position) + Vec2{40, -18};
            } else if (in.key == "slot") {
                const auto slot = slotShape();
                measured = slot ? 2 * slot->radius : 0;
                label.screen = screen(cursor_.position) + Vec2{40, -18};
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
    return out;
}

RenderSketch SketchSession::renderData(const Camera& camera) const
{
    RenderSketch out;
    out.editing = true;
    const sketch::Plane& plane = working_.plane();
    const bool defined = working_.solveReport().ok && working_.solveReport().degreesOfFreedom == 0;
    const bool conflict = !working_.solveReport().ok;
    auto isSelected = [&](sketch::EntityId id) { return std::find(selected_.begin(), selected_.end(), id) != selected_.end(); };
    auto styleOf = [&](sketch::EntityId id, bool construction) {
        if (isSelected(id))
            return SketchStyle::Selected;
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
