// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "sketch/SketchEdit.h"

#include <algorithm>
#include <cmath>

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
