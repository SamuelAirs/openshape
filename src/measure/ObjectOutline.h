// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "measure/ScanMask.h"
#include "measure/StrokeFit.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Find outline (docs/MEASURE.md, "Photo"): a flat part lying on plain
// paper of a contrasting colour, photographed beside a playing card, is
// found in the straightened photo (the photo warped to true millimetres) and
// its edge turned into a few lines, arcs and circles: the outer edge and
// each hole (a washer's or a bracket's hole is an inner loop), each with its
// fitting error.
//
// How: the paper's colour is sampled around the card (the card lies on the
// same paper) and modelled as a plane across the photo (light falls off);
// each pixel's difference from it splits into darkening (the paper's own
// colour, only darker: shading) and a change of colour; Otsu's threshold on
// that difference separates part from paper, morphology closes small gaps
// and drops specks, and the largest piece that does not run off the photo
// (or the one tapped) is the part. Its boundary (pixel cracks) is moved to
// the exact edge (half way between the paper's and the part's colours,
// along the normal: sub-pixel), simplified (Douglas-Peucker) and fitted
// with lines and arcs: corners stay sharp (neighbouring lines meet at their
// intersection), round parts become arcs or a circle.
//
// Shadows: a soft shadow only darkens the paper, so it adds little to the
// difference (a third darker counts a third as much as black would); the
// threshold sits above it and the edge is placed at half the step from the
// local, shadowed paper. A hard shadow darker than about half the paper's
// brightness next to a dark part does join it: light from above or diffuse
// light (daylight, several lamps), not one low lamp, or a light part on dark
// paper, where shadows hardly show.
namespace os::measure {

// The straightened photo as warpToView writes it (4 bytes a pixel, the
// fourth alpha: 0 outside the photo) as RGB plus where it has the photo.
// `bgra`: the bytes are B, G, R, A (a QImage's ARGB32 in memory).
RgbImage rgbFromRgba(const std::uint8_t* data, int width, int height, int stride, bool bgra, std::vector<std::uint8_t>* valid);

struct ObjectOutlineOptions {
    double mmPerPixel = 0.2;  // the straightened photo's scale
    double toleranceMm = 0.2; // the fitted lines and arcs stay this close to the edge
    // Quadrilaterals (pixels) to leave out: the playing card, with a margin
    // (its shadow, its corners' placement).
    std::vector<std::array<Vec2, 4>> exclude;
    double excludeMarginMm = 2.5;
    // Only inside this quadrilateral (pixels), inset: a sheet of paper used
    // as the reference, with the part lying on it.
    std::optional<std::array<Vec2, 4>> searchArea;
    double searchInsetMm = 4;
    // A tapped pixel on the part: that piece, whatever its size.
    std::optional<Vec2> seed;
    double minObjectMm2 = 25; // smaller pieces are dirt
    double minHoleMm2 = 3;    // smaller holes are glints and dust (filled)
};

struct OutlineLoop {
    bool hole = false;
    bool circle = false; // the whole loop is one circle (circleShape)
    Circle circleShape;  // pixels
    // Lines and arcs end to end (pixels), the last ending at the first's
    // start; empty for a circle.
    std::vector<FittedSegment> segments;
    std::vector<Vec2> edge; // the sub-pixel edge the pieces were fitted to (pixels)
    double maxErrorMm = 0;  // the farthest edge point from the pieces
    double areaMm2 = 0;     // the loop's own area (positive)
};

struct ObjectOutline {
    bool found = false;
    std::string message;         // why nothing was found (found == false)
    std::vector<OutlineLoop> loops; // the outer edge first, then the holes
    // How it went (the log): the paper's colour, the part's, the threshold.
    std::array<double, 3> paper{};
    std::array<double, 3> object{};
    double threshold = 0;
    double areaMm2 = 0; // the part's area (holes taken out)
};

ObjectOutline findObjectOutline(const RgbImage& image, const std::vector<std::uint8_t>& valid, const ObjectOutlineOptions& options);

// ---- The pieces of a fitted loop, used alone (tests, Trace-like fitting).
struct LoopFitOptions {
    double tolerance = 1;    // the largest distance from the edge to the pieces (the edge's units)
    double minArcRadius = 3; // a tighter arc between two lines is their corner
    double trim = 1.5;       // edge points this near a piece's ends don't steer its fit (corners blur)
    double minPiece = 0;     // shorter pieces go, their neighbours meet (at least two tolerances)
};
// A closed edge (points in order, not repeated at the end) as one circle
// (loop.circle) or lines and arcs; maxErrorMm is left in the edge's units.
OutlineLoop fitClosedEdge(const std::vector<Vec2>& edge, const LoopFitOptions& options);

} // namespace os::measure
