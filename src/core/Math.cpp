// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Math.h"

#include <algorithm>

namespace os {

std::optional<Mat4> Mat4::inverted() const
{
    // Gauss-Jordan elimination with partial pivoting.
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = at(r, c);
            a[r][c + 4] = (r == c) ? 1.0 : 0.0;
        }
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                pivot = r;
        if (std::abs(a[pivot][col]) < 1e-300)
            return std::nullopt;
        if (pivot != col)
            for (int c = 0; c < 8; ++c)
                std::swap(a[pivot][c], a[col][c]);
        const double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c)
            a[col][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col)
                continue;
            const double f = a[r][col];
            if (f == 0.0)
                continue;
            for (int c = 0; c < 8; ++c)
                a[r][c] -= f * a[col][c];
        }
    }
    Mat4 result;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result.at(r, c) = a[r][c + 4];
    return result;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
{
    const Vec3 f = (target - eye).normalized();
    const Vec3 s = f.cross(up).normalized();
    const Vec3 u = s.cross(f);
    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -s.dot(eye);
    r.at(1, 3) = -u.dot(eye);
    r.at(2, 3) = f.dot(eye);
    return r;
}

Mat4 orthographic(double left, double right, double bottom, double top, double nearPlane, double farPlane)
{
    Mat4 r = Mat4::identity();
    r.at(0, 0) = 2.0 / (right - left);
    r.at(1, 1) = 2.0 / (top - bottom);
    r.at(2, 2) = -1.0 / (farPlane - nearPlane);
    r.at(0, 3) = -(right + left) / (right - left);
    r.at(1, 3) = -(top + bottom) / (top - bottom);
    r.at(2, 3) = -nearPlane / (farPlane - nearPlane);
    return r;
}

Mat4 perspective(double verticalFovRadians, double aspect, double nearPlane, double farPlane)
{
    const double f = 1.0 / std::tan(verticalFovRadians / 2.0);
    Mat4 r;
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = f;
    r.at(2, 2) = farPlane / (nearPlane - farPlane);
    r.at(2, 3) = (nearPlane * farPlane) / (nearPlane - farPlane);
    r.at(3, 2) = -1.0;
    return r;
}

std::optional<double> intersectRayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 e1 = b - a;
    const Vec3 e2 = c - a;
    const Vec3 p = ray.direction.cross(e2);
    const double det = e1.dot(p);
    // Relative epsilon: triangles are in millimeters and may be tiny or huge.
    const double scale = e1.length() * e2.length();
    if (std::abs(det) <= 1e-12 * scale)
        return std::nullopt;
    const double invDet = 1.0 / det;
    const Vec3 t = ray.origin - a;
    // A hair of tolerance: a ray through the edge two triangles share must
    // hit one of them (rounding could put it outside both, and the pick then
    // went through the face to one behind it: the center of a box's face lies
    // on its diagonal).
    constexpr double kEdge = 1e-9;
    const double u = t.dot(p) * invDet;
    if (u < -kEdge || u > 1.0 + kEdge)
        return std::nullopt;
    const Vec3 q = t.cross(e1);
    const double v = ray.direction.dot(q) * invDet;
    if (v < -kEdge || u + v > 1.0 + kEdge)
        return std::nullopt;
    const double dist = e2.dot(q) * invDet;
    if (dist < 0.0)
        return std::nullopt;
    return dist;
}

std::optional<double> closestLineParameterToRay(const Vec3& p, const Vec3& d, const Ray& ray)
{
    // Minimize |(p + t d) - (o + s r)|.
    const Vec3 w = p - ray.origin;
    const double a = d.dot(d);
    const double b = d.dot(ray.direction);
    const double c = ray.direction.dot(ray.direction);
    const double dd = d.dot(w);
    const double e = ray.direction.dot(w);
    const double denom = a * c - b * b;
    if (std::abs(denom) < 1e-9 * a * c)
        return std::nullopt;
    return (b * e - c * dd) / denom;
}

double distanceToSegment2D(Vec2 q, Vec2 a, Vec2 b, double* parameter)
{
    const Vec2 ab = b - a;
    const double len2 = ab.dot(ab);
    double t = len2 > 0 ? (q - a).dot(ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    if (parameter)
        *parameter = t;
    return (q - (a + ab * t)).length();
}

} // namespace os
