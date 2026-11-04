// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScanTypes.h"

namespace os::measure {

namespace {

Vec3 rotate(const std::array<double, 9>& R, Vec3 p)
{
    return {R[0] * p.x + R[1] * p.y + R[2] * p.z, R[3] * p.x + R[4] * p.y + R[5] * p.z,
            R[6] * p.x + R[7] * p.y + R[8] * p.z};
}

} // namespace

Vec3 ScanCamera::center() const
{
    // C = -R^T t.
    return {-(R[0] * t.x + R[3] * t.y + R[6] * t.z), -(R[1] * t.x + R[4] * t.y + R[7] * t.z),
            -(R[2] * t.x + R[5] * t.y + R[8] * t.z)};
}

Vec2 ScanCamera::project(Vec3 world) const
{
    const Vec3 c = rotate(R, world) + t;
    return {fx * c.x / c.z + cx, fy * c.y / c.z + cy};
}

double ScanCamera::depth(Vec3 world) const
{
    return R[6] * world.x + R[7] * world.y + R[8] * world.z + t.z;
}

} // namespace os::measure
