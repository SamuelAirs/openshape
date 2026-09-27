// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/ContactShadow.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace os::interact {

namespace {
constexpr double kStrength = 0.22; // opacity under a body resting on the ground
}

double ContactFootprint::strength() const
{
    if (empty() || size <= 0)
        return 0;
    return kStrength * std::clamp(1.0 - height / (0.5 * size), 0.0, 1.0);
}

double ContactFootprint::blur() const
{
    return std::clamp(0.08 * size, 0.5, 6.0) + 0.5 * height;
}

ContactFootprint contactFootprint(const geom::Mesh& mesh)
{
    ContactFootprint footprint;
    const std::size_t vertices = mesh.vertexCount();
    if (vertices == 0 || mesh.normals.size() != mesh.positions.size())
        return footprint;
    double minZ = std::numeric_limits<double>::infinity(), maxZ = -minZ;
    Vec3 lo{minZ, minZ, minZ}, hi{maxZ, maxZ, maxZ};
    for (std::size_t i = 0; i < vertices; ++i) {
        const Vec3 p = mesh.vertex(i);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const double extent = (hi - lo).length();
    const double tolerance = std::max(1e-6 * extent, 1e-6); // mm
    if (lo.z < -tolerance)
        return footprint; // through the ground: no contact to show

    // Triangles lying at the lowest height and facing straight down.
    Vec3 footLo{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), 0};
    Vec3 footHi{-footLo.x, -footLo.y, 0};
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        bool down = true;
        for (int k = 0; k < 3 && down; ++k) {
            const std::size_t v = mesh.indices[t + std::size_t(k)];
            down = mesh.normals[3 * v + 2] < -0.999 && mesh.vertex(v).z < lo.z + tolerance;
        }
        if (!down)
            continue;
        const Vec3 a = mesh.vertex(mesh.indices[t]), b = mesh.vertex(mesh.indices[t + 1]), c = mesh.vertex(mesh.indices[t + 2]);
        footprint.area += 0.5 * std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
        for (const Vec3& p : {a, b, c}) {
            footLo = {std::min(footLo.x, p.x), std::min(footLo.y, p.y), 0};
            footHi = {std::max(footHi.x, p.x), std::max(footHi.y, p.y), 0};
        }
        footprint.indices.insert(footprint.indices.end(), {mesh.indices[t], mesh.indices[t + 1], mesh.indices[t + 2]});
    }
    if (footprint.indices.empty())
        return footprint;
    footprint.height = std::max(lo.z, 0.0);
    footprint.size = std::max(footHi.x - footLo.x, footHi.y - footLo.y);
    if (footprint.height > 0.5 * footprint.size)
        return ContactFootprint{}; // too high to read as touching
    return footprint;
}

} // namespace os::interact
