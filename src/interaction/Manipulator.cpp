// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/Manipulator.h"

#include <algorithm>
#include <cmath>

namespace os::interact {

std::optional<double> LinearManipulator::hitTest(const Camera& camera, Vec2 screen, double value, double tolerancePx,
                                                 const ArrowStyle& style) const
{
    const Vec3 a = anchor(value);
    const double px = camera.pixelSize(a);
    const Vec3 start = a + direction_ * (style.gapPx * px);
    const Vec3 tip = a + direction_ * (style.totalPx() * px);
    const Vec2 s0 = camera.project(start);
    const Vec2 s1 = camera.project(tip);
    // When the arrow points at the viewer its projection collapses; treat the
    // head as a disc so it stays grabbable.
    const double reach = tolerancePx + style.headRadiusPx;
    const double d = distanceToSegment2D(screen, s0, s1);
    if (d <= reach)
        return d;
    return std::nullopt;
}

double LinearManipulator::axisParameter(const Camera& camera, Vec2 screen) const
{
    if (!screenFallback_) {
        if (const auto t = closestLineParameterToRay(base_, direction_, camera.rayAt(screen)))
            return *t;
    }
    const double px = camera.pixelSize(base_);
    return -(screen.y - dragStartScreen_.y) * px;
}

void LinearManipulator::beginDrag(const Camera& camera, Vec2 screen, double value)
{
    dragStartValue_ = value;
    dragStartScreen_ = screen;
    // Axis within ~8 degrees of the view direction: closest-point math becomes
    // unstable, so use vertical screen motion instead.
    screenFallback_ = std::abs(direction_.dot(camera.forward())) > 0.99;
    dragStartParameter_ = axisParameter(camera, screen);
}

double LinearManipulator::dragTo(const Camera& camera, Vec2 screen) const
{
    const double t = axisParameter(camera, screen);
    if (!std::isfinite(t))
        return dragStartValue_;
    return dragStartValue_ + (t - dragStartParameter_);
}

// ---- Rotation ring -----------------------------------------------------------------

RingManipulator::RingManipulator(const Vec3& center, const Vec3& axis) : center_(center), axis_(axis.normalized())
{
    const Vec3 helper = std::abs(axis_.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    u_ = helper.cross(axis_).normalized();
    v_ = axis_.cross(u_);
}

double RingManipulator::radius(const Camera& camera, const RingStyle& style) const
{
    return style.radiusPx * camera.pixelSize(center_);
}

Vec3 RingManipulator::pointAt(const Camera& camera, double angle, const RingStyle& style) const
{
    return center_ + (u_ * std::cos(angle) + v_ * std::sin(angle)) * radius(camera, style);
}

std::optional<double> RingManipulator::hitTest(const Camera& camera, Vec2 screen, double tolerancePx,
                                               const RingStyle& style) const
{
    constexpr int segments = 48;
    double best = 1e300;
    Vec2 previous = camera.project(pointAt(camera, 0.0, style));
    for (int i = 1; i <= segments; ++i) {
        const Vec2 next = camera.project(pointAt(camera, 2 * kPi * i / segments, style));
        best = std::min(best, distanceToSegment2D(screen, previous, next));
        previous = next;
    }
    const double reach = tolerancePx + style.widthPx;
    if (best <= reach)
        return best;
    return std::nullopt;
}

std::optional<double> RingManipulator::pointerAngle(const Camera& camera, Vec2 screen) const
{
    const Ray ray = camera.rayAt(screen);
    const double denom = ray.direction.dot(axis_);
    if (std::abs(denom) < 0.15) // plane within ~9 degrees of edge-on
        return std::nullopt;
    const double t = (center_ - ray.origin).dot(axis_) / denom;
    const Vec3 d = ray.at(t) - center_;
    return std::atan2(d.dot(v_), d.dot(u_));
}

void RingManipulator::beginDrag(const Camera& camera, Vec2 screen, double angle)
{
    dragStartAngle_ = angle;
    dragStartScreen_ = screen;
    turned_ = 0;
    const auto a = pointerAngle(camera, screen);
    screenFallback_ = !a;
    if (a) {
        lastPointer_ = *a;
        return;
    }
    // Edge-on: turn by pointer motion along the ring's screen tangent at the
    // grabbed point (nearest ring sample), one radius of motion = one radian.
    constexpr int segments = 48;
    double best = 1e300;
    double grabAngle = 0;
    for (int i = 0; i < segments; ++i) {
        const double s = 2 * kPi * i / segments;
        const double dist = (camera.project(pointAt(camera, s)) - screen).length();
        if (dist < best) {
            best = dist;
            grabAngle = s;
        }
    }
    const Vec2 p0 = camera.project(pointAt(camera, grabAngle));
    const Vec2 p1 = camera.project(pointAt(camera, grabAngle + 0.05));
    const Vec2 tangent = p1 - p0;
    screenTangent_ = tangent.length() > 1e-9 ? tangent * (1.0 / tangent.length()) : Vec2{1, 0};
    screenRadiusPx_ = std::max(RingStyle{}.radiusPx, 1.0);
}

double RingManipulator::dragTo(const Camera& camera, Vec2 screen)
{
    if (screenFallback_) {
        const Vec2 delta = screen - dragStartScreen_;
        return dragStartAngle_ + (delta.x * screenTangent_.x + delta.y * screenTangent_.y) / screenRadiusPx_;
    }
    const auto a = pointerAngle(camera, screen);
    if (!a)
        return dragStartAngle_ + turned_;
    // Unwrap so a drag past half a turn keeps counting.
    turned_ += std::remainder(*a - lastPointer_, 2 * kPi);
    lastPointer_ = *a;
    return dragStartAngle_ + turned_;
}

double snapIncrement(double pixelSize, double targetPixels)
{
    const double raw = pixelSize * targetPixels;
    if (!(raw > 0) || !std::isfinite(raw))
        return 1.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * magnitude >= raw)
            return m * magnitude;
    return 10 * magnitude;
}

double snapValue(double value, double increment)
{
    if (!(increment > 0))
        return value;
    const double snapped = std::round(value / increment) * increment;
    // Clean up binary noise such as 14.999999999 for display and storage.
    return std::round(snapped * 1e9) / 1e9;
}

} // namespace os::interact
