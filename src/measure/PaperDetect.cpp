// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/PaperDetect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace os::measure {

namespace {

double cross(Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

// Andrew's monotone chain; counter-clockwise in a y-up frame.
std::vector<Vec2> convexHull(std::vector<Vec2> pts)
{
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }), pts.end());
    if (pts.size() < 3)
        return pts;
    std::vector<Vec2> hull(2 * pts.size());
    std::size_t k = 0;
    for (const Vec2& p : pts) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], p) <= 0)
            --k;
        hull[k++] = p;
    }
    for (std::size_t i = pts.size() - 1, lower = k + 1; i-- > 0;) {
        const Vec2& p = pts[i];
        while (k >= lower && cross(hull[k - 2], hull[k - 1], p) <= 0)
            --k;
        hull[k++] = p;
    }
    hull.resize(k - 1);
    return hull;
}

double quadArea(const std::array<Vec2, 4>& q)
{
    double s = 0;
    for (int i = 0; i < 4; ++i)
        s += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
    return 0.5 * std::abs(s);
}

struct Line {
    Vec2 point, direction; // direction normalized
};

// The total-least-squares line through points.
bool fitLine(const std::vector<Vec2>& pts, Line& line)
{
    if (pts.size() < 8)
        return false;
    Vec2 c;
    for (const Vec2& p : pts)
        c = c + p;
    c = c * (1.0 / pts.size());
    double sxx = 0, sxy = 0, syy = 0;
    for (const Vec2& p : pts) {
        const Vec2 d = p - c;
        sxx += d.x * d.x;
        sxy += d.x * d.y;
        syy += d.y * d.y;
    }
    const double angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
    line = {c, {std::cos(angle), std::sin(angle)}};
    return true;
}

bool intersect(const Line& a, const Line& b, Vec2& out)
{
    const double det = a.direction.x * b.direction.y - a.direction.y * b.direction.x;
    if (std::abs(det) < 1e-9)
        return false;
    const Vec2 d = b.point - a.point;
    const double t = (d.x * b.direction.y - d.y * b.direction.x) / det;
    out = a.point + a.direction * t;
    return true;
}

} // namespace

std::optional<std::array<Vec2, 4>> detectPaper(const RgbImage& image)
{
    if (image.width < 32 || image.height < 32 || image.rgb.size() < std::size_t(image.width) * image.height * 3)
        return std::nullopt;
    const RgbImage work = downsampleRgb(image.rgb.data(), image.width, image.height, image.width * 3, 3, false, 800);
    const int w = work.width, h = work.height;
    const double sx = double(image.width) / w, sy = double(image.height) / h;

    // Bright and colourless: Otsu's threshold on brightness.
    std::vector<int> lum(std::size_t(w) * h);
    std::vector<std::uint8_t> grey(lum.size());
    long histogram[256] = {};
    for (std::size_t i = 0; i < lum.size(); ++i) {
        const std::uint8_t* p = &work.rgb[i * 3];
        const int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
        lum[i] = (p[0] + p[1] + p[2]) / 3;
        grey[i] = mx > 0 && double(mx - mn) / mx < 0.3;
        ++histogram[lum[i]];
    }
    const double total = double(lum.size());
    double sumAll = 0;
    for (int v = 0; v < 256; ++v)
        sumAll += v * double(histogram[v]);
    double sumBelow = 0, countBelow = 0, bestVar = -1;
    int threshold = 128;
    for (int t = 0; t < 255; ++t) {
        countBelow += histogram[t];
        sumBelow += t * double(histogram[t]);
        const double countAbove = total - countBelow;
        if (countBelow <= 0 || countAbove <= 0)
            continue;
        const double m0 = sumBelow / countBelow, m1 = (sumAll - sumBelow) / countAbove;
        const double between = countBelow * countAbove * (m0 - m1) * (m0 - m1);
        if (between > bestVar) {
            bestVar = between;
            threshold = t;
        }
    }
    threshold = std::max(threshold, 70);
    std::vector<std::uint8_t> bright(lum.size());
    for (std::size_t i = 0; i < lum.size(); ++i)
        bright[i] = lum[i] > threshold && grey[i];

    // The largest bright region (one touching the image's edge counts less).
    std::vector<int> label(lum.size(), -1);
    std::vector<std::size_t> stack;
    int best = -1;
    double bestScore = 0;
    std::vector<long> areas;
    for (std::size_t seed = 0; seed < lum.size(); ++seed) {
        if (!bright[seed] || label[seed] >= 0)
            continue;
        const int id = int(areas.size());
        long area = 0;
        bool edge = false;
        label[seed] = id;
        stack.push_back(seed);
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            ++area;
            const int x = int(i % w), y = int(i / w);
            edge = edge || x == 0 || y == 0 || x == w - 1 || y == h - 1;
            const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h)
                    continue;
                const std::size_t j = std::size_t(ny[k]) * w + nx[k];
                if (bright[j] && label[j] < 0) {
                    label[j] = id;
                    stack.push_back(j);
                }
            }
        }
        areas.push_back(area);
        const double score = double(area) * (edge ? 0.3 : 1.0);
        if (score > bestScore) {
            bestScore = score;
            best = id;
        }
    }
    if (best < 0 || areas[best] < 0.02 * total)
        return std::nullopt;

    // Its edge pixels and their convex hull.
    std::vector<Vec2> boundary;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            if (label[i] != best)
                continue;
            const bool rim = x == 0 || y == 0 || x == w - 1 || y == h - 1 || label[i - 1] != best || label[i + 1] != best
                             || label[i - w] != best || label[i + w] != best;
            if (rim)
                boundary.push_back({x + 0.5, y + 0.5});
        }
    const std::vector<Vec2> hull = convexHull(boundary);
    if (hull.size() < 4)
        return std::nullopt;

    // The hull's sharpest turns are the corner candidates.
    double perimeter = 0;
    for (std::size_t i = 0; i < hull.size(); ++i)
        perimeter += (hull[(i + 1) % hull.size()] - hull[i]).length();
    const double reach = 0.03 * perimeter;
    const int n = int(hull.size());
    std::vector<std::pair<double, int>> turns;
    for (int i = 0; i < n; ++i) {
        int a = i, b = i;
        double back = 0, ahead = 0;
        for (int s = 0; s < n && back < reach; ++s) {
            const int prev = (a - 1 + n) % n;
            back += (hull[a] - hull[prev]).length();
            a = prev;
        }
        for (int s = 0; s < n && ahead < reach; ++s) {
            const int next = (b + 1) % n;
            ahead += (hull[next] - hull[b]).length();
            b = next;
        }
        const Vec2 d1 = hull[i] - hull[a], d2 = hull[b] - hull[i];
        const double l1 = d1.length(), l2 = d2.length();
        if (l1 <= 0 || l2 <= 0)
            continue;
        const double turn = std::acos(std::clamp(d1.dot(d2) / (l1 * l2), -1.0, 1.0));
        turns.push_back({turn, i});
    }
    std::sort(turns.begin(), turns.end(), [](const auto& p, const auto& q) { return p.first > q.first; });
    std::vector<int> candidates;
    for (const auto& t : turns) {
        bool near = false;
        for (int c : candidates)
            near = near || (hull[c] - hull[t.second]).length() < reach;
        if (!near)
            candidates.push_back(t.second);
        if (candidates.size() >= 12)
            break;
    }
    if (candidates.size() < 4)
        return std::nullopt;
    std::sort(candidates.begin(), candidates.end());
    std::array<Vec2, 4> quad{};
    double bestArea = 0;
    const int m = int(candidates.size());
    for (int i = 0; i < m; ++i)
        for (int j = i + 1; j < m; ++j)
            for (int k = j + 1; k < m; ++k)
                for (int l = k + 1; l < m; ++l) {
                    const std::array<Vec2, 4> q{hull[candidates[i]], hull[candidates[j]], hull[candidates[k]], hull[candidates[l]]};
                    const double area = quadArea(q);
                    if (area > bestArea) {
                        bestArea = area;
                        quad = q;
                    }
                }
    if (bestArea < 0.02 * total)
        return std::nullopt;

    // Each side refitted to the region's edge pixels along it (where an
    // object hides the edge, its pixels lie off the line and are left out).
    std::array<Line, 4> sides{};
    for (int s = 0; s < 4; ++s) {
        const Vec2 a = quad[s], b = quad[(s + 1) % 4];
        const Vec2 dir = (b - a) * (1.0 / std::max(1e-9, (b - a).length()));
        const double len = (b - a).length();
        std::vector<Vec2> along;
        for (const Vec2& p : boundary) {
            const Vec2 d = p - a;
            const double t = d.dot(dir);
            const double off = std::abs(d.x * dir.y - d.y * dir.x);
            if (t > 0.08 * len && t < 0.92 * len && off < 2.5)
                along.push_back(p);
        }
        if (!fitLine(along, sides[s]))
            sides[s] = {a, dir};
    }
    std::array<Vec2, 4> corners{};
    for (int s = 0; s < 4; ++s) {
        Vec2 c;
        if (!intersect(sides[(s + 3) % 4], sides[s], c) || (c - quad[s]).length() > 0.05 * perimeter)
            c = quad[s];
        corners[s] = {c.x * sx, c.y * sy};
    }
    return corners;
}

} // namespace os::measure
