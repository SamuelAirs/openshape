// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// The data the Scan stages hand each other (docs/SCAN.md): calibrated
// cameras with the images they describe, the carved volume as a signed
// field on a voxel grid, and the triangle mesh made from it. Kept apart from
// the stages so a later stage (multi-view stereo, between carving and the
// surface) can consume and produce them without depending on the rest.
//
// World frame (all stages): millimeters, the paper's center at the origin, x
// along its long side, y along its short side, z up toward the cameras; the
// paper lies at z = 0.
namespace os::measure {

// A pinhole camera (no lens distortion: phone photos come undistorted).
struct ScanCamera {
    double fx = 0, fy = 0, cx = 0, cy = 0;
    int width = 0, height = 0;
    std::array<double, 9> R{1, 0, 0, 0, 1, 0, 0, 0, 1}; // world -> camera, row-major
    Vec3 t;                                             // x_cam = R * X_world + t
    // pixel = (fx * x / z + cx, fy * y / z + cy); z > 0 in front. Pixel
    // (0, 0) is the top-left pixel's top-left corner (its center is (0.5, 0.5)).

    Vec3 toCamera(Vec3 world) const;
    Vec3 center() const;                // the camera's position in the world
    Vec2 project(Vec3 world) const;     // meaningful only where depth(world) > 0
    double depth(Vec3 world) const;     // z in the camera frame
    Vec3 viewDirection() const;         // the optical axis in the world
};

// A calibrated photo, downsampled to the size the stages work at.
struct ScanView {
    ScanCamera camera;
    std::vector<std::uint8_t> rgb;  // width * height * 3, row-major, the image the camera describes
    std::vector<std::uint8_t> mask; // width * height: kMaskObject, kMaskBackground or kMaskUnknown
    int photoIndex = -1;            // which input photo it came from
};

inline constexpr std::uint8_t kMaskObject = 255;
inline constexpr std::uint8_t kMaskBackground = 0;
inline constexpr std::uint8_t kMaskUnknown = 128;

// A scalar field sampled at voxel centers: sample (i, j, k) sits at
// origin + (i + 0.5, j + 0.5, k + 0.5) * voxel. The grid's bottom face lies
// on the paper (origin.z = 0).
struct ScanGrid {
    Vec3 origin;
    double voxel = 1;
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> field; // nx * ny * nz, x fastest; > 0 inside, < 0 outside, iso level 0

    std::size_t index(int i, int j, int k) const { return (std::size_t(k) * ny + j) * nx + i; }
    float at(int i, int j, int k) const { return field[index(i, j, k)]; }
    Vec3 sampleCenter(int i, int j, int k) const
    {
        return origin + Vec3{(i + 0.5) * voxel, (j + 0.5) * voxel, (k + 0.5) * voxel};
    }
    bool empty() const { return nx <= 0 || ny <= 0 || nz <= 0 || field.empty(); }
};

// A closed, outward-oriented triangle mesh (counter-clockwise seen from outside).
struct ScanMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

// Called now and then by the long stages with the fraction done (0..1);
// returning false cancels the stage (it then fails with ErrorCode::NoEffect
// and the message "Cancelled.").
using ScanProgress = std::function<bool(double fraction)>;

} // namespace os::measure
