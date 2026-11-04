// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/Outline.h"

#include <algorithm>
#include <cmath>

namespace os::measure {

OutlineReport describeOutline(const std::vector<Vec2>& points, bool closed)
{
    OutlineReport r;
    const std::size_t n = points.size();
    if (n == 0)
        return r;
    r.boundsMin = r.boundsMax = points.front();
    for (const Vec2& p : points) {
        r.boundsMin = {std::min(r.boundsMin.x, p.x), std::min(r.boundsMin.y, p.y)};
        r.boundsMax = {std::max(r.boundsMax.x, p.x), std::max(r.boundsMax.y, p.y)};
    }
    if (n < 2)
        return r;
    const bool loop = closed && n >= 3;
    const std::size_t sides = loop ? n : n - 1;
    for (std::size_t i = 0; i < sides; ++i) {
        const double len = (points[(i + 1) % n] - points[i]).length();
        r.sideLengths.push_back(len);
        r.perimeter += len;
    }
    if (!loop)
        return r;
    double s = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2& a = points[i];
        const Vec2& b = points[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    r.area = std::abs(s) * 0.5;
    const double orientation = s >= 0 ? 1.0 : -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 toPrev = points[(i + n - 1) % n] - points[i];
        const Vec2 toNext = points[(i + 1) % n] - points[i];
        // The turn from the next side to the previous one, measured inside.
        const double cross = toNext.x * toPrev.y - toNext.y * toPrev.x;
        double angle = std::atan2(cross, toNext.dot(toPrev)) * 180.0 / kPi;
        angle *= orientation;
        if (angle < 0)
            angle += 360.0;
        r.cornerAngles.push_back(angle);
    }
    return r;
}

std::vector<Vec2> centeredOnOrigin(const std::vector<Vec2>& points, bool flipY)
{
    if (points.empty())
        return {};
    Vec2 lo = points.front(), hi = points.front();
    for (const Vec2& p : points) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    const Vec2 c = (lo + hi) * 0.5;
    std::vector<Vec2> out;
    out.reserve(points.size());
    for (const Vec2& p : points) {
        const Vec2 q = p - c;
        out.push_back(flipY ? Vec2{q.x, -q.y} : q);
    }
    return out;
}

} // namespace os::measure
