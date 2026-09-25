// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Exchange.h"

#include <filesystem>
#include <vector>

// 3MF export for slicers: a ZIP package with one mesh object per body, in
// millimeters. Meshes are welded so every edge is shared by exactly two
// triangles (closed, manifold) when the source solid is closed.
namespace os::io {

struct ThreeMfOptions {
    double linearDeflection = 0.01; // mm; well below a nozzle or resin pixel
    double angularDeflection = 0.2; // radians
};

Status export3mf(const std::vector<geom::NamedShape>& shapes, const std::filesystem::path& path,
                 const ThreeMfOptions& options = {});

// Exposed for tests: the mesh that export3mf writes for one shape.
struct WeldedMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};
WeldedMesh weldedMesh(const geom::Shape& shape, const ThreeMfOptions& options = {});

} // namespace os::io
