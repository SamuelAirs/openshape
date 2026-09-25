#include "core/Camera.h"

#include <algorithm>

namespace os {

namespace {

constexpr double kMaxPitch = kPi / 2;
constexpr double kOrbitRadiansPerPixel = 0.008;
constexpr double kMinOrthoHeight = 1e-3;
constexpr double kMaxOrthoHeight = 1e7;

Mat4 orthoGL(double l, double r, double b, double t, double n, double f)
{
    Mat4 m = Mat4::identity();
    m.at(0, 0) = 2.0 / (r - l);
    m.at(1, 1) = 2.0 / (t - b);
    m.at(2, 2) = -2.0 / (f - n);
    m.at(0, 3) = -(r + l) / (r - l);
    m.at(1, 3) = -(t + b) / (t - b);
    m.at(2, 3) = -(f + n) / (f - n);
    return m;
}

Mat4 perspectiveGL(double fovY, double aspect, double n, double f)
{
    const double c = 1.0 / std::tan(fovY / 2);
    Mat4 m;
    m.at(0, 0) = c / aspect;
    m.at(1, 1) = c;
    m.at(2, 2) = (f + n) / (n - f);
    m.at(2, 3) = 2 * f * n / (n - f);
    m.at(3, 2) = -1;
    return m;
}

// Rodrigues rotation of v around unit axis k.
Vec3 rotate(const Vec3& v, const Vec3& k, double angle)
{
    const double c = std::cos(angle), s = std::sin(angle);
    return v * c + k.cross(v) * s + k * (k.dot(v) * (1 - c));
}

double wrapAngle(double a)
{
    while (a > kPi)
        a -= 2 * kPi;
    while (a < -kPi)
        a += 2 * kPi;
    return a;
}

} // namespace

Vec3 Camera::backward() const
{
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}

Vec3 Camera::right() const
{
    // Depends on yaw only, so straight-down views remain well defined.
    return {-std::sin(yaw), std::cos(yaw), 0};
}

Vec3 Camera::up() const
{
    return backward().cross(right());
}

Vec3 Camera::eye() const
{
    if (projection == Projection::Perspective)
        return target + backward() * distance;
    // Orthographic: the eye sits outside the scene sphere; the image does not
    // depend on this distance.
    const double reach = (target - sceneCenter).length() + sceneRadius * 2 + 1.0;
    return target + backward() * reach;
}

Mat4 Camera::viewMatrix() const
{
    const Vec3 s = right();
    const Vec3 u = up();
    const Vec3 f = forward();
    const Vec3 e = eye();
    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -s.dot(e);
    r.at(1, 3) = -u.dot(e);
    r.at(2, 3) = f.dot(e);
    return r;
}

double Camera::orthoWidth() const
{
    return orthoHeight * viewportSize.x / std::max(viewportSize.y, 1.0);
}

Mat4 Camera::projectionMatrix(bool depthZeroToOne) const
{
    const double aspect = viewportSize.x / std::max(viewportSize.y, 1.0);
    const double eyeToCenter = (eye() - sceneCenter).length();
    const double farPlane = eyeToCenter + sceneRadius * 1.5 + 1.0;
    if (projection == Projection::Orthographic) {
        const double hw = orthoWidth() / 2, hh = orthoHeight / 2;
        const double nearPlane = std::max(0.01, eyeToCenter - sceneRadius * 1.5 - 1.0);
        return depthZeroToOne ? orthographic(-hw, hw, -hh, hh, nearPlane, farPlane)
                              : orthoGL(-hw, hw, -hh, hh, nearPlane, farPlane);
    }
    const double nearPlane = std::max({0.01, distance * 0.002, eyeToCenter - sceneRadius * 1.5});
    return depthZeroToOne ? perspective(fovY, aspect, nearPlane, farPlane)
                          : perspectiveGL(fovY, aspect, nearPlane, farPlane);
}

Vec2 Camera::project(const Vec3& world) const
{
    const Vec3 ndc = viewProjection().transformPoint(world);
    return {(ndc.x + 1) * 0.5 * viewportSize.x, (1 - ndc.y) * 0.5 * viewportSize.y};
}

double Camera::depthOf(const Vec3& world) const
{
    return (world - eye()).dot(forward());
}

Ray Camera::rayAt(Vec2 screen) const
{
    const double nx = screen.x / std::max(viewportSize.x, 1.0) * 2 - 1;
    const double ny = 1 - screen.y / std::max(viewportSize.y, 1.0) * 2;
    if (projection == Projection::Orthographic) {
        const Vec3 origin = eye() + right() * (nx * orthoWidth() / 2) + up() * (ny * orthoHeight / 2);
        return {origin, forward()};
    }
    const double aspect = viewportSize.x / std::max(viewportSize.y, 1.0);
    const double t = std::tan(fovY / 2);
    const Vec3 dir = (forward() + right() * (nx * t * aspect) + up() * (ny * t)).normalized();
    return {eye(), dir};
}

double Camera::pixelSize(const Vec3& at) const
{
    if (projection == Projection::Orthographic)
        return orthoHeight / std::max(viewportSize.y, 1.0);
    const double depth = std::max(depthOf(at), 1e-6);
    return 2 * depth * std::tan(fovY / 2) / std::max(viewportSize.y, 1.0);
}

Vec3 Camera::pointOnViewPlane(Vec2 screen, const Vec3& planePoint) const
{
    const Ray ray = rayAt(screen);
    const Vec3 n = forward();
    const double denom = ray.direction.dot(n);
    if (std::abs(denom) < 1e-12)
        return planePoint;
    const double t = (planePoint - ray.origin).dot(n) / denom;
    return ray.at(t);
}

void Camera::orbit(double dxPixels, double dyPixels, const Vec3& pivot)
{
    const double dYaw = -dxPixels * kOrbitRadiansPerPixel;
    const double newPitch = std::clamp(pitch + dyPixels * kOrbitRadiansPerPixel, -kMaxPitch, kMaxPitch);
    const double dPitch = newPitch - pitch;

    // Rotate the target around the pivot by the same rotation the view undergoes,
    // so the pivot stays fixed on screen.
    Vec3 offset = target - pivot;
    offset = rotate(offset, right(), -dPitch);
    offset = rotate(offset, {0, 0, 1}, dYaw);
    target = pivot + offset;
    pitch = newPitch;
    yaw = wrapAngle(yaw + dYaw);
}

void Camera::pan(Vec2 fromScreen, Vec2 toScreen)
{
    const Vec3 a = pointOnViewPlane(fromScreen, target);
    const Vec3 b = pointOnViewPlane(toScreen, target);
    target -= (b - a);
}

void Camera::zoomAt(Vec2 screen, double factor)
{
    factor = std::clamp(factor, 0.01, 100.0);
    const Vec3 before = pointOnViewPlane(screen, target);
    if (projection == Projection::Orthographic)
        orthoHeight = std::clamp(orthoHeight * factor, kMinOrthoHeight, kMaxOrthoHeight);
    else
        distance = std::clamp(distance * factor, 1e-3, 1e7);
    const Vec3 after = pointOnViewPlane(screen, target);
    target += before - after;
}

void Camera::fit(const Vec3& boxMin, const Vec3& boxMax)
{
    const Vec3 center = (boxMin + boxMax) * 0.5;
    const double radius = std::max((boxMax - boxMin).length() * 0.5, 1.0);
    target = center;
    const double aspect = viewportSize.x / std::max(viewportSize.y, 1.0);
    const double margin = 1.9; // leave room for manipulators and growth
    orthoHeight = 2 * radius * margin / std::min(aspect, 1.0);
    distance = radius * margin / std::sin(fovY / 2) / std::min(aspect, 1.0);
}

void Camera::setStandardView(StandardView view)
{
    switch (view) {
    case StandardView::Front: yaw = -kPi / 2; pitch = 0; break;
    case StandardView::Back: yaw = kPi / 2; pitch = 0; break;
    case StandardView::Right: yaw = 0; pitch = 0; break;
    case StandardView::Left: yaw = kPi; pitch = 0; break;
    case StandardView::Top: yaw = -kPi / 2; pitch = kPi / 2; break;
    case StandardView::Bottom: yaw = -kPi / 2; pitch = -kPi / 2; break;
    case StandardView::Isometric: yaw = -kPi / 4; pitch = std::atan(1 / std::sqrt(2.0)); break;
    }
}

Camera Camera::interpolate(const Camera& a, const Camera& b, double t)
{
    // Smoothstep easing for calm transitions.
    const double s = t * t * (3 - 2 * t);
    Camera c = b;
    c.target = a.target + (b.target - a.target) * s;
    c.yaw = wrapAngle(a.yaw + wrapAngle(b.yaw - a.yaw) * s);
    c.pitch = a.pitch + (b.pitch - a.pitch) * s;
    // Interpolate zoom geometrically so it feels uniform.
    c.orthoHeight = a.orthoHeight * std::pow(b.orthoHeight / a.orthoHeight, s);
    c.distance = a.distance * std::pow(b.distance / a.distance, s);
    return c;
}

} // namespace os
