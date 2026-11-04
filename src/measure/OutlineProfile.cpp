// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/OutlineProfile.h"

#include <algorithm>
#include <cmath>

namespace os::measure {

namespace {

double wrapAngle(double a)
{
    while (a < 0)
        a += 2 * kPi;
    while (a >= 2 * kPi)
        a -= 2 * kPi;
    return a;
}

// The signed sweep of the arc from a through m to b about c.
double sweepOf(Vec2 c, Vec2 a, Vec2 m, Vec2 b)
{
    const double a0 = std::atan2(a.y - c.y, a.x - c.x);
    const double am = std::atan2(m.y - c.y, m.x - c.x);
    const double a1 = std::atan2(b.y - c.y, b.x - c.x);
    const double toEnd = wrapAngle(a1 - a0), toMid = wrapAngle(am - a0);
    return toMid <= toEnd ? toEnd : toEnd - 2 * kPi;
}

double sweepOf(const FittedSegment& s) { return sweepOf(s.center, s.start, s.through, s.end); }

Vec2 rotate(Vec2 p, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    return {c * p.x - s * p.y, s * p.x + c * p.y};
}

// A piece as points: its start, and an arc's points every `step` radians.
void appendPiece(std::vector<Vec2>& out, const FittedSegment& s, double step)
{
    out.push_back(s.start);
    if (!s.arc)
        return;
    const double sweep = sweepOf(s);
    const int n = std::max(2, int(std::ceil(std::abs(sweep) / step)));
    const double a0 = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x);
    for (int i = 1; i < n; ++i) {
        const double a = a0 + sweep * i / n;
        out.push_back(s.center + Vec2{std::cos(a), std::sin(a)} * s.radius);
    }
}

} // namespace

ProfileLoop profileLoopOf(const OutlineLoop& loop)
{
    ProfileLoop p;
    if (loop.circle) {
        const Circle& c = loop.circleShape;
        for (int i = 0; i < 4; ++i) {
            const double a = -kPi / 2 + i * kPi / 2;
            p.points.push_back(c.center + Vec2{std::cos(a), std::sin(a)} * c.radius);
            p.through.push_back(i % 2 == 1);
        }
        return p;
    }
    for (const FittedSegment& s : loop.segments) {
        p.points.push_back(s.start);
        p.through.push_back(false);
        if (s.arc) {
            p.points.push_back(s.through);
            p.through.push_back(true);
        }
    }
    return p;
}

std::vector<FittedSegment> profileSegments(const ProfileLoop& loop)
{
    std::vector<FittedSegment> out;
    const std::size_t n = loop.points.size();
    if (n < 2 || loop.through.size() != n)
        return out;
    // Start at a corner (a point that is no arc's middle).
    std::size_t first = 0;
    while (first < n && loop.through[first])
        ++first;
    if (first == n)
        return out;
    std::size_t i = first;
    do {
        const std::size_t j = (i + 1) % n;
        FittedSegment s;
        s.start = loop.points[i];
        if (loop.through[j] && !loop.through[(j + 1) % n]) {
            const std::size_t k = (j + 1) % n;
            s.end = loop.points[k];
            s.through = loop.points[j];
            if (const auto c = circleThrough(s.start, s.through, s.end)) {
                s.arc = true;
                s.center = c->center;
                s.radius = c->radius;
                out.push_back(s);
            } else {
                // In a line: two lines.
                FittedSegment a, b;
                a.start = s.start;
                a.end = a.through = s.through;
                b.start = s.through;
                b.end = b.through = s.end;
                a.through = (a.start + a.end) * 0.5;
                b.through = (b.start + b.end) * 0.5;
                out.push_back(a);
                out.push_back(b);
            }
            i = k;
        } else {
            s.end = loop.points[j];
            s.through = (s.start + s.end) * 0.5;
            out.push_back(s);
            i = j;
        }
    } while (i != first);
    return out;
}

bool profileIsCircle(const ProfileLoop& loop, double tolerance, Circle* circle)
{
    const auto segments = profileSegments(loop);
    if (segments.size() < 2)
        return false;
    double total = 0;
    for (const auto& s : segments) {
        if (!s.arc)
            return false;
        if ((s.center - segments.front().center).length() > tolerance || std::abs(s.radius - segments.front().radius) > tolerance)
            return false;
        total += sweepOf(s);
    }
    if (std::abs(std::abs(total) - 2 * kPi) > 1e-3)
        return false;
    if (circle) {
        Vec2 c;
        double r = 0;
        for (const auto& s : segments) {
            c = c + s.center;
            r += s.radius;
        }
        circle->center = c * (1.0 / double(segments.size()));
        circle->radius = r / double(segments.size());
    }
    return true;
}

std::vector<Vec2> profilePolyline(const ProfileLoop& loop, double degreesPerStep)
{
    std::vector<Vec2> out;
    const double step = std::max(0.1, degreesPerStep) * kPi / 180;
    for (const auto& s : profileSegments(loop))
        appendPiece(out, s, step);
    return out;
}

double profileSignedArea(const ProfileLoop& loop)
{
    double twice = 0;
    for (const auto& s : profileSegments(loop)) {
        if (!s.arc) {
            twice += s.start.x * s.end.y - s.end.x * s.start.y;
            continue;
        }
        // The integral of x dy - y dx along the arc.
        const double phi = sweepOf(s);
        const double a0 = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x), a1 = a0 + phi;
        const double r = s.radius, cx = s.center.x, cy = s.center.y;
        twice += r * cx * (std::sin(a1) - std::sin(a0)) - r * cy * (std::cos(a1) - std::cos(a0)) + r * r * phi;
    }
    return twice * 0.5;
}

double profileAlignment(const ProfileLoop& loop)
{
    double best = 0, angle = 0;
    for (const auto& s : profileSegments(loop)) {
        if (s.arc)
            continue;
        const Vec2 d = s.end - s.start;
        if (d.length() > best) {
            best = d.length();
            angle = -std::atan2(d.y, d.x);
        }
    }
    // The smallest turn that lays it along x (either way along).
    while (angle > kPi / 2)
        angle -= kPi;
    while (angle <= -kPi / 2)
        angle += kPi;
    return angle;
}

ProfileReport describeProfile(const ProfileLoop& outer, const std::vector<ProfileLoop>& holes, double scale, double circleTolerance)
{
    ProfileReport r;
    const auto segments = profileSegments(outer);
    for (const auto& s : segments) {
        ProfileReport::Side side;
        if (s.arc) {
            side.arc = true;
            side.radius = s.radius * scale;
            side.length = std::abs(sweepOf(s)) * s.radius * scale;
            side.middle = s.through;
        } else {
            side.length = (s.end - s.start).length() * scale;
            side.middle = (s.start + s.end) * 0.5;
        }
        r.sides.push_back(side);
        r.perimeter += side.length;
    }
    Circle circle;
    if (profileIsCircle(outer, circleTolerance, &circle)) {
        r.circle = true;
        r.diameter = 2 * circle.radius * scale;
        r.sides.clear();
    }
    r.angle = profileAlignment(outer);
    const auto bounds = [&](const ProfileLoop& loop, Vec2& lo, Vec2& hi) {
        const auto pts = profilePolyline(loop, 0.5);
        if (pts.empty())
            return false;
        lo = hi = rotate(pts.front(), r.angle);
        for (const Vec2& p : pts) {
            const Vec2 q = rotate(p, r.angle);
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
        }
        return true;
    };
    Vec2 lo, hi;
    if (bounds(outer, lo, hi)) {
        r.width = (hi.x - lo.x) * scale;
        r.height = (hi.y - lo.y) * scale;
    }
    r.area = std::abs(profileSignedArea(outer)) * scale * scale;
    for (const auto& h : holes) {
        ProfileReport::Hole hole;
        Circle c;
        if (profileIsCircle(h, circleTolerance, &c)) {
            hole.circle = true;
            hole.diameter = 2 * c.radius * scale;
            hole.center = c.center;
        }
        Vec2 hlo, hhi;
        if (bounds(h, hlo, hhi)) {
            hole.width = (hhi.x - hlo.x) * scale;
            hole.height = (hhi.y - hlo.y) * scale;
            if (!hole.circle)
                hole.center = rotate((hlo + hhi) * 0.5, -r.angle);
        }
        r.area -= std::abs(profileSignedArea(h)) * scale * scale;
        r.holes.push_back(hole);
    }
    return r;
}

} // namespace os::measure
