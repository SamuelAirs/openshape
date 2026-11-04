// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/StrokeFit.h"

#include <algorithm>
#include <cmath>

namespace os::measure {

namespace {

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double len2 = ab.dot(ab);
    if (len2 <= 0)
        return (p - a).length();
    const double t = std::clamp((p - a).dot(ab) / len2, 0.0, 1.0);
    return (p - (a + ab * t)).length();
}

// Samples at an even spacing along the polyline (keeps both ends).
std::vector<Vec2> resample(const std::vector<Vec2>& pts, double spacing)
{
    std::vector<Vec2> out;
    if (pts.empty())
        return out;
    out.push_back(pts.front());
    double carry = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        Vec2 a = pts[i - 1];
        const Vec2 b = pts[i];
        double seg = (b - a).length();
        while (carry + seg >= spacing && seg > 0) {
            const double t = (spacing - carry) / seg;
            a = a + (b - a) * t;
            out.push_back(a);
            seg = (b - a).length();
            carry = 0;
        }
        carry += seg;
    }
    if ((out.back() - pts.back()).length() > 1e-9)
        out.push_back(pts.back());
    return out;
}

// One pass of a (1, 2, 1) / 4 filter: hand tremor out, the ends kept.
std::vector<Vec2> smooth(const std::vector<Vec2>& pts, bool closed)
{
    if (pts.size() < 3)
        return pts;
    std::vector<Vec2> out(pts.size());
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i + 1 == n)) {
            out[i] = pts[i];
            continue;
        }
        const Vec2 prev = pts[(i + n - 1) % n];
        const Vec2 next = pts[(i + 1) % n];
        out[i] = (prev + pts[i] * 2.0 + next) * 0.25;
    }
    return out;
}

void rdp(const std::vector<Vec2>& pts, std::size_t first, std::size_t last, double tolerance, std::vector<std::size_t>& keep)
{
    if (last <= first + 1)
        return;
    double worst = -1;
    std::size_t at = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double d = distanceToSegment(pts[i], pts[first], pts[last]);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (worst > tolerance) {
        rdp(pts, first, at, tolerance, keep);
        keep.push_back(at);
        rdp(pts, at, last, tolerance, keep);
    }
}

// Indices of the vertices RDP keeps between first and last (both included).
std::vector<std::size_t> rdpIndices(const std::vector<Vec2>& pts, std::size_t first, std::size_t last, double tolerance)
{
    std::vector<std::size_t> keep{first};
    rdp(pts, first, last, tolerance, keep);
    keep.push_back(last);
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    return keep;
}

double wrapAngle(double a)
{
    while (a < 0)
        a += 2 * kPi;
    while (a >= 2 * kPi)
        a -= 2 * kPi;
    return a;
}

// The sweep (radians) of the arc from a through m to b about c.
double arcSweep(const Circle& c, Vec2 a, Vec2 m, Vec2 b)
{
    const double a0 = std::atan2(a.y - c.center.y, a.x - c.center.x);
    const double am = std::atan2(m.y - c.center.y, m.x - c.center.x);
    const double a1 = std::atan2(b.y - c.center.y, b.x - c.center.x);
    const double ccwToEnd = wrapAngle(a1 - a0);
    const double ccwToMid = wrapAngle(am - a0);
    return ccwToMid <= ccwToEnd ? ccwToEnd : 2 * kPi - ccwToEnd;
}

// Whether the samples run round the circle one way only (a little hand
// wobble allowed), so an arc stands for them.
bool monotoneAround(const Circle& c, const std::vector<Vec2>& pts, std::size_t from, std::size_t to)
{
    double total = 0, back = 0;
    double prev = std::atan2(pts[from].y - c.center.y, pts[from].x - c.center.x);
    for (std::size_t i = from + 1; i <= to; ++i) {
        const double a = std::atan2(pts[i].y - c.center.y, pts[i].x - c.center.x);
        double d = a - prev;
        while (d > kPi)
            d -= 2 * kPi;
        while (d < -kPi)
            d += 2 * kPi;
        total += d;
        prev = a;
        back += std::abs(d);
    }
    // |sum| close to the sum of |steps|: no turning back.
    return back > 0 && std::abs(total) >= 0.9 * back;
}

} // namespace

std::optional<Circle> circleThrough(Vec2 a, Vec2 b, Vec2 c)
{
    const double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    const double scale = std::max({(b - a).length(), (c - b).length(), (a - c).length()});
    if (!(scale > 0) || std::abs(d) < 1e-9 * scale * scale)
        return std::nullopt;
    const double a2 = a.dot(a), b2 = b.dot(b), c2 = c.dot(c);
    const Vec2 center{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
                      (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
    return Circle{center, (a - center).length()};
}

std::optional<Circle> fitCircle(const std::vector<Vec2>& points)
{
    if (points.size() < 3)
        return std::nullopt;
    // Kasa's algebraic fit about the centroid: x² + y² + D x + E y + F = 0.
    Vec2 m;
    for (const Vec2& p : points)
        m = m + p;
    m = m * (1.0 / double(points.size()));
    double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (const Vec2& p : points) {
        const double u = p.x - m.x, v = p.y - m.y;
        suu += u * u;
        svv += v * v;
        suv += u * v;
        suuu += u * u * u;
        svvv += v * v * v;
        suvv += u * v * v;
        svuu += v * u * u;
    }
    const double det = suu * svv - suv * suv;
    if (std::abs(det) < 1e-12 * (suu + svv) * (suu + svv) || !(suu + svv > 0))
        return std::nullopt;
    const double bu = 0.5 * (suuu + suvv), bv = 0.5 * (svvv + svuu);
    const double uc = (bu * svv - bv * suv) / det;
    const double vc = (suu * bv - suv * bu) / det;
    Circle c{{m.x + uc, m.y + vc}, 0};
    double r2 = 0;
    for (const Vec2& p : points)
        r2 += (p - c.center).dot(p - c.center);
    c.radius = std::sqrt(r2 / double(points.size()));
    // A few Gauss-Newton steps on the geometric distances (Kasa leans small
    // on short arcs).
    for (int iter = 0; iter < 10; ++iter) {
        double jtj[3][3] = {}, jtr[3] = {};
        for (const Vec2& p : points) {
            const Vec2 d = p - c.center;
            const double dist = d.length();
            if (dist <= 1e-12)
                continue;
            const double res = dist - c.radius;
            const double j[3] = {-d.x / dist, -d.y / dist, -1.0};
            for (int a = 0; a < 3; ++a) {
                jtr[a] += j[a] * res;
                for (int b = 0; b < 3; ++b)
                    jtj[a][b] += j[a] * j[b];
            }
        }
        // Solve jtj * delta = -jtr (3 x 3, Cramer's rule).
        auto det3 = [](const double m3[3][3]) {
            return m3[0][0] * (m3[1][1] * m3[2][2] - m3[1][2] * m3[2][1]) - m3[0][1] * (m3[1][0] * m3[2][2] - m3[1][2] * m3[2][0])
                 + m3[0][2] * (m3[1][0] * m3[2][1] - m3[1][1] * m3[2][0]);
        };
        const double dj = det3(jtj);
        if (std::abs(dj) < 1e-18)
            break;
        double delta[3];
        for (int k = 0; k < 3; ++k) {
            double mk[3][3];
            for (int a = 0; a < 3; ++a)
                for (int b = 0; b < 3; ++b)
                    mk[a][b] = b == k ? -jtr[a] : jtj[a][b];
            delta[k] = det3(mk) / dj;
        }
        c.center = c.center + Vec2{delta[0], delta[1]};
        c.radius += delta[2];
        if (std::abs(delta[0]) + std::abs(delta[1]) + std::abs(delta[2]) < 1e-9 * std::max(1.0, c.radius))
            break;
    }
    if (!(c.radius > 0) || !std::isfinite(c.radius))
        return std::nullopt;
    return c;
}

std::vector<Vec2> simplifyPolyline(const std::vector<Vec2>& points, double tolerance)
{
    if (points.size() < 3)
        return points;
    std::vector<Vec2> out;
    for (std::size_t i : rdpIndices(points, 0, points.size() - 1, tolerance))
        out.push_back(points[i]);
    return out;
}

FittedStroke fitStroke(const std::vector<Vec2>& samples, const FitOptions& options)
{
    FittedStroke result;
    const double tol = options.tolerance > 0 ? options.tolerance : 0.5;
    std::vector<Vec2> raw;
    for (const Vec2& p : samples)
        if (std::isfinite(p.x) && std::isfinite(p.y) && (raw.empty() || (p - raw.back()).length() > 1e-9))
            raw.push_back(p);
    if (raw.size() < 2)
        return result;
    double length = 0;
    for (std::size_t i = 1; i < raw.size(); ++i)
        length += (raw[i] - raw[i - 1]).length();
    if (length < tol)
        return result;

    const bool closed = raw.size() >= 4 && (raw.front() - raw.back()).length() <= options.closeGap
                     && length > 4 * std::max(options.closeGap, tol);
    std::vector<Vec2> pts = resample(raw, std::max(tol * 0.5, length / 4000.0));
    if (closed && pts.size() > 2 && (pts.front() - pts.back()).length() < 1e-9)
        pts.pop_back();
    pts = smooth(pts, closed);
    result.closed = closed;

    if (closed && options.circles && pts.size() >= 8) {
        if (const auto c = fitCircle(pts); c && c->radius > 2 * tol) {
            double worst = 0;
            for (const Vec2& p : pts)
                worst = std::max(worst, std::abs((p - c->center).length() - c->radius));
            if (worst <= tol) {
                result.kind = FittedStroke::Kind::Circle;
                result.circle = *c;
                result.maxError = worst;
                return result;
            }
        }
    }

    // A closed stroke runs on past its start: one more sample at the start
    // makes the loop, split at the sample farthest from it so RDP keeps it.
    std::vector<std::size_t> vertices;
    if (closed) {
        pts.push_back(pts.front());
        std::size_t far = 0;
        double farDist = -1;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const double d = (pts[i] - pts.front()).length();
            if (d > farDist) {
                farDist = d;
                far = i;
            }
        }
        vertices = rdpIndices(pts, 0, far, tol);
        const auto second = rdpIndices(pts, far, pts.size() - 1, tol);
        vertices.insert(vertices.end(), second.begin() + 1, second.end());
    } else {
        vertices = rdpIndices(pts, 0, pts.size() - 1, tol);
    }

    result.kind = FittedStroke::Kind::Path;
    std::size_t i = 0;
    while (i + 1 < vertices.size()) {
        std::size_t best = i + 1;
        std::optional<Circle> bestCircle;
        std::size_t bestMid = 0;
        double bestErr = 0;
        if (options.arcs) {
            for (std::size_t j = i + 2; j < vertices.size(); ++j) {
                const std::size_t from = vertices[i], to = vertices[j];
                const std::size_t mid = (from + to) / 2;
                const auto c = circleThrough(pts[from], pts[mid], pts[to]);
                if (!c || c->radius > 1e4 * tol)
                    break;
                const double sweep = arcSweep(*c, pts[from], pts[mid], pts[to]);
                if (sweep < 15.0 * kPi / 180.0 || sweep > 330.0 * kPi / 180.0)
                    break;
                double worst = 0;
                for (std::size_t k = from; k <= to; ++k)
                    worst = std::max(worst, std::abs((pts[k] - c->center).length() - c->radius));
                if (worst > tol || !monotoneAround(*c, pts, from, to))
                    break;
                best = j;
                bestCircle = c;
                bestMid = mid;
                bestErr = worst;
            }
        }
        FittedSegment seg;
        seg.start = pts[vertices[i]];
        seg.end = pts[vertices[best]];
        if (bestCircle) {
            seg.arc = true;
            seg.through = pts[bestMid];
            seg.center = bestCircle->center;
            seg.radius = bestCircle->radius;
            result.maxError = std::max(result.maxError, bestErr);
        } else {
            seg.through = (seg.start + seg.end) * 0.5;
            for (std::size_t k = vertices[i]; k <= vertices[best]; ++k)
                result.maxError = std::max(result.maxError, distanceToSegment(pts[k], seg.start, seg.end));
        }
        result.segments.push_back(seg);
        i = best;
    }
    // Exactly closed: the last piece ends where the first starts.
    if (closed && !result.segments.empty())
        result.segments.back().end = result.segments.front().start;
    if (result.segments.empty())
        result.kind = FittedStroke::Kind::Empty;
    return result;
}

} // namespace os::measure
