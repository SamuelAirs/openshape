// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// Synthetic photos of known objects for Scan (tests/test_scan.cpp, the
// stereo stage's tests, the acceptance run and `--demo scan`, which show the
// whole pipeline without a camera): a sheet of paper on a dark table with an object
// standing on it, ray cast from cameras walking around, with soft shadows,
// an uneven light on the paper, optional procedural texture (solid noise on
// the object, a faint grain on the paper), a subject mask, and the paper's
// corners handed over from a shuffled start and direction like Apple Vision
// or the owner's handles would.

#include "measure/Homography.h"
#include "measure/Scan.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace os::measure::synthetic {

// A part of the object: an axis-aligned box (turned by `turnDeg` about the
// vertical through `center`), or an upright cylinder.
struct Part {
    enum class Kind { Box, Cylinder };
    Kind kind = Kind::Box;
    Vec3 center;          // box: its center; cylinder: its base center
    Vec3 size;            // box: x, y, z extents
    double radius = 0;    // cylinder
    double height = 0;    // cylinder
    double turnDeg = 0;   // box
};

struct Object {
    std::string name;
    std::vector<Part> parts; // their union (they may touch, not overlap, so volumes add)
    double volume = 0;       // mm³
    Vec3 min, max;           // its bounding box
};

// Objects standing on the paper (z = 0), placed at `at` (mm, the paper's frame).
Object boxObject(Vec3 size, Vec2 at = {}, double turnDeg = 0);
Object cylinderObject(double diameter, double height, Vec2 at = {});
// An L: a 50 x 30 x 15 slab with a 15 x 30 x 25 upright on its +x end.
Object lBlockObject(Vec2 at = {});
// A U-channel open upward: a 50 x 30 x 8 base with two 8 x 30 x 22 walls
// (outer 50 x 30 x 30; the slot between the walls is 34 wide, 22 deep).
Object uChannelObject(Vec2 at = {});

struct Scene {
    measure::Reference paper = measure::a4Reference();
    Object object;
    Vec3 albedo{0.75, 0.42, 0.28};    // the object's colour (linear, 0..1)
    double textureStrength = 0;       // object: albedo * (1 +- strength) from solid noise
    double textureScaleMm = 4;
    double paperGrain = 0.015;        // the paper's faint texture
    double paperGradient = 0.12;      // the light falling off across the sheet
    Vec3 table{0.16, 0.12, 0.10};
    Vec3 lightDirection{0.35, 0.2, 0.9}; // toward the light (normalized when used)
    double ambient = 0.42;
    bool shadows = true;
    double softness = 0.05;           // radians of the light's spread
    double noise = 1.5;               // sensor noise (8-bit levels)
};

struct CameraSpec {
    double azimuthDeg = 0;
    double elevationDeg = 45;
    double distanceMm = 480;
    Vec3 target{0, 0, 15};
    double focal35mm = 26;
    int width = 1200;
    int height = 900;
};

// `count` cameras around, azimuths evenly over `spanDeg` from `startDeg`,
// elevations alternating between `elevLow` and `elevHigh`.
std::vector<CameraSpec> ring(int count, double elevLow, double elevHigh, double startDeg = 10, double spanDeg = 360,
                             double distanceMm = 480, double focal35mm = 26);

struct Photo {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;     // width * height * 4
    std::vector<std::uint8_t> mask;     // width * height, 255 on the object
    measure::ScanCamera camera;         // the truth
    std::array<Vec2, 4> corners{};      // shuffled start and direction, with noise
    int trueFirstCorner = 0;            // the input corner on the paper's (-W/2, -H/2)
    bool trueReversed = false;
    double focal35mm = 0;
    bool paperInView = false;           // all four corners inside the image
};

// Renders the photos (in parallel). `seed` drives the corner shuffle and noise.
std::vector<Photo> render(const Scene& scene, const std::vector<CameraSpec>& cameras, unsigned seed = 1,
                          double cornerNoisePx = 0.3);

// The photos as the engine takes them (borrowing their pixels).
std::vector<measure::ScanPhoto> asScanPhotos(const std::vector<Photo>& photos, bool withMask, bool withFocal);

// The camera of a spec (the truth the renderer uses).
measure::ScanCamera cameraFor(const CameraSpec& spec);

} // namespace os::measure::synthetic
