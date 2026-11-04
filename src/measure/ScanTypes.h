// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace os::measure {

struct ScanCamera {
    double fx = 0, fy = 0, cx = 0, cy = 0;
    int width = 0, height = 0;
    std::array<double, 9> R{}; // world -> camera, row-major
    Vec3 t;                    // x_cam = R * X_world + t; pixel = (fx*x/z + cx, fy*y/z + cy), z > 0 in front
    Vec3 center() const;
    Vec2 project(Vec3 world) const;
    double depth(Vec3 world) const;
};

struct ScanView {
    ScanCamera camera;
    std::vector<std::uint8_t> rgb;  // width*height*3, the image the camera describes
    std::vector<std::uint8_t> mask; // width*height: 255 object, 0 background, 128 unknown
};

struct ScanGrid {
    Vec3 origin;
    double voxel = 1;
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> field; // nx*ny*nz, x fastest; > 0 inside, < 0 outside, iso level 0
};

} // namespace os::measure
