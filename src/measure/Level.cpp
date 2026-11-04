// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/Level.h"

#include <algorithm>
#include <cmath>

namespace os::measure {

namespace {
constexpr double kDeg = 180.0 / kPi;
}

Tilt tiltFrom(const Gravity& g, double bubbleRangeDeg)
{
    Tilt t;
    const Vec3 v{g.x, g.y, g.z};
    const double len = v.length();
    if (!(len > 1e-6))
        return t;
    const Vec3 n = v / len;
    // Lying: gravity mostly through the glass.
    t.lying = std::abs(n.z) >= std::sqrt(0.5);
    t.slopeDeg = std::acos(std::clamp(std::abs(n.z), 0.0, 1.0)) * kDeg;
    // The bubble floats to the high side, away from where gravity leans.
    const double range = std::max(0.1, bubbleRangeDeg);
    const double rollDeg = std::atan2(n.x, std::abs(n.z)) * kDeg;
    const double pitchDeg = std::atan2(n.y, std::abs(n.z)) * kDeg;
    t.bubble = {std::clamp(-rollDeg / range, -1.0, 1.0), std::clamp(-pitchDeg / range, -1.0, 1.0)};
    // Standing: the bottom edge is level when gravity points straight down
    // the screen (+y); a lean of gravity to the left (x < 0) means the right
    // end is higher.
    t.edgeDeg = std::atan2(-n.x, n.y) * kDeg;
    return t;
}

double angleBetween(const Gravity& a, const Gravity& b)
{
    const Vec3 u = Vec3{a.x, a.y, a.z}.normalized();
    const Vec3 w = Vec3{b.x, b.y, b.z}.normalized();
    if (u.length() == 0 || w.length() == 0)
        return 0;
    // atan2 of |u x w| and u . w: accurate at small angles too.
    return std::atan2(u.cross(w).length(), u.dot(w)) * kDeg;
}

double relativeEdgeAngle(double edgeDeg, double referenceEdgeDeg)
{
    double d = edgeDeg - referenceEdgeDeg;
    while (d > 180)
        d -= 360;
    while (d <= -180)
        d += 360;
    return d;
}

Vec2 lyingAngles(const Gravity& g)
{
    const double z = std::max(std::abs(g.z), 1e-9);
    return {std::atan2(g.x, z) * kDeg, std::atan2(g.y, z) * kDeg};
}

Gravity fromLyingAngles(Vec2 anglesDeg, bool faceDown)
{
    const Vec3 v = Vec3{std::tan(anglesDeg.x / kDeg), std::tan(anglesDeg.y / kDeg), faceDown ? 1.0 : -1.0}.normalized();
    return {v.x, v.y, v.z};
}

FlipResult lyingFlip(const Gravity& first, const Gravity& second)
{
    const Vec2 a = lyingAngles(first), b = lyingAngles(second);
    return {(a + b) * 0.5, (a - b) * 0.5};
}

FlipResult edgeFlip(double firstEdgeDeg, double secondEdgeDeg)
{
    return {{0.5 * (firstEdgeDeg + secondEdgeDeg), 0}, {relativeEdgeAngle(firstEdgeDeg, secondEdgeDeg) * 0.5, 0}};
}

Gravity correctedLying(const Gravity& g, Vec2 offsetDeg)
{
    const double len = Vec3{g.x, g.y, g.z}.length();
    if (!(len > 1e-6))
        return g;
    const Gravity c = fromLyingAngles(lyingAngles(g) - offsetDeg, g.z > 0);
    return {c.x * len, c.y * len, c.z * len};
}

namespace {
int quarter(int turn) { return ((turn % 4) + 4) % 4; }
} // namespace

Gravity windowToUpright(const Gravity& g, int turn)
{
    // MeasureDevicesIos.mm's toWindowFrame, undone: upright, the window's
    // axes are the device's (y down the screen).
    switch (quarter(turn)) {
    case 1: return {-g.y, g.x, g.z};
    case 2: return {-g.x, -g.y, g.z};
    case 3: return {g.y, -g.x, g.z};
    default: return g;
    }
}

Gravity uprightToWindow(const Gravity& g, int turn)
{
    switch (quarter(turn)) {
    case 1: return {g.y, -g.x, g.z};
    case 2: return {-g.x, -g.y, g.z};
    case 3: return {-g.y, g.x, g.z};
    default: return g;
    }
}

} // namespace os::measure
