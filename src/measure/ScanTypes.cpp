// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScanTypes.h"

namespace os::measure {

Vec3 ScanCamera::toCamera(Vec3 w) const
{
    return {R[0] * w.x + R[1] * w.y + R[2] * w.z + t.x, R[3] * w.x + R[4] * w.y + R[5] * w.z + t.y,
            R[6] * w.x + R[7] * w.y + R[8] * w.z + t.z};
}

Vec3 ScanCamera::center() const
{
    // C = -R^T t
    return {-(R[0] * t.x + R[3] * t.y + R[6] * t.z), -(R[1] * t.x + R[4] * t.y + R[7] * t.z),
            -(R[2] * t.x + R[5] * t.y + R[8] * t.z)};
}

Vec2 ScanCamera::project(Vec3 world) const
{
    const Vec3 c = toCamera(world);
    return {fx * c.x / c.z + cx, fy * c.y / c.z + cy};
}

double ScanCamera::depth(Vec3 world) const { return R[6] * world.x + R[7] * world.y + R[8] * world.z + t.z; }

Vec3 ScanCamera::viewDirection() const { return {R[6], R[7], R[8]}; }

} // namespace os::measure
