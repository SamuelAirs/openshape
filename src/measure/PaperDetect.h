// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "measure/ScanMask.h"

#include <array>
#include <optional>

// Finding the sheet of paper in a photo without Apple Vision (the desktop,
// the developer tool tools/scan_photos, and a fallback): the largest bright,
// colourless region, its convex hull (an object standing on the sheet and
// hiding part of its edge does not matter), the four-sided shape that best
// fills the hull, and each side refitted to the region's edge pixels along it.
namespace os::measure {

// The paper's corners in pixels, in order around the sheet; nullopt when no
// bright four-sided region of a reasonable size is found.
std::optional<std::array<Vec2, 4>> detectPaper(const RgbImage& image);

} // namespace os::measure
