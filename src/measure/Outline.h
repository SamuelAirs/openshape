// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <vector>

// Points measured on a flat surface (tapped on a straightened photo, placed
// with the camera in AR), as a path or a closed outline: its sides, its
// corners' angles, its perimeter and area, and where it sits so a sketch
// made from it lands around the origin.
namespace os::measure {

struct OutlineReport {
    std::vector<double> sideLengths; // mm, from point i to i+1 (closed: the last back to the first)
    std::vector<double> cornerAngles; // degrees, the inside angle at each point (closed outlines; an open path's ends have none)
    double perimeter = 0;
    double area = 0; // mm², closed outlines with three or more points
    Vec2 boundsMin;
    Vec2 boundsMax;
};

OutlineReport describeOutline(const std::vector<Vec2>& points, bool closed);

// The points moved so their bounding box is centered on the origin, and
// mirrored top to bottom when `flipY` (a photo's y runs down the image; a
// sketch's y runs up).
std::vector<Vec2> centeredOnOrigin(const std::vector<Vec2>& points, bool flipY);

} // namespace os::measure
