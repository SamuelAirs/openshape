// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/Homography.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace os::measure {

namespace {

using Mat3 = std::array<double, 9>;

Mat3 multiply(const Mat3& a, const Mat3& b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = a[i * 3 + 0] * b[0 * 3 + j] + a[i * 3 + 1] * b[1 * 3 + j] + a[i * 3 + 2] * b[2 * 3 + j];
    return r;
}

double determinant(const Mat3& m)
{
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

std::optional<Mat3> invert(const Mat3& m)
{
    const double det = determinant(m);
    double scale = 0;
    for (double v : m)
        scale = std::max(scale, std::abs(v));
    if (!(scale > 0) || std::abs(det) <= 1e-14 * scale * scale * scale)
        return std::nullopt;
    Mat3 r{};
    r[0] = (m[4] * m[8] - m[5] * m[7]) / det;
    r[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    r[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    r[3] = (m[5] * m[6] - m[3] * m[8]) / det;
    r[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    r[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    r[6] = (m[3] * m[7] - m[4] * m[6]) / det;
    r[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    r[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return r;
}

// Scales so that h[8] is 1 when it is not tiny (the usual form).
Mat3 normalized(Mat3 m)
{
    if (std::abs(m[8]) > 1e-12) {
        const double s = 1.0 / m[8];
        for (double& v : m)
            v *= s;
    }
    return m;
}

// Hartley's normalization: the points' centroid to the origin and their mean
// distance from it to sqrt(2), so the linear system is well conditioned for
// photo pixel coordinates in the thousands.
Mat3 normalizingTransform(const std::array<Vec2, 4>& pts)
{
    Vec2 c;
    for (const Vec2& p : pts)
        c = c + p;
    c = c * 0.25;
    double mean = 0;
    for (const Vec2& p : pts)
        mean += (p - c).length();
    mean *= 0.25;
    const double s = mean > 0 ? std::sqrt(2.0) / mean : 1.0;
    return {s, 0, -s * c.x, 0, s, -s * c.y, 0, 0, 1};
}

Vec2 apply(const Mat3& m, Vec2 p)
{
    const double w = m[6] * p.x + m[7] * p.y + m[8];
    return {(m[0] * p.x + m[1] * p.y + m[2]) / w, (m[3] * p.x + m[4] * p.y + m[5]) / w};
}

// Solves the 8 x 8 system a x = b by Gaussian elimination with partial
// pivoting; false when it is singular.
bool solve8(std::array<std::array<double, 9>, 8>& a, std::array<double, 8>& x)
{
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col]))
                pivot = row;
        if (std::abs(a[pivot][col]) < 1e-10)
            return false;
        std::swap(a[col], a[pivot]);
        for (int row = col + 1; row < 8; ++row) {
            const double f = a[row][col] / a[col][col];
            for (int k = col; k < 9; ++k)
                a[row][k] -= f * a[col][k];
        }
    }
    for (int row = 7; row >= 0; --row) {
        double v = a[row][8];
        for (int k = row + 1; k < 8; ++k)
            v -= a[row][k] * x[k];
        x[row] = v / a[row][row];
    }
    return true;
}

double shoelace(const std::array<Vec2, 4>& q)
{
    double s = 0;
    for (int i = 0; i < 4; ++i) {
        const Vec2& a = q[i];
        const Vec2& b = q[(i + 1) % 4];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

bool convex(const std::array<Vec2, 4>& q)
{
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        const Vec2 e1 = q[(i + 1) % 4] - q[i];
        const Vec2 e2 = q[(i + 2) % 4] - q[(i + 1) % 4];
        const double cross = e1.x * e2.y - e1.y * e2.x;
        const double scale = e1.length() * e2.length();
        if (!(scale > 0) || std::abs(cross) < 1e-6 * scale)
            return false;
        const int s = cross > 0 ? 1 : -1;
        if (sign != 0 && s != sign)
            return false;
        sign = s;
    }
    return true;
}

// No three of the four points in a line (else no unique map).
bool generalPosition(const std::array<Vec2, 4>& q)
{
    double scale = 0;
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j)
            scale = std::max(scale, (q[i] - q[j]).length());
    if (!(scale > 0))
        return false;
    for (int skip = 0; skip < 4; ++skip) {
        std::array<Vec2, 3> t{};
        int k = 0;
        for (int i = 0; i < 4; ++i)
            if (i != skip)
                t[k++] = q[i];
        const Vec2 a = t[1] - t[0], b = t[2] - t[0];
        if (std::abs(a.x * b.y - a.y * b.x) < 1e-9 * scale * scale)
            return false;
    }
    return true;
}

} // namespace

std::optional<Homography> Homography::fromQuads(const std::array<Vec2, 4>& from, const std::array<Vec2, 4>& to)
{
    if (!generalPosition(from) || !generalPosition(to))
        return std::nullopt;
    const Mat3 tf = normalizingTransform(from);
    const Mat3 tt = normalizingTransform(to);
    std::array<Vec2, 4> f{}, t{};
    for (int i = 0; i < 4; ++i) {
        f[i] = apply(tf, from[i]);
        t[i] = apply(tt, to[i]);
    }
    std::array<std::array<double, 9>, 8> a{};
    for (int i = 0; i < 4; ++i) {
        const double x = f[i].x, y = f[i].y, u = t[i].x, v = t[i].y;
        a[2 * i] = {x, y, 1, 0, 0, 0, -x * u, -y * u, u};
        a[2 * i + 1] = {0, 0, 0, x, y, 1, -x * v, -y * v, v};
    }
    std::array<double, 8> x{};
    if (!solve8(a, x))
        return std::nullopt;
    const Mat3 hn{x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], 1.0};
    const auto ttInv = invert(tt);
    if (!ttInv)
        return std::nullopt;
    Homography result;
    result.h = normalized(multiply(*ttInv, multiply(hn, tf)));
    for (double v : result.h)
        if (!std::isfinite(v))
            return std::nullopt;
    return result;
}

Vec2 Homography::map(Vec2 p) const { return apply(h, p); }

std::optional<Homography> Homography::inverse() const
{
    const auto inv = invert(h);
    if (!inv)
        return std::nullopt;
    Homography result;
    result.h = normalized(*inv);
    return result;
}

Homography Homography::then(const Homography& next) const
{
    Homography result;
    result.h = normalized(multiply(next.h, h));
    return result;
}

Reference cardReference() { return {Reference::Kind::Card, kPlayingCard.longMm, kPlayingCard.shortMm, kPlayingCard.name}; }
Reference a4Reference() { return {Reference::Kind::A4, 297.0, 210.0, "A4 paper"}; }
Reference letterReference() { return {Reference::Kind::Letter, 279.4, 215.9, "Letter paper"}; }

std::optional<Reference> rectangleReference(double sideA, double sideB)
{
    if (!(sideA > 0) || !(sideB > 0) || !std::isfinite(sideA) || !std::isfinite(sideB))
        return std::nullopt;
    return Reference{Reference::Kind::Rectangle, std::max(sideA, sideB), std::min(sideA, sideB), "Rectangle"};
}

std::optional<PlaneRectifier> PlaneRectifier::create(const std::array<Vec2, 4>& imageCorners, const Reference& reference)
{
    if (!(reference.widthMm > 0) || !(reference.heightMm > 0) || !convex(imageCorners))
        return std::nullopt;
    std::array<Vec2, 4> q = imageCorners;
    // The plane's corners (0,0), (W,0), (W,H), (0,H) go round the same way as
    // image pixels (y down): keep the photo's corners in that sense, or the
    // straightened photo would come out mirrored.
    if (shoelace(q) < 0)
        q = {q[0], q[3], q[2], q[1]};
    // The long side: the pair of opposite edges that look longer.
    const double first = ((q[1] - q[0]).length() + (q[3] - q[2]).length());
    const double second = ((q[2] - q[1]).length() + (q[0] - q[3]).length());
    if (second > first)
        q = {q[1], q[2], q[3], q[0]};
    const double w = std::max(reference.widthMm, reference.heightMm);
    const double hgt = std::min(reference.widthMm, reference.heightMm);
    const std::array<Vec2, 4> plane{Vec2{0, 0}, Vec2{w, 0}, Vec2{w, hgt}, Vec2{0, hgt}};
    auto toPlane = Homography::fromQuads(q, plane);
    if (!toPlane)
        return std::nullopt;
    // Positive weights at the reference (in front of the camera).
    if (toPlane->weight(q[0]) < 0)
        for (double& v : toPlane->h)
            v = -v;
    auto toImage = toPlane->inverse();
    if (!toImage)
        return std::nullopt;
    PlaneRectifier r;
    r.imageToPlane_ = *toPlane;
    r.planeToImage_ = *toImage;
    r.corners_ = q;
    r.widthMm_ = w;
    r.heightMm_ = hgt;
    return r;
}

bool PlaneRectifier::onPlane(Vec2 image) const
{
    // The weight falls to zero at the horizon. A small fraction of the
    // reference's own weight keeps points just short of it out too: their
    // places on the surface are too far away to mean anything.
    const double ref = imageToPlane_.weight(corners_[0]);
    return imageToPlane_.weight(image) > 1e-3 * std::abs(ref);
}

double PlaneRectifier::distanceMm(Vec2 imageA, Vec2 imageB) const { return (toPlane(imageA) - toPlane(imageB)).length(); }

double PlaneRectifier::areaMm2(const std::vector<Vec2>& imagePolygon) const
{
    if (imagePolygon.size() < 3)
        return 0;
    double s = 0;
    for (std::size_t i = 0; i < imagePolygon.size(); ++i) {
        const Vec2 a = toPlane(imagePolygon[i]);
        const Vec2 b = toPlane(imagePolygon[(i + 1) % imagePolygon.size()]);
        s += a.x * b.y - b.x * a.y;
    }
    return std::abs(s) * 0.5;
}

double PlaneRectifier::mmPerImagePixelAtReference() const
{
    const Vec2 c = toImage({widthMm_ * 0.5, heightMm_ * 0.5});
    const Vec2 p = toPlane(c);
    const Vec2 dx = toPlane(c + Vec2{1, 0}) - p;
    const Vec2 dy = toPlane(c + Vec2{0, 1}) - p;
    return std::sqrt(std::abs(dx.x * dy.y - dx.y * dy.x));
}

RectifiedView planRectifiedView(const PlaneRectifier& rectifier, int imageWidth, int imageHeight, int maxPixels, double margin)
{
    const double w = rectifier.referenceWidthMm();
    const double h = rectifier.referenceHeightMm();
    const double minX = -margin * w, maxX = (1 + margin) * w;
    const double minY = -margin * h, maxY = (1 + margin) * h;
    double x0 = 0, y0 = 0, x1 = w, y1 = h; // the reference is always in view
    const int n = 32;
    for (int i = 0; i <= n; ++i) {
        for (int j = 0; j <= n; ++j) {
            const Vec2 image{imageWidth * double(i) / n, imageHeight * double(j) / n};
            if (!rectifier.onPlane(image))
                continue;
            const Vec2 p = rectifier.toPlane(image);
            x0 = std::min(x0, std::clamp(p.x, minX, maxX));
            x1 = std::max(x1, std::clamp(p.x, minX, maxX));
            y0 = std::min(y0, std::clamp(p.y, minY, maxY));
            y1 = std::max(y1, std::clamp(p.y, minY, maxY));
        }
    }
    const double extent = std::max(x1 - x0, y1 - y0);
    const double finest = rectifier.mmPerImagePixelAtReference();
    double mmPerPixel = extent / std::max(1, maxPixels);
    if (finest > 0 && std::isfinite(finest))
        mmPerPixel = std::max(mmPerPixel, finest);
    RectifiedView view;
    view.originMm = {x0, y0};
    view.mmPerPixel = mmPerPixel;
    view.width = std::max(1, int(std::ceil((x1 - x0) / mmPerPixel)));
    view.height = std::max(1, int(std::ceil((y1 - y0) / mmPerPixel)));
    return view;
}

void warpToView(const ImageBuffer& photo, const PlaneRectifier& rectifier, const RectifiedView& view, std::uint8_t* out,
                int outStride)
{
    if (!out || view.width <= 0 || view.height <= 0)
        return;
    const Homography& m = rectifier.planeToImage();
    // Plane points whose image has the other sign of weight lie behind the
    // camera: they are not in the photo.
    const Vec2 refCenter{rectifier.referenceWidthMm() * 0.5, rectifier.referenceHeightMm() * 0.5};
    const double refSign = m.weight(refCenter) >= 0 ? 1.0 : -1.0;
    for (int y = 0; y < view.height; ++y) {
        std::uint8_t* row = out + std::ptrdiff_t(y) * outStride;
        for (int x = 0; x < view.width; ++x) {
            std::uint8_t* px = row + 4 * x;
            const Vec2 plane = view.toPlane({x + 0.5, y + 0.5});
            if (m.weight(plane) * refSign <= 0 || !photo.data) {
                std::memset(px, 0, 4);
                continue;
            }
            const Vec2 img = m.map(plane);
            const double fx = img.x - 0.5, fy = img.y - 0.5;
            if (!(fx >= -0.5 && fy >= -0.5 && fx <= photo.width - 0.5 && fy <= photo.height - 0.5)) {
                std::memset(px, 0, 4);
                continue;
            }
            const int ix = std::clamp(int(std::floor(fx)), 0, photo.width - 1);
            const int iy = std::clamp(int(std::floor(fy)), 0, photo.height - 1);
            const int ix1 = std::min(ix + 1, photo.width - 1);
            const int iy1 = std::min(iy + 1, photo.height - 1);
            const double tx = std::clamp(fx - ix, 0.0, 1.0);
            const double ty = std::clamp(fy - iy, 0.0, 1.0);
            const std::uint8_t* a = photo.data + std::ptrdiff_t(iy) * photo.stride + 4 * ix;
            const std::uint8_t* b = photo.data + std::ptrdiff_t(iy) * photo.stride + 4 * ix1;
            const std::uint8_t* c = photo.data + std::ptrdiff_t(iy1) * photo.stride + 4 * ix;
            const std::uint8_t* d = photo.data + std::ptrdiff_t(iy1) * photo.stride + 4 * ix1;
            for (int k = 0; k < 4; ++k) {
                const double top = a[k] + (b[k] - a[k]) * tx;
                const double bottom = c[k] + (d[k] - c[k]) * tx;
                px[k] = std::uint8_t(std::clamp(top + (bottom - top) * ty + 0.5, 0.0, 255.0));
            }
        }
    }
}

} // namespace os::measure
