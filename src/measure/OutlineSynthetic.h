// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "measure/Homography.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// Photos for testing Find outline (tests/test_measure.cpp, the acceptance
// run's measureautooutline): flat parts on a sheet of paper on a table, a
// playing card beside them, seen through a perspective map, with soft
// shadows, light falling off across the photo, a little blur and noise.
namespace os::measure {

using Colour = std::array<std::uint8_t, 3>;

// A part as a signed distance (mm, negative inside) on the surface.
using ShapeDistance = std::function<double(Vec2)>;

double boxDistance(Vec2 p, Vec2 center, Vec2 half, double angle = 0, double cornerRadius = 0);
double circleDistance(Vec2 p, Vec2 center, double radius);
inline double unite(double a, double b) { return std::min(a, b); }
inline double subtract(double a, double b) { return std::max(a, -b); }

struct SyntheticPart {
    ShapeDistance shape;
    Colour colour{60, 62, 68};
};

struct SyntheticScene {
    Colour table{150, 112, 78};
    Colour paper{238, 238, 232};
    Vec2 paperMin{-40, -60}; // the sheet (mm, axis-aligned on the surface)
    Vec2 paperMax{230, 130};
    std::array<Vec2, 4> card{Vec2{0, 0}, Vec2{88.9, 0}, Vec2{88.9, 63.5}, Vec2{0, 63.5}}; // mm, in order
    Colour cardColour{250, 250, 252};
    std::vector<SyntheticPart> parts;
    double shadow = 0.35;         // how much a part's shadow darkens (0..1)
    Vec2 shadowOffset{1.2, 1.6};  // mm
    double shadowSoftness = 2.5;  // mm across its edge
    double falloff = 0.12;        // the light, dimmer by this across the photo
    double blurPx = 0.7;          // the lens (Gaussian sigma, photo pixels)
    double noise = 3.0;           // per channel (sigma, levels)
    unsigned seed = 7;
};

// The photo (`width` x `height`, 4 bytes a pixel: R, G, B, 255) of the
// scene as `surfaceToImage` maps the surface (mm) into it.
std::vector<std::uint8_t> renderSyntheticPhoto(const SyntheticScene& scene, const Homography& surfaceToImage, int width, int height);

} // namespace os::measure
