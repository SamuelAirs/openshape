// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "geometry/Mesh.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace os::interact {

// A small picture of the model for project files and the start screen.
// Drawn on the CPU from the display meshes, so it needs no window or GPU
// (saving works the same in automated runs and on every platform) and never
// shows what the view shows mid-operation: shaded bodies with their edges,
// seen from the isometric direction and framed to fill the image, on a
// transparent background, lit like the viewport.
struct ThumbnailImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // premultiplied alpha, rows top to bottom
    bool empty() const { return width == 0 || height == 0; }
};

// Renders the meshes (world coordinates) into a size x size image (2 x 2
// samples per pixel). Empty when there is nothing to draw.
ThumbnailImage renderThumbnail(const std::vector<std::shared_ptr<const geom::Mesh>>& meshes, int size);

} // namespace os::interact
