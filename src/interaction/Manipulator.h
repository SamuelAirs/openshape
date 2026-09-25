// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Camera.h"

#include <optional>

namespace os::interact {

// Screen-space proportions of a linear arrow handle (logical pixels). The
// renderer draws with the same numbers the hit test uses.
struct ArrowStyle {
    double gapPx = 6;        // distance from the anchor to the start of the shaft
    double shaftPx = 64;
    double headPx = 20;
    double shaftRadiusPx = 2.6;
    double headRadiusPx = 8.5;
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

// Screen-space size of a rotation ring (logical pixels).
struct RingStyle {
    double radiusPx = 92;
    double widthPx = 3;
};

// A rotation handle: a circle of constant screen size around `center`, in the
// plane normal to `axis`. Dragging around it changes an angle (radians); the
// angle keeps counting past half a turn.
class RingManipulator {
public:
    RingManipulator() = default;
    RingManipulator(const Vec3& center, const Vec3& axis);

    const Vec3& center() const { return center_; }
    const Vec3& axis() const { return axis_; }
    // World radius for the current zoom.
    double radius(const Camera& camera, const RingStyle& style = {}) const;
    // Point on the ring at `angle` radians from its reference direction.
    Vec3 pointAt(const Camera& camera, double angle, const RingStyle& style = {}) const;

    // Distance in pixels from `screen` to the ring, if within tolerance.
    std::optional<double> hitTest(const Camera& camera, Vec2 screen, double tolerancePx, const RingStyle& style = {}) const;

    void beginDrag(const Camera& camera, Vec2 screen, double angle);
    // New (unsnapped) angle for the pointer position.
    double dragTo(const Camera& camera, Vec2 screen);

private:
    // Pointer angle around the axis in the ring's plane; nullopt when the
    // plane is seen (almost) edge-on.
    std::optional<double> pointerAngle(const Camera& camera, Vec2 screen) const;

    Vec3 center_;
    Vec3 axis_{0, 0, 1};
    Vec3 u_{1, 0, 0}; // in-plane basis: angle 0 lies along u
    Vec3 v_{0, 1, 0};
    double dragStartAngle_ = 0;
    double lastPointer_ = 0;
    double turned_ = 0;
    bool screenFallback_ = false;
    Vec2 dragStartScreen_;
    Vec2 screenTangent_;     // fallback: pointer motion along this turns the ring
    double screenRadiusPx_ = 92;
};

// Rounds `value` to a "nice" increment appropriate for the current zoom:
// roughly `targetPixels` on screen per step (1, 2, 5 x 10^n mm).
double snapIncrement(double pixelSize, double targetPixels = 8.0);
double snapValue(double value, double increment);

} // namespace os::interact
