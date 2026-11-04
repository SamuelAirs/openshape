// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "measure/Homography.h"
#include "measure/ScanTypes.h"

#include <cstdint>
#include <vector>

// Scan, step 3 (docs/SCAN.md): which pixels of a photo show the object.
// Three states per pixel (ScanTypes.h): object, background (the paper, its
// shadows, or what a subject mask calls background) and unknown (everything
// the photo cannot vouch for: the table and the room around the paper
// without a subject mask, the paper's very edge). Carving removes volume only
// where photos are sure.
namespace os::measure {

// An 8-bit RGB image (3 bytes a pixel, rows packed).
struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb;
};

// The photo shrunk (area-averaged) so its longer side is at most `maxSide`
// pixels; never enlarged. `bytesPerPixel` is 3 or 4 (the fourth byte, alpha,
// is ignored); `bgr` swaps the first and third bytes into RGB order.
RgbImage downsampleRgb(const std::uint8_t* data, int width, int height, int stride, int bytesPerPixel, bool bgr,
                       int maxSide);

// An 8-bit mask of any size sampled (bilinearly) at `width` x `height`.
std::vector<std::uint8_t> resampleMask(const std::uint8_t* data, int width, int height, int stride, int outWidth,
                                       int outHeight);

// Where a pixel looks onto the table (z = 0): false above the horizon.
bool pixelToPaperPlane(const ScanCamera& camera, Vec2 pixel, Vec2& planeMm);

struct MaskOptions {
    // Shadows: paper down to this fraction of its lit brightness, with the
    // paper's colour, still counts as paper.
    double shadowFloor = 0.35;
    // The paper's edge (mm inside it) is left unknown: its pixels mix paper
    // and table.
    double edgeMm = 3;
    // Pixels a subject mask calls object but whose colour is clearly lit
    // paper are background (the mask's edges bleed).
    bool trustLitPaperOverMask = true;
};

// The paper's colour as a smooth function of its position (a plane per
// channel over the sheet, in mm), fitted robustly to a band just inside the
// sheet's edge (objects and shadows reaching the band are left out).
struct PaperColourModel {
    std::array<std::array<double, 3>, 3> coefficients{}; // per channel: a + b x + c y
    int samples = 0;
    bool valid = false;
    std::array<double, 3> at(Vec2 planeMm) const;
};
PaperColourModel fitPaperColour(const RgbImage& image, const ScanCamera& camera, const Reference& paper);

// The photo's three-state mask (kMaskObject / kMaskBackground /
// kMaskUnknown, width * height). `subjectMask` (optional, already at the
// image's size, >= 128 = subject) is a segmentation from elsewhere (Apple
// Vision's foreground mask): it decides outside the paper, and is combined
// with the paper's colour on it. Without it, colour alone decides on the
// paper and everything off the paper is unknown.
std::vector<std::uint8_t> segmentPhoto(const RgbImage& image, const ScanCamera& camera, const Reference& paper,
                                       const std::uint8_t* subjectMask, const MaskOptions& options = {});

} // namespace os::measure
