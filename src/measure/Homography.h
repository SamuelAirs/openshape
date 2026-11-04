// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "measure/ScreenScale.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Measuring in a photo (docs/MEASURE.md, "Photo"): a rectangle of known size
// lying in the photo (a playing card, a sheet of A4 or Letter paper, or any
// rectangle whose sides the owner types) fixes the projective map between
// the photo and that flat surface. Everything else on the same surface can
// then be measured in millimeters, whatever angle the photo was taken from,
// and the photo can be shown straightened, as if seen from straight above.
namespace os::measure {

// A projective map of the plane (a 3 x 3 matrix, row-major, h[8] scaled to 1
// where possible).
struct Homography {
    std::array<double, 9> h{1, 0, 0, 0, 1, 0, 0, 0, 1};

    // The map taking from[i] to to[i] (i = 0..3). nullopt when three of the
    // points line up (no unique map).
    static std::optional<Homography> fromQuads(const std::array<Vec2, 4>& from, const std::array<Vec2, 4>& to);

    Vec2 map(Vec2 p) const;
    // The homogeneous weight of p's image: positive in front of the camera
    // for an image-to-plane map whose reference corners have positive weight.
    double weight(Vec2 p) const { return h[6] * p.x + h[7] * p.y + h[8]; }
    std::optional<Homography> inverse() const;
    Homography then(const Homography& next) const; // next ∘ this
};

// The rectangle of known size in the photo.
struct Reference {
    enum class Kind { Card, A4, Letter, Legal, Rectangle };
    Kind kind = Kind::Card;
    double widthMm = 0;  // the long side
    double heightMm = 0; // the short side
    std::string name;    // "Playing card", "A4 paper", ...
};

// A playing card lying in the photo (kPlayingCard).
Reference cardReference();
Reference a4Reference();
Reference letterReference();
// US Legal, 8.5 x 14 in (a legal pad's sheet).
Reference legalReference();
// Any rectangle; the sides are sorted (long first). Refused (nullopt) when a
// side is not positive.
std::optional<Reference> rectangleReference(double sideA, double sideB);

// A photo with a reference rectangle marked: corners in image pixels, in
// order around the rectangle (either direction, from any corner).
class PlaneRectifier {
public:
    // nullopt when the corners do not make a convex quadrilateral.
    static std::optional<PlaneRectifier> create(const std::array<Vec2, 4>& imageCorners, const Reference& reference);

    // The surface's coordinates (mm): the reference's first corner at the
    // origin, its long side along +x, its short side along +y. Which image
    // edge is the long side is chosen from the photo (the pair of opposite
    // edges that look longer), so the corners may start at any corner.
    Vec2 toPlane(Vec2 image) const { return imageToPlane_.map(image); }
    Vec2 toImage(Vec2 plane) const { return planeToImage_.map(plane); }
    // Whether an image point lies on the surface's visible side (below its
    // horizon): points beyond it have no place on the surface.
    bool onPlane(Vec2 image) const;

    double distanceMm(Vec2 imageA, Vec2 imageB) const;
    // A closed polygon's area on the surface (mm²), from image points.
    double areaMm2(const std::vector<Vec2>& imagePolygon) const;
    // The plane's long-side and short-side sizes of the reference (mm).
    double referenceWidthMm() const { return widthMm_; }
    double referenceHeightMm() const { return heightMm_; }
    const Homography& imageToPlane() const { return imageToPlane_; }
    const Homography& planeToImage() const { return planeToImage_; }
    // The image corners in the order the plane uses: (0,0), (W,0), (W,H), (0,H).
    const std::array<Vec2, 4>& orderedCorners() const { return corners_; }

    // How finely the photo resolves the surface at the reference's center:
    // millimeters per image pixel (the straightened view need not be finer).
    double mmPerImagePixelAtReference() const;

private:
    Homography imageToPlane_;
    Homography planeToImage_;
    std::array<Vec2, 4> corners_{};
    double widthMm_ = 0;
    double heightMm_ = 0;
};

// The straightened view of a photo: which part of the surface it shows and
// at what scale.
struct RectifiedView {
    Vec2 originMm;          // the surface point at the view's top-left pixel corner
    double mmPerPixel = 1;  // the view's scale
    int width = 0;          // pixels
    int height = 0;
    Vec2 toPixel(Vec2 planeMm) const { return (planeMm - originMm) * (1.0 / mmPerPixel); }
    Vec2 toPlane(Vec2 pixel) const { return originMm + pixel * mmPerPixel; }
};

// The view covering the part of the surface the photo shows, up to `margin`
// times the reference's size around it (a photo's far edge can reach the
// horizon), at most `maxPixels` on its longer side and no finer than the
// photo resolves near the reference.
RectifiedView planRectifiedView(const PlaneRectifier& rectifier, int imageWidth, int imageHeight, int maxPixels = 2400,
                                double margin = 1.5);

// 32-bit pixels (any byte order: the four bytes are interpolated alike).
struct ImageBuffer {
    int width = 0;
    int height = 0;
    int stride = 0; // bytes per row
    const std::uint8_t* data = nullptr;
};

// Fills `out` (view.width x view.height, 4 bytes a pixel, `outStride` bytes a
// row) with the photo straightened: each pixel samples the photo bilinearly
// where its surface point lies; outside the photo it is transparent black.
void warpToView(const ImageBuffer& photo, const PlaneRectifier& rectifier, const RectifiedView& view, std::uint8_t* out,
                int outStride);

} // namespace os::measure
