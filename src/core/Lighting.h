// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Camera.h"

namespace os {

// The viewport's studio lighting: one model for the GPU (the renderer passes
// these numbers to render/shaders/mesh.frag, which evaluates the same
// formula) and the CPU (project thumbnails, tests).
//
// - Up is up. Ambient light comes from a sky above and a darker ground below
//   (a hemisphere light), and the key light shines from above at a fixed
//   elevation, so faces turned up are the lightest and faces turned down the
//   darkest from every angle, as under a real lamp.
// - The key light's direction around the vertical turns with the camera: it
//   stands 50 degrees to the viewer's left (like the lights of a
//   photographer's turntable), so the two sides of a box seen at once always
//   get clearly different shades, the one on the left lighter. With lights
//   fixed in the world, some turn of any box shows two sides in the same
//   shade, and looking at a part from behind leaves the sides facing the
//   viewer in shadow.
// - The ambient keeps every face readable (nothing goes black), a faint fill
//   from the viewer separates faces turned towards the viewer from grazing
//   ones, and a soft specular highlight shows how curved faces bend.
// Measured on the rendered window by the acceptance scenario "shading".
struct StudioLighting {
    double sky = 0.72;            // ambient of a face turned straight up
    double ground = 0.44;         // ambient of a face turned straight down
    double key = 0.34;            // key light strength
    double fill = 0.05;           // fill from the viewer
    double specular = 0.08;       // highlight strength (added, not multiplied by the colour)
    double shininess = 40.0;      // highlight exponent
    double keyElevation = 0.8727; // radians above the horizon (50 degrees)
    double keyAzimuth = 0.8727;   // radians to the viewer's left of the view direction (50 degrees)

    // World direction towards the key light when looking through `camera`
    // (depends on its yaw only: well defined in the top and bottom views).
    Vec3 keyDirection(const Camera& camera) const;

    struct Shade {
        double diffuse = 0;  // multiplies the surface colour
        double specular = 0; // added to it
    };
    // Shade of a surface with the unit world normal `normal` seen from the
    // unit direction `toViewer` (from the surface towards the eye). Two-sided:
    // a normal turned away from the viewer is flipped first.
    Shade shade(Vec3 normal, const Vec3& toViewer, const Vec3& keyDirection) const;
};

} // namespace os
