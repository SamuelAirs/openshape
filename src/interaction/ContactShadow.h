// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "geometry/Mesh.h"

#include <cstdint>
#include <vector>

namespace os::interact {

// What a body rests on the ground with, for its soft contact shadow: the
// triangles of its lowest faces that face straight down (their exact
// outline, so an L-shaped part casts an L), drawn flattened onto the ground
// and blurred (render/shaders/shadow.vert).
struct ContactFootprint {
    std::vector<std::uint32_t> indices; // triangles of the mesh, 3 per triangle
    double height = 0;                  // of those faces above the ground (0: resting on it)
    double size = 0;                    // the footprint's larger extent, mm
    double area = 0;                    // mm^2

    bool empty() const { return indices.empty(); }
    // How dark the shadow is where the body touches the ground (0..1): full
    // when resting on it, fading out as it rises to half its size.
    double strength() const;
    // How far (mm) the shadow fades out around the footprint: a small part of
    // its size, softer the higher the body floats.
    double blur() const;
};

// Empty when the body reaches below the ground, floats higher than half its
// footprint above it, or has no face turned straight down at its lowest
// (a cylinder lying on its side).
ContactFootprint contactFootprint(const geom::Mesh& mesh);

} // namespace os::interact
