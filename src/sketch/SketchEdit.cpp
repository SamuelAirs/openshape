// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sketch/SketchEdit.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace os::sketch {

namespace {

constexpr double kEps = 1e-9;
constexpr double kTwoPi = 2 * kPi;

double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
double angleOf(Vec2 v) { return std::atan2(v.y, v.x); }
Vec2 unitAt(double angle) { return {std::cos(angle), std::sin(angle)}; }
// Counterclockwise sweep from one angle to another, in [0, 2 pi).
double ccw(double from, double to)
{
    double d = std::fmod(to - from, kTwoPi);
    return d < 0 ? d + kTwoPi : d;
}

Status failure(const std::string& user, const std::string& developer)
{
    return Status::failure(ErrorCode::InvalidArgument, user, developer);
}

// A curve of the sketch in plain geometry. Lines are parametrized 0..1 from
// start to end, circles and arcs by the counterclockwise angle from `start`.
struct Curve {
    enum class Kind { Line, Circle, Arc } kind = Kind::Line;
    Vec2 a, b; // line ends
    Vec2 center;
    double radius = 0;
    double start = 0, sweep = kTwoPi;
    double end() const { return kind == Kind::Line ? 1.0 : sweep; }
};

std::optional<Curve> curveOf(const Sketch& s, EntityId id)
{
    Curve c;
    if (const auto* l = s.line(id)) {
        c.kind = Curve::Kind::Line;
        c.a = s.point(l->start)->position;
        c.b = s.point(l->end)->position;
        if ((c.b - c.a).length() < kEps)
            return std::nullopt;
        return c;
    }
    if (const auto* k = s.circle(id)) {
        c.kind = Curve::Kind::Circle;
        c.center = s.point(k->center)->position;
        c.radius = k->radius;
        return c;
    }
    if (const auto* arc = s.arc(id)) {
        c.kind = Curve::Kind::Arc;
        c.center = s.point(arc->center)->position;
        const Vec2 from = s.point(arc->start)->position - c.center, to = s.point(arc->end)->position - c.center;
        c.radius = from.length();
        c.start = angleOf(from);
        c.sweep = ccw(c.start, angleOf(to));
        if (c.sweep < kEps)
            c.sweep = kTwoPi;
        return c;
    }
    return std::nullopt;
}

double paramOf(const Curve& c, Vec2 p)
{
    if (c.kind == Curve::Kind::Line) {
        const Vec2 d = c.b - c.a;
        return (p - c.a).dot(d) / d.dot(d);
    }
    return ccw(c.start, angleOf(p - c.center));
}

Vec2 pointAt(const Curve& c, double t)
{
    if (c.kind == Curve::Kind::Line)
        return c.a + (c.b - c.a) * t;
    return c.center + unitAt(c.start + t) * c.radius;
}

bool onExtent(const Curve& c, Vec2 p)
{
    const double tol = 1e-7;
    if (c.kind == Curve::Kind::Line) {
        const double t = paramOf(c, p);
        return t > -tol && t < 1 + tol;
    }
    if (c.kind == Curve::Kind::Circle)
        return true;
    const double t = paramOf(c, p);
    return t < c.sweep + tol || t > kTwoPi - tol; // the start itself reads as ~2 pi
}

// Where the full underlying line/circle geometry of two curves meet.
std::vector<Vec2> meetings(const Curve& x, const Curve& y)
{
    using K = Curve::Kind;
    std::vector<Vec2> out;
    if (x.kind == K::Line && y.kind == K::Line) {
        const Vec2 d1 = x.b - x.a, d2 = y.b - y.a;
        const double den = cross(d1, d2);
        if (std::abs(den) < kEps * d1.length() * d2.length())
            return out; // parallel (overlaps are not crossings)
        out.push_back(x.a + d1 * (cross(y.a - x.a, d2) / den));
        return out;
    }
    if (x.kind != K::Line && y.kind == K::Line)
        return meetings(y, x);
    if (x.kind == K::Line) {
        const Vec2 d = x.b - x.a, f = x.a - y.center;
        const double a = d.dot(d), b = 2 * f.dot(d), c = f.dot(f) - y.radius * y.radius;
        const double disc = b * b - 4 * a * c;
        if (disc < -1e-12 * a * y.radius * y.radius)
            return out;
        const double root = std::sqrt(std::max(disc, 0.0));
        out.push_back(x.a + d * ((-b - root) / (2 * a)));
        if (root > 1e-12)
            out.push_back(x.a + d * ((-b + root) / (2 * a)));
        return out;
    }
    const Vec2 between = y.center - x.center;
    const double d = between.length();
    if (d < kEps || d > x.radius + y.radius + 1e-9 || d < std::abs(x.radius - y.radius) - 1e-9)
        return out;
    const double along = (x.radius * x.radius - y.radius * y.radius + d * d) / (2 * d);
    const double h = std::sqrt(std::max(x.radius * x.radius - along * along, 0.0));
    const Vec2 u = between * (1.0 / d), n{-u.y, u.x};
    const Vec2 m = x.center + u * along;
    out.push_back(m + n * h);
    if (h > 1e-9)
        out.push_back(m - n * h);
    return out;
}

bool usedByGeometry(const Sketch& s, EntityId point)
{
    for (const auto& [id, l] : s.lines())
        if (l.start == point || l.end == point)
            return true;
    for (const auto& [id, k] : s.circles())
        if (k.center == point)
            return true;
    for (const auto& [id, a] : s.arcs())
        if (a.center == point || a.start == point || a.end == point)
            return true;
    return false;
}

void removeIfOrphan(Sketch& s, EntityId point)
{
    if (point != kOriginId && s.point(point) && !usedByGeometry(s, point))
        s.remove(point);
}

// One end of the piece to remove: a crossing with another curve, or one of
// the trimmed curve's own ends (other == kNoEntity).
struct Cut {
    double t = 0;
    EntityId other = kNoEntity;
};

struct TrimPlan {
    Curve curve;
    bool whole = false; // nothing crosses the piece: remove the entire curve
    Cut lo, hi;         // the piece runs counterclockwise / forward from lo to hi
};

std::optional<TrimPlan> planTrim(const Sketch& s, EntityId id, Vec2 at)
{
    const auto target = curveOf(s, id);
    if (!target)
        return std::nullopt;
    std::vector<Cut> cuts;
    auto consider = [&](EntityId other) {
        if (other == id)
            return;
        const auto curve = curveOf(s, other);
        if (!curve)
            return;
        for (const Vec2 p : meetings(*target, *curve))
            if (onExtent(*target, p) && onExtent(*curve, p))
                cuts.push_back({paramOf(*target, p), other});
    };
    for (const auto& [other, l] : s.lines())
        consider(other);
    for (const auto& [other, k] : s.circles())
        consider(other);
    for (const auto& [other, a] : s.arcs())
        consider(other);

    TrimPlan plan;
    plan.curve = *target;
    const double tol = 1e-7;
    if (target->kind == Curve::Kind::Circle) {
        const double tc = paramOf(*target, at);
        const Cut* before = nullptr;
        const Cut* after = nullptr;
        double bestBefore = kTwoPi, bestAfter = kTwoPi;
        for (const auto& cut : cuts) {
            const double a = ccw(tc, cut.t), b = ccw(cut.t, tc);
            if (a > tol && a < bestAfter) {
                bestAfter = a;
                after = &cut;
            }
            if (b > tol && b < bestBefore) {
                bestBefore = b;
                before = &cut;
            }
        }
        if (!before || !after || std::abs(ccw(before->t, after->t)) < tol || std::abs(ccw(before->t, after->t) - kTwoPi) < tol) {
            plan.whole = true;
            return plan;
        }
        plan.lo = *before;
        plan.hi = *after;
        return plan;
    }
    const double end = target->end();
    double tc = paramOf(*target, at);
    if (target->kind == Curve::Kind::Arc && tc > end)
        tc = tc - end < kTwoPi - tc ? end : 0.0; // just past whichever end is nearer
    tc = std::clamp(tc, 0.0, end);
    plan.lo = {0.0, kNoEntity};
    plan.hi = {end, kNoEntity};
    for (auto cut : cuts) {
        if (target->kind == Curve::Kind::Arc && cut.t > end + tol)
            cut.t = 0; // the arc's start seen from just below 2 pi
        if (cut.t <= tol || cut.t >= end - tol)
            continue; // the curve's own ends
        if (cut.t < tc && cut.t > plan.lo.t)
            plan.lo = cut;
        if (cut.t > tc && cut.t < plan.hi.t)
            plan.hi = cut;
    }
    plan.whole = plan.lo.other == kNoEntity && plan.hi.other == kNoEntity;
    return plan;
}

// A point for a new end at `p`: an existing end of the other curve when it
// is right there, else a new point kept on the other curve.
EntityId endPoint(Sketch& s, Vec2 p, EntityId other)
{
    std::vector<EntityId> candidates;
    if (const auto* l = s.line(other))
        candidates = {l->start, l->end};
    else if (const auto* a = s.arc(other))
        candidates = {a->start, a->end};
    for (const EntityId c : candidates)
        if ((s.point(c)->position - p).length() < 1e-7)
            return c;
    const EntityId id = s.addPoint(p);
    if (s.line(other))
        s.addConstraint({ConstraintKind::PointOnLine, id, other});
    else if (s.isRound(other))
        s.addConstraint({ConstraintKind::PointOnCircle, id, other});
    return id;
}

} // namespace

SlotIds addSlot(Sketch& s, Vec2 a, Vec2 b, double radius, EntityId reuseA, EntityId reuseB)
{
    SlotIds ids;
    const Vec2 axis = b - a;
    const double length = axis.length();
    if (length < kEps || !(radius > kEps))
        return ids;
    const Vec2 u = axis * (1.0 / length), v{-u.y, u.x};
    ids.centers[0] = reuseA != kNoEntity ? reuseA : s.addPoint(a);
    ids.centers[1] = reuseB != kNoEntity ? reuseB : s.addPoint(b);
    const EntityId p1 = s.addPoint(a + v * radius), p2 = s.addPoint(b + v * radius);
    const EntityId p3 = s.addPoint(b - v * radius), p4 = s.addPoint(a - v * radius);
    ids.lines[0] = s.addLine(p1, p2);
    ids.lines[1] = s.addLine(p3, p4);
    // Counterclockwise from -v to +v around b passes b + u r (the outer end);
    // from +v to -v around a passes a - u r.
    ids.arcs[1] = s.addArc(ids.centers[1], p3, p2);
    ids.arcs[0] = s.addArc(ids.centers[0], p1, p4);
    for (const EntityId line : ids.lines)
        for (const EntityId arc : ids.arcs)
            s.addConstraint({ConstraintKind::Tangent, line, arc});
    s.addConstraint({ConstraintKind::Equal, ids.arcs[0], ids.arcs[1]});
    return ids;
}

CenterRectangleIds addCenterRectangle(Sketch& s, Vec2 center, Vec2 corner, EntityId reuseCenter)
{
    CenterRectangleIds ids;
    const Vec2 half = corner - center;
    if (std::abs(half.x) < kEps || std::abs(half.y) < kEps)
        return ids;
    ids.rectangle = addRectangle(s, center * 2.0 - corner, corner);
    ids.diagonal = s.addLine(ids.rectangle.corners[0], ids.rectangle.corners[2], true);
    ids.center = reuseCenter != kNoEntity && s.point(reuseCenter) ? reuseCenter : s.addPoint(center);
    s.addConstraint({ConstraintKind::Midpoint, ids.center, ids.diagonal});
    return ids;
}

std::vector<Vec2> polygonCorners(Vec2 center, Vec2 sideMiddle, int sides)
{
    std::vector<Vec2> out;
    const Vec2 toSide = sideMiddle - center;
    const double apothem = toSide.length();
    if (sides < kMinPolygonSides || sides > kMaxPolygonSides || apothem < kEps)
        return out;
    const double half = kPi / sides;
    const double circumradius = apothem / std::cos(half);
    const double first = angleOf(toSide) - half; // the first side runs from here to angleOf(toSide) + half
    for (int i = 0; i < sides; ++i)
        out.push_back(center + unitAt(first + 2 * half * i) * circumradius);
    return out;
}

PolygonIds addPolygon(Sketch& s, Vec2 center, Vec2 sideMiddle, int sides, EntityId reuseCenter)
{
    PolygonIds ids;
    const auto corners = polygonCorners(center, sideMiddle, sides);
    if (corners.empty())
        return ids;
    ids.center = reuseCenter != kNoEntity && s.point(reuseCenter) ? reuseCenter : s.addPoint(center);
    const double apothem = (sideMiddle - center).length();
    ids.outer = s.addCircle(ids.center, (corners[0] - center).length(), true);
    ids.inner = s.addCircle(ids.center, apothem, true);
    for (const Vec2 p : corners)
        ids.corners.push_back(s.addPoint(p));
    for (int i = 0; i < sides; ++i)
        ids.sides.push_back(s.addLine(ids.corners[i], ids.corners[(i + 1) % sides]));
    // Corners on one circle and equal sides make it regular (for any count;
    // equal sides around an inner circle alone would let an even polygon
    // flex, like a rhombus around a circle).
    for (const EntityId corner : ids.corners)
        s.addConstraint({ConstraintKind::PointOnCircle, corner, ids.outer});
    for (int i = 1; i < sides; ++i)
        s.addConstraint({ConstraintKind::Equal, ids.sides[0], ids.sides[i]});
    s.addConstraint({ConstraintKind::Tangent, ids.sides[0], ids.inner});
    return ids;
}

namespace {

// The points a curve is made of.
std::vector<EntityId> curvePoints(const Sketch& s, EntityId curve)
{
    if (const auto* l = s.line(curve))
        return {l->start, l->end};
    if (const auto* c = s.circle(curve))
        return {c->center};
    if (const auto* a = s.arc(curve))
        return {a->center, a->start, a->end};
    return {};
}

bool isConstruction(const Sketch& s, EntityId curve)
{
    if (const auto* l = s.line(curve))
        return l->construction;
    if (const auto* c = s.circle(curve))
        return c->construction;
    if (const auto* a = s.arc(curve))
        return a->construction;
    return false;
}

// The distinct curves of a selection, in order, without `except`.
std::vector<EntityId> curvesOnly(const Sketch& s, const std::vector<EntityId>& ids, EntityId except = kNoEntity)
{
    std::vector<EntityId> out;
    for (const EntityId id : ids)
        if (id != except && (s.line(id) || s.isRound(id)) && std::find(out.begin(), out.end(), id) == out.end())
            out.push_back(id);
    return out;
}

// A copy of `curve` on the mapped points (arcs reversed when mirrored).
EntityId copyCurve(Sketch& s, EntityId curve, const std::map<EntityId, EntityId>& map, bool reversed)
{
    const bool construction = isConstruction(s, curve);
    if (const auto* l = s.line(curve)) {
        const SketchLine line = *l;
        return s.addLine(map.at(line.start), map.at(line.end), construction);
    }
    if (const auto* c = s.circle(curve)) {
        const SketchCircle circle = *c;
        return s.addCircle(map.at(circle.center), circle.radius, construction);
    }
    const SketchArc arc = *s.arc(curve);
    // A mirror image runs the other way round: counterclockwise from the image of the end.
    return reversed ? s.addArc(map.at(arc.center), map.at(arc.end), map.at(arc.start), construction)
                    : s.addArc(map.at(arc.center), map.at(arc.start), map.at(arc.end), construction);
}

} // namespace

Result<std::vector<EntityId>> mirrorCurves(Sketch& s, const std::vector<EntityId>& ids, EntityId axis)
{
    using R = Result<std::vector<EntityId>>;
    const auto* axisLine = s.line(axis);
    if (!axisLine)
        return R::failure(ErrorCode::InvalidArgument, "Mirror across a straight line.", "mirror: axis is not a line");
    const std::vector<EntityId> curves = curvesOnly(s, ids, axis);
    if (curves.empty())
        return R::failure(ErrorCode::InvalidArgument, "Select the curves to mirror first.", "mirror: nothing to mirror");
    const Vec2 a = s.point(axisLine->start)->position, b = s.point(axisLine->end)->position;
    const double axisLength = (b - a).length();
    if (axisLength < kEps)
        return R::failure(ErrorCode::InvalidArgument, "The mirror line has no length.", "mirror: degenerate axis");
    const Vec2 d = (b - a) * (1.0 / axisLength);
    auto mirrored = [&](Vec2 p) {
        const Vec2 v = p - a;
        return a + d * (2 * v.dot(d)) - v;
    };
    auto onAxis = [&](EntityId p) {
        if (p == axisLine->start || p == axisLine->end)
            return true;
        const Vec2 v = s.point(p)->position - a;
        return std::abs(cross(d, v)) < 1e-7;
    };
    const EntityId axisStart = axisLine->start, axisEnd = axisLine->end;
    const bool allOnAxis = std::all_of(curves.begin(), curves.end(), [&](EntityId c) {
        const auto points = curvePoints(s, c);
        return std::all_of(points.begin(), points.end(), onAxis);
    });
    if (allOnAxis)
        return R::failure(ErrorCode::InvalidArgument, "These curves lie on the mirror line.", "mirror: all on the axis");

    // Which points are only arc centers: their image follows from the arc (an
    // Equal radius), a Symmetric pair there would say the same thing twice.
    std::map<EntityId, bool> onlyArcCenter;
    for (const EntityId c : curves) {
        const auto points = curvePoints(s, c);
        for (std::size_t i = 0; i < points.size(); ++i) {
            const bool arcCenter = s.arc(c) && i == 0;
            auto [it, inserted] = onlyArcCenter.try_emplace(points[i], arcCenter);
            if (!inserted)
                it->second = it->second && arcCenter;
        }
    }

    std::map<EntityId, EntityId> image;
    for (const auto& [p, arcCenterOnly] : onlyArcCenter) {
        if (onAxis(p)) {
            image[p] = p; // shared by both halves; it must stay on the axis
            bool held = p == axisStart || p == axisEnd;
            for (const EntityId cid : s.constraintsOn(p)) {
                const auto* k = s.constraint(cid);
                held = held || (k->kind == ConstraintKind::PointOnLine && k->a == p && k->b == axis);
            }
            if (!held)
                s.addConstraint({ConstraintKind::PointOnLine, p, axis});
            continue;
        }
        image[p] = s.addPoint(mirrored(s.point(p)->position));
        if (!arcCenterOnly)
            s.addConstraint({ConstraintKind::Symmetric, p, image[p], 0.0, axis});
    }

    std::vector<EntityId> made;
    std::map<EntityId, bool> centerHeld; // an arc-only center whose image an earlier arc already fixes
    for (const EntityId c : curves) {
        const auto points = curvePoints(s, c);
        if (std::all_of(points.begin(), points.end(), [&](EntityId p) { return image.at(p) == p; }))
            continue; // lies on the axis: its image is itself
        const EntityId copy = copyCurve(s, c, image, true);
        if (copy == kNoEntity)
            continue;
        made.push_back(copy);
        if (s.circle(c)) {
            s.addConstraint({ConstraintKind::Equal, c, copy}); // the center is mirrored, the size is equal
        } else if (s.arc(c)) {
            const EntityId center = s.arc(c)->center;
            if (onlyArcCenter.at(center) && image.at(center) != center && !centerHeld[center]) {
                s.addConstraint({ConstraintKind::Equal, c, copy}); // with the mirrored ends, this fixes the center
                centerHeld[center] = true;
            }
        }
    }
    if (made.empty())
        return R::failure(ErrorCode::InvalidArgument, "These curves lie on the mirror line.", "mirror: all on the axis");
    return R::success(std::move(made));
}

Vec2 Motion2D::apply(Vec2 p) const
{
    const Vec2 v = p - center;
    const double c = std::cos(angle), s = std::sin(angle);
    return center + Vec2{c * v.x - s * v.y, s * v.x + c * v.y} + offset;
}

std::vector<Motion2D> patternMotions(const PatternLayout& layout)
{
    std::vector<Motion2D> out;
    const int count = std::clamp(layout.count, 1, kMaxPatternCount);
    const bool fullTurn = std::abs(layout.angle - 2 * kPi) < 1e-9;
    const double step = fullTurn ? layout.angle / count : layout.angle / std::max(count - 1, 1);
    for (int k = 1; k < count; ++k) {
        Motion2D m;
        if (layout.circular) {
            m.center = layout.center;
            m.angle = step * k;
        } else {
            m.offset = layout.step * double(k);
        }
        out.push_back(m);
    }
    return out;
}

Result<std::vector<EntityId>> patternCurves(Sketch& s, const std::vector<EntityId>& ids, const PatternLayout& layout)
{
    using R = Result<std::vector<EntityId>>;
    const std::vector<EntityId> curves = curvesOnly(s, ids);
    if (curves.empty())
        return R::failure(ErrorCode::InvalidArgument, "Select the curves to repeat first.", "pattern: nothing selected");
    if (layout.count < 2 || layout.count > kMaxPatternCount)
        return R::failure(ErrorCode::InvalidArgument,
                          "Use between 2 and " + std::to_string(kMaxPatternCount) + " items.", "pattern: count");
    if (!layout.circular && layout.step.length() < kEps)
        return R::failure(ErrorCode::InvalidArgument, "The spacing must be greater than zero.", "pattern: zero step");
    if (layout.circular && !(layout.angle > kEps && layout.angle <= 2 * kPi + 1e-9))
        return R::failure(ErrorCode::InvalidArgument, "The angle must be more than 0 and at most 360 degrees.",
                          "pattern: angle");

    // Everything the selection is made of, for sharing points and copying
    // the constraints among them.
    std::vector<EntityId> points;
    for (const EntityId c : curves)
        for (const EntityId p : curvePoints(s, c))
            if (std::find(points.begin(), points.end(), p) == points.end())
                points.push_back(p);
    auto inSelection = [&](EntityId e) {
        return e == kNoEntity || std::find(curves.begin(), curves.end(), e) != curves.end()
            || std::find(points.begin(), points.end(), e) != points.end();
    };
    std::vector<SketchConstraint> internal;
    for (const auto& [cid, k] : s.constraints()) {
        using K = ConstraintKind;
        if (k.isDimension() || !inSelection(k.a) || !inSelection(k.b) || !inSelection(k.c))
            continue;
        if ((k.kind == K::Horizontal || k.kind == K::Vertical) && layout.circular)
            continue; // turned copies are neither
        if (k.kind == K::Equal && s.isRound(k.a))
            continue; // round sizes follow the originals through their own Equal
        internal.push_back(k);
    }

    std::vector<std::pair<Vec2, EntityId>> shared; // points a copy may land on
    for (const EntityId p : points)
        shared.emplace_back(s.point(p)->position, p);
    std::vector<EntityId> made;
    for (const Motion2D& motion : patternMotions(layout)) {
        std::map<EntityId, EntityId> map;
        for (const EntityId p : points) {
            const Vec2 q = motion.apply(s.point(p)->position);
            EntityId id = kNoEntity;
            for (const auto& [where, existing] : shared)
                if ((where - q).length() < 1e-7)
                    id = existing;
            if (id == kNoEntity) {
                id = s.addPoint(q);
                shared.emplace_back(q, id);
            }
            map[p] = id;
        }
        for (const EntityId c : curves) {
            const EntityId copy = copyCurve(s, c, map, false);
            if (copy == kNoEntity)
                continue;
            map[c] = copy;
            made.push_back(copy);
            if (s.isRound(c))
                s.addConstraint({ConstraintKind::Equal, c, copy});
        }
        for (const SketchConstraint& k : internal) {
            auto remap = [&](EntityId e) {
                const auto it = map.find(e);
                return it == map.end() ? e : it->second;
            };
            const SketchConstraint copy{k.kind, remap(k.a), remap(k.b), k.value, remap(k.c)};
            if (copy.a == k.a && copy.b == k.b && copy.c == k.c)
                continue; // everything it holds is shared: it is there already
            s.addConstraint(copy);
        }
    }
    return R::success(std::move(made));
}

Result<EntityId> filletCorner(Sketch& s, EntityId corner, double radius)
{
    std::vector<EntityId> lines;
    for (const auto& [id, l] : s.lines())
        if (l.start == corner || l.end == corner)
            lines.push_back(id);
    for (const auto& [id, a] : s.arcs())
        if (a.start == corner || a.end == corner)
            return Result<EntityId>::failure(ErrorCode::InvalidArgument, "Only corners between two lines can be rounded.",
                                             "fillet: corner on an arc");
    if (lines.size() != 2 || !s.point(corner))
        return Result<EntityId>::failure(ErrorCode::InvalidArgument, "Select a corner where exactly two lines meet.",
                                         "fillet: " + std::to_string(lines.size()) + " lines at the point");
    if (!(radius > kEps))
        return Result<EntityId>::failure(ErrorCode::InvalidArgument, "The radius must be greater than zero.", "fillet: radius");
    const Vec2 p = s.point(corner)->position;
    auto otherEnd = [&](EntityId line) {
        const auto* l = s.line(line);
        return s.point(l->start == corner ? l->end : l->start)->position;
    };
    const Vec2 toA = otherEnd(lines[0]) - p, toB = otherEnd(lines[1]) - p;
    const double lengthA = toA.length(), lengthB = toB.length();
    const Vec2 d1 = toA * (1.0 / lengthA), d2 = toB * (1.0 / lengthB);
    if (std::abs(cross(d1, d2)) < 1e-9)
        return Result<EntityId>::failure(ErrorCode::InvalidArgument, "These lines are in line: there is no corner to round.",
                                         "fillet: collinear");
    const double half = std::acos(std::clamp(d1.dot(d2), -1.0, 1.0)) / 2;
    const double back = radius / std::tan(half); // from the corner to each tangent point
    if (back >= lengthA - 1e-9 || back >= lengthB - 1e-9)
        return Result<EntityId>::failure(ErrorCode::InvalidArgument, "That radius is too large for these lines.",
                                         "fillet: tangent point beyond a line end");
    const Vec2 t1 = p + d1 * back, t2 = p + d2 * back;
    Vec2 bisector = d1 + d2;
    bisector = bisector * (1.0 / bisector.length());
    const Vec2 center = p + bisector * (radius / std::sin(half));

    const EntityId t1Id = s.addPoint(t1), t2Id = s.addPoint(t2), centerId = s.addPoint(center);
    for (int i = 0; i < 2; ++i) {
        SketchLine* l = s.line(lines[i]);
        (l->start == corner ? l->start : l->end) = i == 0 ? t1Id : t2Id;
    }
    // The short way round is counterclockwise from one tangent point to the other.
    const bool fromT1 = cross(t1 - center, t2 - center) > 0;
    const EntityId arc = s.addArc(centerId, fromT1 ? t1Id : t2Id, fromT1 ? t2Id : t1Id);
    for (const EntityId line : lines) {
        s.addConstraint({ConstraintKind::Tangent, line, arc});
        s.addConstraint({ConstraintKind::PointOnLine, corner, line}); // the virtual sharp
    }
    s.addConstraint({ConstraintKind::Radius, arc, kNoEntity, radius});
    return Result<EntityId>::success(arc);
}

std::optional<double> suggestedFilletRadius(const Sketch& s, EntityId corner)
{
    std::vector<Vec2> ends;
    for (const auto& [id, l] : s.lines())
        if (l.start == corner || l.end == corner)
            ends.push_back(s.point(l.start == corner ? l.end : l.start)->position);
    if (ends.size() != 2 || !s.point(corner))
        return std::nullopt;
    const Vec2 p = s.point(corner)->position;
    const Vec2 toA = ends[0] - p, toB = ends[1] - p;
    const double shorter = std::min(toA.length(), toB.length());
    const double cosine = std::clamp(toA.dot(toB) / (toA.length() * toB.length()), -1.0, 1.0);
    const double half = std::acos(cosine) / 2;
    if (shorter < kEps || half < 1e-6 || half > kPi / 2 - 1e-6)
        return std::nullopt;
    // A quarter of the shorter line, but the tangent points must stay on the lines.
    const double wanted = std::min(shorter * 0.25, 0.8 * shorter * std::tan(half));
    // Round down to 1, 2 or 5 times a power of ten.
    const double decade = std::pow(10.0, std::floor(std::log10(wanted)));
    for (const double step : {5.0, 2.0, 1.0})
        if (step * decade <= wanted + 1e-12)
            return step * decade;
    return wanted;
}

Status trimAt(Sketch& s, EntityId id, Vec2 at)
{
    const auto plan = planTrim(s, id, at);
    if (!plan)
        return failure("Click a line, circle or arc to trim it.", "trim: not a curve");
    const Curve& c = plan->curve;
    if (plan->whole) {
        std::vector<EntityId> ends;
        if (const auto* l = s.line(id))
            ends = {l->start, l->end};
        else if (const auto* k = s.circle(id))
            ends = {k->center};
        else if (const auto* a = s.arc(id))
            ends = {a->start, a->end, a->center};
        s.remove(id);
        for (const EntityId e : ends)
            removeIfOrphan(s, e);
        return okStatus();
    }

    if (c.kind == Curve::Kind::Circle) {
        // The rest of the circle becomes an arc from hi round to lo.
        const SketchCircle circle = *s.circle(id);
        const EntityId from = endPoint(s, pointAt(c, plan->hi.t), plan->hi.other);
        const EntityId to = endPoint(s, pointAt(c, plan->lo.t), plan->lo.other);
        const EntityId arc = s.addArc(circle.center, from, to, circle.construction);
        if (arc == kNoEntity)
            return failure("Unable to trim this circle.", "trim: arc rejected");
        for (const EntityId cid : s.constraintsOn(id)) {
            SketchConstraint copy = *s.constraint(cid);
            if (copy.kind == ConstraintKind::Diameter) {
                copy = {ConstraintKind::Radius, arc, kNoEntity, copy.value / 2};
            } else {
                copy.a = copy.a == id ? arc : copy.a;
                copy.b = copy.b == id ? arc : copy.b;
            }
            s.addConstraint(copy); // dropped when it no longer applies
        }
        s.remove(id);
        return okStatus();
    }

    const bool fromStart = plan->lo.other == kNoEntity; // the piece starts at the curve's start
    const bool toEnd = plan->hi.other == kNoEntity;
    if (c.kind == Curve::Kind::Line) {
        SketchLine line = *s.line(id);
        if (fromStart) {
            const EntityId q = endPoint(s, pointAt(c, plan->hi.t), plan->hi.other);
            s.line(id)->start = q;
            removeIfOrphan(s, line.start);
        } else if (toEnd) {
            const EntityId q = endPoint(s, pointAt(c, plan->lo.t), plan->lo.other);
            s.line(id)->end = q;
            removeIfOrphan(s, line.end);
        } else {
            const EntityId q1 = endPoint(s, pointAt(c, plan->lo.t), plan->lo.other);
            const EntityId q2 = endPoint(s, pointAt(c, plan->hi.t), plan->hi.other);
            s.line(id)->end = q1;
            const EntityId rest = s.addLine(q2, line.end, line.construction);
            for (const EntityId cid : s.constraintsOn(id)) {
                const SketchConstraint k = *s.constraint(cid);
                if (k.kind == ConstraintKind::Horizontal || k.kind == ConstraintKind::Vertical)
                    s.addConstraint({k.kind, rest});
            }
        }
        return okStatus();
    }

    // Arc.
    SketchArc arc = *s.arc(id);
    if (fromStart) {
        const EntityId q = endPoint(s, pointAt(c, plan->hi.t), plan->hi.other);
        s.arc(id)->start = q;
        removeIfOrphan(s, arc.start);
    } else if (toEnd) {
        const EntityId q = endPoint(s, pointAt(c, plan->lo.t), plan->lo.other);
        s.arc(id)->end = q;
        removeIfOrphan(s, arc.end);
    } else {
        const EntityId q1 = endPoint(s, pointAt(c, plan->lo.t), plan->lo.other);
        const EntityId q2 = endPoint(s, pointAt(c, plan->hi.t), plan->hi.other);
        s.arc(id)->end = q1;
        const EntityId rest = s.addArc(arc.center, q2, arc.end, arc.construction);
        s.addConstraint({ConstraintKind::Equal, rest, id}); // the same circle
    }
    return okStatus();
}

std::vector<Vec2> trimPreview(const Sketch& s, EntityId id, Vec2 at)
{
    std::vector<Vec2> out;
    const auto plan = planTrim(s, id, at);
    if (!plan)
        return out;
    const Curve& c = plan->curve;
    double lo = plan->lo.t, hi = plan->hi.t;
    if (plan->whole) {
        lo = 0;
        hi = c.end();
    }
    if (c.kind == Curve::Kind::Line) {
        out = {pointAt(c, lo), pointAt(c, hi)};
        return out;
    }
    double sweep = c.kind == Curve::Kind::Circle && !plan->whole ? ccw(lo, hi) : hi - lo;
    if (sweep <= 0)
        sweep += kTwoPi;
    const int n = std::max(2, int(std::ceil(sweep / (kPi / 36))));
    for (int i = 0; i <= n; ++i)
        out.push_back(pointAt(c, lo + sweep * i / n));
    return out;
}

} // namespace os::sketch
