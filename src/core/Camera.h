// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

namespace os {

enum class StandardView { Front, Back, Left, Right, Top, Bottom, Isometric };

// Z-up turntable camera, perspective by default (depth cues: a box cannot
// flip in the viewer's mind), orthographic optional. Pure math: shared by
// picking, interaction and the renderer. Screen coordinates are logical
// (device-independent) pixels with the origin at the top-left and y pointing
// down.
class Camera {
public:
    enum class Projection { Orthographic, Perspective };

    // The closest the eye comes to the target in perspective (mm): five
    // times the near plane's floor, so the surface a zoom heads for is never
    // clipped by the near plane.
    static constexpr double kMinDistance = 0.05;

    Vec3 target{0, 0, 0};
    double yaw = -kPi / 4;          // radians, around +Z; 0 = looking from +X
    // radians, elevation; +90deg = from above (atan(1/sqrt2)). Beyond +-90deg
    // the view has gone over the top (or under the bottom) and is upside
    // down: the eye is on the other side of the vertical, +Z points down the
    // screen. Kept within (-180deg, 180deg].
    double pitch = 0.6154797087;
    double orthoHeight = 80.0;      // mm visible vertically (orthographic)
    double distance = 150.0;        // eye-target distance (perspective)
    double fovY = 0.61;             // radians (~35 degrees)
    Projection projection = Projection::Perspective;

    Vec2 viewportSize{800, 600};    // logical pixels
    // Bounding sphere of everything that must not be clipped.
    Vec3 sceneCenter{0, 0, 0};
    double sceneRadius = 100.0;

    // Unit vector from target towards the eye.
    Vec3 backward() const;
    Vec3 forward() const { return -backward(); }
    Vec3 right() const;
    Vec3 up() const;
    Vec3 eye() const;

    Mat4 viewMatrix() const;
    // depthZeroToOne: Vulkan/D3D/Metal convention (used for picking math).
    // Otherwise OpenGL convention [-1, 1], which the renderer converts with
    // QRhi::clipSpaceCorrMatrix().
    Mat4 projectionMatrix(bool depthZeroToOne = true) const;
    Mat4 viewProjection() const { return projectionMatrix() * viewMatrix(); }

    Vec2 project(const Vec3& world) const;
    // View-space depth (distance along forward from the eye / reference plane).
    double depthOf(const Vec3& world) const;
    Ray rayAt(Vec2 screen) const;
    // World size (mm) of one logical pixel at a given point.
    double pixelSize(const Vec3& at) const;

    // Point on the plane through `planePoint` perpendicular to the view
    // direction that lies under the given screen position.
    Vec3 pointOnViewPlane(Vec2 screen, const Vec3& planePoint) const;

    // ---- Navigation ----
    // Whether the view is upside down (pitch beyond +-90 degrees).
    bool upsideDown() const;
    // +1, or -1 while the view is upside down: the way a sideways drag turns
    // the view about the vertical so that the model follows the finger. A
    // drag takes it once, where it starts, and keeps it (orbit's `sense`):
    // changing it as the view passes over the top would reverse the turn
    // under the finger.
    double orbitSense() const { return upsideDown() ? -1.0 : 1.0; }
    // A turntable about the vertical through `pivot`, which stays where it
    // is on screen: dx turns about +Z, dy changes the elevation, with no
    // stop at the top or the bottom (the view goes on over the pole).
    void orbit(double dxPixels, double dyPixels, const Vec3& pivot, double sense = 1.0);
    void pan(Vec2 fromScreen, Vec2 toScreen);
    // factor < 1 zooms in. Keeps the point under `screen` fixed. In
    // perspective the distance stops at kMinDistance (a zoom in never moves
    // the eye back when it is already closer).
    void zoomAt(Vec2 screen, double factor);
    void fit(const Vec3& boxMin, const Vec3& boxMax);
    void setStandardView(StandardView view);
    // Looks from `direction` (from the target towards the eye, any length),
    // keeping the target and zoom: the view cube's corners. Straight up or
    // down keeps the current yaw (the turntable's right stays level); a zero
    // direction changes nothing.
    void lookFrom(const Vec3& direction);

    // Switches the projection, keeping the model about the same size on
    // screen (the perspective distance matches the orthographic height).
    void setProjection(Projection p);

    static Camera interpolate(const Camera& a, const Camera& b, double t);

private:
    double orthoWidth() const;
};

} // namespace os
