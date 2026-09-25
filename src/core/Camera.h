#pragma once

#include "core/Math.h"

namespace os {

enum class StandardView { Front, Back, Left, Right, Top, Bottom, Isometric };

// Z-up turntable camera. Pure math: shared by picking, interaction and the
// renderer. Screen coordinates are logical (device-independent) pixels with
// the origin at the top-left and y pointing down.
class Camera {
public:
    enum class Projection { Orthographic, Perspective };

    Vec3 target{0, 0, 0};
    double yaw = -kPi / 4;          // radians, around +Z; 0 = looking from +X
    double pitch = 0.6154797087;    // radians, elevation; +90deg = from above (atan(1/sqrt2))
    double orthoHeight = 80.0;      // mm visible vertically (orthographic)
    double distance = 150.0;        // eye-target distance (perspective)
    double fovY = 0.61;             // radians (~35 degrees)
    Projection projection = Projection::Orthographic;

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
    void orbit(double dxPixels, double dyPixels, const Vec3& pivot);
    void pan(Vec2 fromScreen, Vec2 toScreen);
    // factor < 1 zooms in. Keeps the point under `screen` fixed.
    void zoomAt(Vec2 screen, double factor);
    void fit(const Vec3& boxMin, const Vec3& boxMax);
    void setStandardView(StandardView view);

    static Camera interpolate(const Camera& a, const Camera& b, double t);

private:
    double orthoWidth() const;
};

} // namespace os
