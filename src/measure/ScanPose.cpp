// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScanPose.h"

#include "measure/ScanMath.h"

#include <algorithm>
#include <cmath>

namespace os::measure {

using namespace scanmath;

namespace {

constexpr double kFullFrameDiagonalMm = 43.27;
constexpr double kDeg = 180.0 / kPi;

double diagonal(int width, int height) { return std::sqrt(double(width) * width + double(height) * height); }

std::array<Vec2, 4> labelledCorners(const PaperCorners& photo, int firstCorner, bool reversed)
{
    std::array<Vec2, 4> out{};
    for (int m = 0; m < 4; ++m)
        out[m] = photo.corners[reversed ? (firstCorner - m + 8) % 4 : (firstCorner + m) % 4];
    return out;
}

std::optional<Homography> worldToImage(const PaperCorners& photo, const Reference& paper, int firstCorner, bool reversed)
{
    const auto world = paperWorldCorners(paper);
    const std::array<Vec2, 4> from{Vec2{world[0].x, world[0].y}, Vec2{world[1].x, world[1].y}, Vec2{world[2].x, world[2].y},
                                   Vec2{world[3].x, world[3].y}};
    return Homography::fromQuads(from, labelledCorners(photo, firstCorner, reversed));
}

// K^-1 H's columns (H: world plane -> pixels; principal point at the center).
std::array<Vec3, 3> normalizedColumns(const Homography& h, double f, double cx, double cy)
{
    std::array<Vec3, 3> b{};
    for (int j = 0; j < 3; ++j) {
        const double h0 = h.h[j], h1 = h.h[3 + j], h2 = h.h[6 + j];
        b[j] = {(h0 - cx * h2) / f, (h1 - cy * h2) / f, h2};
    }
    return b;
}

double reprojectionRms(const ScanCamera& camera, const std::array<Vec3, 4>& world, const std::array<Vec2, 4>& image)
{
    double s = 0;
    for (int m = 0; m < 4; ++m) {
        if (camera.depth(world[m]) <= 0)
            return 1e30;
        const Vec2 p = camera.project(world[m]);
        s += (p - image[m]).dot(p - image[m]);
    }
    return std::sqrt(s / 4);
}

// Levenberg-Marquardt on the six pose parameters (rotation vector applied
// on the left, translation), numeric derivatives.
void refinePose(ScanCamera& camera, const std::array<Vec3, 4>& world, const std::array<Vec2, 4>& image)
{
    auto residuals = [&](const ScanCamera& c, std::array<double, 8>& r) {
        for (int m = 0; m < 4; ++m) {
            const Vec3 pc = c.toCamera(world[m]);
            if (pc.z <= 1e-9)
                return false;
            r[2 * m] = c.fx * pc.x / pc.z + c.cx - image[m].x;
            r[2 * m + 1] = c.fy * pc.y / pc.z + c.cy - image[m].y;
        }
        return true;
    };
    auto applyStep = [](const ScanCamera& c, const std::array<double, 6>& p) {
        ScanCamera n = c;
        n.R = mul(rodrigues({p[0], p[1], p[2]}), c.R);
        n.t = c.t + Vec3{p[3], p[4], p[5]};
        return n;
    };
    auto cost = [](const std::array<double, 8>& r) {
        double s = 0;
        for (double v : r)
            s += v * v;
        return s;
    };
    std::array<double, 8> r{};
    if (!residuals(camera, r))
        return;
    double current = cost(r);
    double lambda = 1e-3;
    const double scaleT = std::max(1.0, camera.t.length() * 1e-3);
    for (int iteration = 0; iteration < 30 && current > 1e-20; ++iteration) {
        // Jacobian (8 x 6) by central differences.
        std::array<std::array<double, 6>, 8> jac{};
        for (int k = 0; k < 6; ++k) {
            const double h = k < 3 ? 1e-6 : 1e-6 * scaleT * 1e3;
            std::array<double, 6> dp{};
            dp[k] = h;
            std::array<double, 8> rp{}, rm{};
            const bool okP = residuals(applyStep(camera, dp), rp);
            dp[k] = -h;
            const bool okM = residuals(applyStep(camera, dp), rm);
            if (!okP || !okM)
                return;
            for (int i = 0; i < 8; ++i)
                jac[i][k] = (rp[i] - rm[i]) / (2 * h);
        }
        std::vector<double> jtj(36, 0.0), jtr(6, 0.0);
        for (int i = 0; i < 8; ++i)
            for (int a = 0; a < 6; ++a) {
                jtr[a] -= jac[i][a] * r[i];
                for (int b = 0; b < 6; ++b)
                    jtj[a * 6 + b] += jac[i][a] * jac[i][b];
            }
        bool improved = false;
        for (int attempt = 0; attempt < 8; ++attempt) {
            std::vector<double> a = jtj;
            for (int d = 0; d < 6; ++d)
                a[d * 6 + d] += lambda * std::max(jtj[d * 6 + d], 1e-12);
            std::vector<double> step;
            if (!solve(a, jtr, 6, step)) {
                lambda *= 10;
                continue;
            }
            const ScanCamera candidate = applyStep(camera, {step[0], step[1], step[2], step[3], step[4], step[5]});
            std::array<double, 8> rc{};
            if (residuals(candidate, rc) && cost(rc) < current) {
                camera = candidate;
                r = rc;
                const double before = current;
                current = cost(rc);
                lambda = std::max(lambda * 0.3, 1e-9);
                improved = true;
                if (before - current < 1e-12 * before)
                    iteration = 1000;
                break;
            }
            lambda *= 10;
        }
        if (!improved)
            break;
    }
    camera.R = nearestRotation(camera.R);
}

// How far the paper's image is from a rectangle of the reference's
// proportions at this focal length, for the better of the two long-side
// assignments: the cosine between K^-1 H's first two columns (they must be
// perpendicular) and the log of their lengths' ratio (they must be equal).
double rectangleError(const std::array<std::optional<Homography>, 2>& homographies, double f, double cx, double cy)
{
    double best = 1e30;
    for (const auto& h : homographies) {
        if (!h)
            continue;
        const auto b = normalizedColumns(*h, f, cx, cy);
        const double l1 = b[0].length(), l2 = b[1].length();
        if (!(l1 > 0) || !(l2 > 0))
            continue;
        const double c = b[0].dot(b[1]) / (l1 * l2);
        const double rho = std::log(l1 / l2);
        best = std::min(best, c * c + rho * rho);
    }
    return best;
}

} // namespace

double focalPxFrom35mm(double focal35mm, int width, int height)
{
    return focal35mm * diagonal(width, height) / kFullFrameDiagonalMm;
}

double focal35mmFromPx(double focalPx, int width, int height)
{
    const double d = diagonal(width, height);
    return d > 0 ? focalPx * kFullFrameDiagonalMm / d : 0;
}

std::array<Vec3, 4> paperWorldCorners(const Reference& paper)
{
    const double w = std::max(paper.widthMm, paper.heightMm) * 0.5;
    const double h = std::min(paper.widthMm, paper.heightMm) * 0.5;
    return {Vec3{-w, -h, 0}, Vec3{w, -h, 0}, Vec3{w, h, 0}, Vec3{-w, h, 0}};
}

std::optional<PaperPose> poseForLabelling(const PaperCorners& photo, const Reference& paper, double focalPx,
                                          int firstCorner, bool reversed)
{
    if (!(focalPx > 0) || photo.width <= 0 || photo.height <= 0)
        return std::nullopt;
    const auto h = worldToImage(photo, paper, firstCorner, reversed);
    if (!h)
        return std::nullopt;
    ScanCamera cam;
    cam.fx = cam.fy = focalPx;
    cam.cx = photo.width * 0.5;
    cam.cy = photo.height * 0.5;
    cam.width = photo.width;
    cam.height = photo.height;
    const auto b = normalizedColumns(*h, focalPx, cam.cx, cam.cy);
    const double l1 = b[0].length(), l2 = b[1].length();
    if (!(l1 > 0) || !(l2 > 0))
        return std::nullopt;
    double lambda = 2.0 / (l1 + l2);
    if (b[2].z * lambda < 0)
        lambda = -lambda;
    const Vec3 r1 = b[0] * lambda, r2 = b[1] * lambda;
    const Vec3 r1n = r1.normalized(), r2n = r2.normalized();
    cam.R = nearestRotation(fromColumns(r1n, r2n, r1n.cross(r2n)));
    cam.t = b[2] * lambda;
    if (cam.center().z <= 0)
        return std::nullopt;
    const auto world = paperWorldCorners(paper);
    const auto image = labelledCorners(photo, firstCorner, reversed);
    refinePose(cam, world, image);
    if (cam.center().z <= 0)
        return std::nullopt;
    for (const Vec3& w : world)
        if (cam.depth(w) <= 0)
            return std::nullopt;
    PaperPose pose;
    pose.firstCorner = firstCorner;
    pose.reversed = reversed;
    pose.camera = cam;
    pose.reprojectionErrorPx = reprojectionRms(cam, world, image);
    if (!std::isfinite(pose.reprojectionErrorPx))
        return std::nullopt;
    return pose;
}

std::vector<PaperPose> plausiblePoses(const PaperCorners& photo, const Reference& paper, double focalPx)
{
    std::vector<PaperPose> all;
    for (int reversed = 0; reversed < 2; ++reversed)
        for (int first = 0; first < 4; ++first)
            if (auto pose = poseForLabelling(photo, paper, focalPx, first, reversed != 0))
                all.push_back(*pose);
    std::sort(all.begin(), all.end(),
              [](const PaperPose& a, const PaperPose& b) { return a.reprojectionErrorPx < b.reprojectionErrorPx; });
    if (all.empty())
        return all;
    // The paper's size in the image sets what "close" means: a wrong long
    // side misplaces corners by a good fraction of it.
    double extent = 0;
    for (int i = 0; i < 4; ++i)
        extent = std::max(extent, (photo.corners[i] - photo.corners[(i + 2) % 4]).length());
    const double limit = 3 * all.front().reprojectionErrorPx + 1.0 + 0.004 * extent;
    std::vector<PaperPose> kept;
    for (const PaperPose& p : all)
        if (p.reprojectionErrorPx <= limit)
            kept.push_back(p);
    return kept;
}

std::optional<double> estimateFocal35mm(const std::vector<PaperCorners>& photos, const Reference& paper, int* usedPhotos)
{
    if (usedPhotos)
        *usedPhotos = 0;
    struct Item {
        std::array<std::optional<Homography>, 2> h;
        double cx, cy, diag;
        const PaperCorners* photo;
    };
    std::vector<Item> items;
    for (const PaperCorners& p : photos) {
        if (p.width <= 0 || p.height <= 0)
            continue;
        Item item{{worldToImage(p, paper, 0, false), worldToImage(p, paper, 1, false)}, p.width * 0.5, p.height * 0.5,
                  diagonal(p.width, p.height), &p};
        if (item.h[0] || item.h[1])
            items.push_back(item);
    }
    if (items.empty())
        return std::nullopt;

    // A grid of focal lengths (35 mm equivalent), from fisheye to telephoto.
    constexpr int kSteps = 400;
    const double lo = 8, hi = 200;
    auto focalAt = [&](int s) { return lo * std::pow(hi / lo, double(s) / (kSteps - 1)); };

    // Each photo's own best focal and how sharply its error rises around it:
    // a photo looking almost straight down fits any focal.
    std::vector<double> estimates;
    std::vector<const Item*> informative;
    for (const Item& item : items) {
        std::vector<double> err(kSteps);
        int best = 0;
        for (int s = 0; s < kSteps; ++s) {
            err[s] = rectangleError(item.h, focalAt(s) * item.diag / kFullFrameDiagonalMm, item.cx, item.cy);
            if (err[s] < err[best])
                best = s;
        }
        if (best == 0 || best == kSteps - 1)
            continue;
        // The error 20 % either side of the minimum.
        const double f = focalAt(best);
        const double below = rectangleError(item.h, 0.8 * f * item.diag / kFullFrameDiagonalMm, item.cx, item.cy);
        const double above = rectangleError(item.h, 1.25 * f * item.diag / kFullFrameDiagonalMm, item.cx, item.cy);
        const double rise = std::min(below, above) - err[best];
        // Also the camera's tilt from looking straight down, at that focal.
        const auto pose = poseForLabelling(*item.photo, paper, f * item.diag / kFullFrameDiagonalMm, 0, false);
        const auto other = pose ? pose : poseForLabelling(*item.photo, paper, f * item.diag / kFullFrameDiagonalMm, 0, true);
        double tiltDeg = 90;
        if (other) {
            const Vec3 axis = other->camera.viewDirection();
            tiltDeg = std::acos(std::clamp(-axis.z, -1.0, 1.0)) * kDeg;
        }
        if (rise < 2e-4 || tiltDeg < 15)
            continue;
        estimates.push_back(f);
        informative.push_back(&item);
    }
    if (estimates.empty())
        return std::nullopt;
    std::vector<double> sorted = estimates;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double median = sorted[sorted.size() / 2];

    // Refined on the corners: the focal whose cameras reproject all the
    // informative photos' corners best (golden-section search near the median).
    auto total = [&](double f35) {
        double s = 0;
        for (const Item* item : informative) {
            const auto poses = plausiblePoses(*item->photo, paper, f35 * item->diag / kFullFrameDiagonalMm);
            const double e = poses.empty() ? 50.0 : std::min(poses.front().reprojectionErrorPx, 50.0);
            s += e * e;
        }
        return s;
    };
    double a = median * 0.85, b = median * 1.18;
    const double g = (std::sqrt(5.0) - 1) / 2;
    double x1 = b - g * (b - a), x2 = a + g * (b - a);
    double f1 = total(x1), f2 = total(x2);
    for (int it = 0; it < 40 && b - a > 1e-4 * median; ++it) {
        if (f1 < f2) {
            b = x2;
            x2 = x1;
            f2 = f1;
            x1 = b - g * (b - a);
            f1 = total(x1);
        } else {
            a = x1;
            x1 = x2;
            f1 = f2;
            x2 = a + g * (b - a);
            f2 = total(x2);
        }
    }
    if (usedPhotos)
        *usedPhotos = int(informative.size());
    return 0.5 * (a + b);
}

CameraPlacement cameraPlacement(const ScanCamera& camera)
{
    const Vec3 c = camera.center();
    CameraPlacement p;
    p.distanceMm = c.length();
    p.azimuthDeg = std::atan2(c.y, c.x) * kDeg;
    p.elevationDeg = std::atan2(c.z, std::sqrt(c.x * c.x + c.y * c.y)) * kDeg;
    return p;
}

ScanCamera turnedHalf(const ScanCamera& camera)
{
    ScanCamera c = camera;
    for (int row = 0; row < 3; ++row) {
        c.R[row * 3] = -camera.R[row * 3];
        c.R[row * 3 + 1] = -camera.R[row * 3 + 1];
    }
    return c;
}

} // namespace os::measure
