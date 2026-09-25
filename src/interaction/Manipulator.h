#pragma once

#include "core/Camera.h"

#include <optional>

namespace os::interact {

// Screen-space proportions of a linear arrow handle (logical pixels). The
// renderer draws with the same numbers the hit test uses.
struct ArrowStyle {
    double gapPx = 6;        // distance from the anchor to the start of the shaft
    double shaftPx = 64;
    double headPx = 18;
    double shaftRadiusPx = 2.2;
    double headRadiusPx = 7;
    double totalPx() const { return gapPx + shaftPx + headPx; }
};

enum class HandleState { Normal, Hovered, Active, Error };

// A one-dimensional drag handle: an arrow anchored at base + direction*value.
// Dragging changes `value` by the distance the pointer ray travels along the
// axis, so the arrow tip stays under the pointer.
class LinearManipulator {
public:
    LinearManipulator() = default;
    LinearManipulator(const Vec3& base, const Vec3& direction) : base_(base), direction_(direction.normalized()) {}

    const Vec3& base() const { return base_; }
    const Vec3& direction() const { return direction_; }
    Vec3 anchor(double value) const { return base_ + direction_ * value; }

    // Distance in pixels from `screen` to the arrow, if within tolerance.
    std::optional<double> hitTest(const Camera& camera, Vec2 screen, double value, double tolerancePx,
                                  const ArrowStyle& style = {}) const;

    void beginDrag(const Camera& camera, Vec2 screen, double value);
    // Returns the new (unsnapped) value for the pointer position.
    double dragTo(const Camera& camera, Vec2 screen) const;

private:
    // Parameter along the axis for a screen position. Falls back to vertical
    // screen motion when the axis points (almost) straight at the viewer.
    double axisParameter(const Camera& camera, Vec2 screen) const;

    Vec3 base_;
    Vec3 direction_{0, 0, 1};
    double dragStartValue_ = 0;
    double dragStartParameter_ = 0;
    Vec2 dragStartScreen_;
    bool screenFallback_ = false;
};

// Rounds `value` to a "nice" increment appropriate for the current zoom:
// roughly `targetPixels` on screen per step (1, 2, 5 x 10^n mm).
double snapIncrement(double pixelSize, double targetPixels = 8.0);
double snapValue(double value, double increment);

} // namespace os::interact
