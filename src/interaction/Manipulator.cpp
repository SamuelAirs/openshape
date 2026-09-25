#include "interaction/Manipulator.h"

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
