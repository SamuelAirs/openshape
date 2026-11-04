// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// Private to src/measure's Scan stages: small dense linear algebra (3 x 3
// matrices, rotations, a little linear solver).

#include "core/Math.h"

#include <array>
#include <cmath>
#include <vector>

namespace os::measure::scanmath {

using Mat3 = std::array<double, 9>; // row-major

inline Mat3 identity3() { return {1, 0, 0, 0, 1, 0, 0, 0, 1}; }

inline Mat3 mul(const Mat3& a, const Mat3& b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    return r;
}

inline Mat3 transpose(const Mat3& a) { return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]}; }

inline Vec3 mul(const Mat3& a, const Vec3& v)
{
    return {a[0] * v.x + a[1] * v.y + a[2] * v.z, a[3] * v.x + a[4] * v.y + a[5] * v.z, a[6] * v.x + a[7] * v.y + a[8] * v.z};
}

inline double det(const Mat3& m)
{
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

inline bool invert(const Mat3& m, Mat3& r)
{
    const double d = det(m);
    if (!(std::abs(d) > 1e-300) || !std::isfinite(d))
        return false;
    r[0] = (m[4] * m[8] - m[5] * m[7]) / d;
    r[1] = (m[2] * m[7] - m[1] * m[8]) / d;
    r[2] = (m[1] * m[5] - m[2] * m[4]) / d;
    r[3] = (m[5] * m[6] - m[3] * m[8]) / d;
    r[4] = (m[0] * m[8] - m[2] * m[6]) / d;
    r[5] = (m[2] * m[3] - m[0] * m[5]) / d;
    r[6] = (m[3] * m[7] - m[4] * m[6]) / d;
    r[7] = (m[1] * m[6] - m[0] * m[7]) / d;
    r[8] = (m[0] * m[4] - m[1] * m[3]) / d;
    return true;
}

inline Mat3 fromColumns(const Vec3& a, const Vec3& b, const Vec3& c) { return {a.x, b.x, c.x, a.y, b.y, c.y, a.z, b.z, c.z}; }
inline Vec3 column(const Mat3& m, int j) { return {m[j], m[3 + j], m[6 + j]}; }

// The nearest rotation (orthogonal polar factor) of a matrix with positive
// determinant: Newton's iteration X <- (X + X^-T) / 2, which converges
// quadratically. The input's columns are first scaled to unit length.
inline Mat3 nearestRotation(Mat3 x)
{
    for (int it = 0; it < 30; ++it) {
        Mat3 inv{};
        if (!invert(x, inv))
            break;
        const Mat3 invT = transpose(inv);
        double change = 0;
        for (int k = 0; k < 9; ++k) {
            const double next = 0.5 * (x[k] + invT[k]);
            change = std::max(change, std::abs(next - x[k]));
            x[k] = next;
        }
        if (change < 1e-15)
            break;
    }
    return x;
}

// Rotation by the rotation vector w (axis * angle, radians).
inline Mat3 rodrigues(const Vec3& w)
{
    const double theta = w.length();
    if (theta < 1e-12)
        return {1, -w.z, w.y, w.z, 1, -w.x, -w.y, w.x, 1};
    const Vec3 k = w / theta;
    const double c = std::cos(theta), s = std::sin(theta), v = 1 - c;
    return {c + k.x * k.x * v,       k.x * k.y * v - k.z * s, k.x * k.z * v + k.y * s,
            k.y * k.x * v + k.z * s, c + k.y * k.y * v,       k.y * k.z * v - k.x * s,
            k.z * k.x * v - k.y * s, k.z * k.y * v + k.x * s, c + k.z * k.z * v};
}

// Solves the n x n system a x = b (a row-major, n*n) by Gaussian elimination
// with partial pivoting. false when singular.
inline bool solve(std::vector<double> a, std::vector<double> b, int n, std::vector<double>& x)
{
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(a[row * n + col]) > std::abs(a[pivot * n + col]))
                pivot = row;
        if (!(std::abs(a[pivot * n + col]) > 1e-300))
            return false;
        if (pivot != col) {
            for (int k = 0; k < n; ++k)
                std::swap(a[col * n + k], a[pivot * n + k]);
            std::swap(b[col], b[pivot]);
        }
        for (int row = col + 1; row < n; ++row) {
            const double f = a[row * n + col] / a[col * n + col];
            if (f == 0)
                continue;
            for (int k = col; k < n; ++k)
                a[row * n + k] -= f * a[col * n + k];
            b[row] -= f * b[col];
        }
    }
    x.assign(n, 0.0);
    for (int row = n - 1; row >= 0; --row) {
        double v = b[row];
        for (int k = row + 1; k < n; ++k)
            v -= a[row * n + k] * x[k];
        x[row] = v / a[row * n + row];
    }
    for (double v : x)
        if (!std::isfinite(v))
            return false;
    return true;
}

} // namespace os::measure::scanmath
