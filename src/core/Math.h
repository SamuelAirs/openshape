#pragma once

#include <array>
#include <cmath>
#include <optional>

// Small double-precision linear algebra used by the camera, picking and
// manipulators. Deliberately minimal; the kernel has its own gp_* types and
// the renderer converts to float only at upload time.
namespace os {

inline constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const
    {
        const double l = length();
        return l > 0 ? *this / l : Vec3{};
    }
};

inline constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }

struct Vec4 {
    double x = 0, y = 0, z = 0, w = 0;
};

// Column-major 4x4 matrix (m[col][row]), matching OpenGL/Vulkan conventions.
struct Mat4 {
    std::array<std::array<double, 4>, 4> m{};

    static Mat4 identity()
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            r.m[i][i] = 1.0;
        return r;
    }

    double& at(int row, int col) { return m[col][row]; }
    double at(int row, int col) const { return m[col][row]; }

    Mat4 operator*(const Mat4& o) const
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                double s = 0;
                for (int k = 0; k < 4; ++k)
                    s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }

    Vec4 operator*(const Vec4& v) const
    {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
                at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w};
    }

    Vec3 transformPoint(const Vec3& p) const
    {
        const Vec4 r = (*this) * Vec4{p.x, p.y, p.z, 1.0};
        return r.w != 0 ? Vec3{r.x / r.w, r.y / r.w, r.z / r.w} : Vec3{r.x, r.y, r.z};
    }

    Vec3 transformDirection(const Vec3& d) const
    {
        const Vec4 r = (*this) * Vec4{d.x, d.y, d.z, 0.0};
        return {r.x, r.y, r.z};
    }

    std::optional<Mat4> inverted() const;
};

// Right-handed look-at view matrix.
Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
// Projection matrices map depth to [0,1] (Vulkan/D3D/Metal convention). The
// renderer applies QRhi::clipSpaceCorrMatrix() for OpenGL backends.
Mat4 orthographic(double left, double right, double bottom, double top, double nearPlane, double farPlane);
Mat4 perspective(double verticalFovRadians, double aspect, double nearPlane, double farPlane);

struct Ray {
    Vec3 origin;
    Vec3 direction; // normalized
    Vec3 at(double t) const { return origin + direction * t; }
};

// Möller–Trumbore. Returns the ray parameter of the hit, if any (t >= 0).
std::optional<double> intersectRayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c);

// Parameter t on the (infinite) line p + t*d that is closest to the ray.
// Returns nullopt when the ray is (nearly) parallel to the line.
std::optional<double> closestLineParameterToRay(const Vec3& p, const Vec3& d, const Ray& ray);

// Distance from point q to segment [a,b] in 2D, and the parameter of the
// closest point along the segment (0..1).
double distanceToSegment2D(Vec2 q, Vec2 a, Vec2 b, double* parameter = nullptr);

} // namespace os
