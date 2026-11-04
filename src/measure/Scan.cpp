// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/Scan.h"

#include "measure/MeshTools.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

namespace os::measure {

namespace {

template <typename R>
R cancelled()
{
    return R::failure(ErrorCode::NoEffect, "Cancelled.", "scan: cancelled by the progress callback");
}

// Runs body(i) for i in [0, n) on a few threads (photos are independent).
template <typename F>
void parallelFor(int n, F body)
{
    const int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 6);
    if (n <= 1 || threads <= 1) {
        for (int i = 0; i < n; ++i)
            body(i);
        return;
    }
    std::vector<std::thread> pool;
    std::atomic<int> next{0};
    for (int t = 0; t < std::min(threads, n); ++t)
        pool.emplace_back([&] {
            for (int i = next++; i < n; i = next++)
                body(i);
        });
    for (auto& th : pool)
        th.join();
}

std::string format(double v, int decimals = 0)
{
    std::ostringstream s;
    s << std::fixed << std::setprecision(decimals) << v;
    return s.str();
}

double wrapDeg(double a)
{
    while (a > 180)
        a -= 360;
    while (a <= -180)
        a += 360;
    return a;
}

// Chamfer (3-4) distance in pixels from each pixel to the nearest seed,
// capped at 255. Beyond the image counts as a seed when `edgeIsSeed`.
std::vector<std::uint8_t> distanceTo(const std::vector<std::uint8_t>& seed, int w, int h, bool edgeIsSeed)
{
    constexpr int kInf = 1 << 28;
    std::vector<int> d(seed.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            if (seed[i])
                d[i] = 0;
            else if (edgeIsSeed)
                d[i] = 3 * std::min(std::min(x + 1, y + 1), std::min(w - x, h - y));
            else
                d[i] = kInf;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            int v = d[i];
            if (x > 0)
                v = std::min(v, d[i - 1] + 3);
            if (y > 0) {
                v = std::min(v, d[i - w] + 3);
                if (x > 0)
                    v = std::min(v, d[i - w - 1] + 4);
                if (x + 1 < w)
                    v = std::min(v, d[i - w + 1] + 4);
            }
            d[i] = v;
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            const std::size_t i = std::size_t(y) * w + x;
            int v = d[i];
            if (x + 1 < w)
                v = std::min(v, d[i + 1] + 3);
            if (y + 1 < h) {
                v = std::min(v, d[i + w] + 3);
                if (x + 1 < w)
                    v = std::min(v, d[i + w + 1] + 4);
                if (x > 0)
                    v = std::min(v, d[i + w - 1] + 4);
            }
            d[i] = v;
        }
    std::vector<std::uint8_t> out(seed.size());
    for (std::size_t i = 0; i < d.size(); ++i)
        out[i] = std::uint8_t(std::min(255, d[i] / 3));
    return out;
}

// A view's distance maps: to the nearest pixel that is not background (0 on
// such pixels; the image's edge counts, beyond it nothing is known) and to
// the nearest object pixel.
struct ViewMaps {
    std::vector<std::uint8_t> toNonBackground;
    std::vector<std::uint8_t> toObject;
};

ViewMaps viewMaps(const ScanView& view)
{
    const int w = view.camera.width, h = view.camera.height;
    std::vector<std::uint8_t> nonBg(view.mask.size()), obj(view.mask.size());
    for (std::size_t i = 0; i < view.mask.size(); ++i) {
        nonBg[i] = view.mask[i] != kMaskBackground;
        obj[i] = view.mask[i] == kMaskObject;
    }
    return {distanceTo(nonBg, w, h, true), distanceTo(obj, w, h, false)};
}

// What a view says about a point: its pixel (or -1 when outside the image
// or behind the camera), and the camera's pixels per mm there.
struct Hit {
    long pixel = -1;
    double pxPerMm = 0;
};

inline Hit look(const ScanCamera& c, const Vec3& p)
{
    const Vec3 q = c.toCamera(p);
    if (q.z <= 1e-6)
        return {};
    const double u = c.fx * q.x / q.z + c.cx, v = c.fy * q.y / q.z + c.cy;
    if (!(u >= 0 && v >= 0 && u < c.width && v < c.height))
        return {};
    return {long(int(v)) * c.width + int(u), c.fx / q.z};
}

double carveHeight(const ScanOptions& o)
{
    return o.heightMm > 0 ? o.heightMm : std::min(o.paper.widthMm, o.paper.heightMm);
}

// ---- Corner labelling by silhouette consistency.
//
// Each photo has a few candidate cameras (the paper alone allows its two
// labellings half a turn apart). A coarse voxel grid over the sheet is
// carved by every photo's outline with the chosen candidates; a wrong
// candidate carves the object in the wrong place, so the hull shrinks and no
// longer fills the other photos' outlines. The choice maximizes how much of
// every photo's outline the hull fills, with the camera path's continuity
// (photos taken in sequence around the object) as a tie-break.
struct LabelSolver {
    struct Candidate {
        std::vector<std::uint8_t> carves;  // per voxel: the photo is sure it is empty
        std::vector<std::uint8_t> objects; // per voxel: the photo sees the object there
        std::vector<int> cell;             // per voxel: the outline cell it lands in, -1 outside
        double azimuth = 0;
    };
    struct Photo {
        std::vector<Candidate> candidates;
        int cellsX = 0, cellsY = 0;
        std::vector<std::uint8_t> objectCell; // cells mostly object
        int objectCells = 0;
    };
    std::vector<Photo> photos;
    std::size_t voxels = 0;
    double pathWeight = 0.04;

    std::vector<double> coverage(const std::vector<int>& choice, std::vector<std::size_t>& alive) const
    {
        alive.clear();
        for (std::size_t v = 0; v < voxels; ++v) {
            bool carved = false, seen = false;
            for (std::size_t i = 0; i < choice.size() && !carved; ++i) {
                const Candidate& c = photos[i].candidates[choice[i]];
                carved = c.carves[v] != 0;
                seen = seen || c.objects[v] != 0;
            }
            if (!carved && seen)
                alive.push_back(v);
        }
        std::vector<double> out(choice.size(), 0.0);
        std::vector<std::uint8_t> hit;
        for (std::size_t i = 0; i < choice.size(); ++i) {
            const Photo& p = photos[i];
            if (p.objectCells == 0)
                continue;
            hit.assign(p.objectCell.size(), 0);
            const Candidate& c = p.candidates[choice[i]];
            for (std::size_t v : alive) {
                if (c.cell[v] < 0)
                    continue;
                const int cx = c.cell[v] % p.cellsX, cy = c.cell[v] / p.cellsX;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int x = cx + dx, y = cy + dy;
                        if (x >= 0 && y >= 0 && x < p.cellsX && y < p.cellsY)
                            hit[std::size_t(y) * p.cellsX + x] = 1;
                    }
            }
            int covered = 0;
            for (std::size_t k = 0; k < hit.size(); ++k)
                covered += hit[k] && p.objectCell[k];
            out[i] = double(covered) / p.objectCells;
        }
        return out;
    }

    double pathCost(const std::vector<int>& choice) const
    {
        double s = 0;
        for (std::size_t i = 1; i < choice.size(); ++i)
            s += std::abs(wrapDeg(photos[i].candidates[choice[i]].azimuth - photos[i - 1].candidates[choice[i - 1]].azimuth))
                 / 180.0;
        return s;
    }

    // The photos [0, choice.size()) with these candidates.
    double score(const std::vector<int>& choice) const
    {
        std::vector<std::size_t> alive;
        double s = 0;
        for (double c : coverage(choice, alive))
            s += c;
        return s - pathWeight * pathCost(choice);
    }

    // The smoothest path around (dynamic programming over the sequence).
    std::vector<int> smoothestPath() const
    {
        const std::size_t n = photos.size();
        std::vector<std::vector<double>> cost(n);
        std::vector<std::vector<int>> from(n);
        cost[0].assign(photos[0].candidates.size(), 0.0);
        from[0].assign(photos[0].candidates.size(), -1);
        for (std::size_t i = 1; i < n; ++i) {
            const auto& cand = photos[i].candidates;
            cost[i].assign(cand.size(), 1e300);
            from[i].assign(cand.size(), 0);
            for (std::size_t b = 0; b < cand.size(); ++b)
                for (std::size_t a = 0; a < photos[i - 1].candidates.size(); ++a) {
                    const double c = cost[i - 1][a]
                                     + std::abs(wrapDeg(cand[b].azimuth - photos[i - 1].candidates[a].azimuth));
                    if (c < cost[i][b]) {
                        cost[i][b] = c;
                        from[i][b] = int(a);
                    }
                }
        }
        std::vector<int> choice(n, 0);
        choice[n - 1] = int(std::min_element(cost[n - 1].begin(), cost[n - 1].end()) - cost[n - 1].begin());
        for (std::size_t i = n - 1; i > 0; --i)
            choice[i - 1] = from[i][choice[i]];
        return choice;
    }

    // Coordinate ascent from a start.
    std::vector<int> improve(std::vector<int> choice, double& best) const
    {
        best = score(choice);
        for (int round = 0; round < 6; ++round) {
            bool changed = false;
            for (std::size_t i = 0; i < photos.size(); ++i)
                for (int k = 0; k < int(photos[i].candidates.size()); ++k) {
                    if (k == choice[i])
                        continue;
                    std::vector<int> trial = choice;
                    trial[i] = k;
                    const double s = score(trial);
                    if (s > best + 1e-9) {
                        best = s;
                        choice = trial;
                        changed = true;
                    }
                }
            if (!changed)
                break;
        }
        return choice;
    }
};

} // namespace

// ------------------------------------------------------------------ calibrate

Result<ScanCalibration> calibrate(const std::vector<ScanPhoto>& photos, const ScanOptions& options, const ScanProgress& progress)
{
    using R = Result<ScanCalibration>;
    const auto report = [&](double f) { return !progress || progress(f); };
    ScanCalibration out;
    const int n = int(photos.size());
    out.photos.resize(n);
    if (n < 3)
        return R::failure(ErrorCode::InvalidArgument, "A scan needs at least 3 photos (better 8 to 16, walking around the object).",
                          "calibrate: " + std::to_string(n) + " photos");
    if (!report(0))
        return cancelled<R>();

    // Shrink the photos, their corners and masks.
    struct Work {
        RgbImage image;
        std::vector<std::uint8_t> subject;
        PaperCorners corners;
        double scale = 1;
        bool ok = false;
        std::vector<PaperPose> poses;
        std::vector<std::uint8_t> mask;
    };
    std::vector<Work> work(n);
    parallelFor(n, [&](int i) {
        const ScanPhoto& p = photos[i];
        Work& w = work[i];
        bool finite = true;
        for (const Vec2& c : p.paperCorners)
            finite = finite && std::isfinite(c.x) && std::isfinite(c.y);
        if (!p.pixels || p.width < 16 || p.height < 16 || !finite
            || !PlaneRectifier::create(p.paperCorners, options.paper)) {
            out.photos[i].problem = !p.pixels || p.width < 16 || p.height < 16 ? "The photo is empty."
                                                                                : "The paper's corners are not marked.";
            return;
        }
        w.image = downsampleRgb(p.pixels, p.width, p.height, p.stride, p.bytesPerPixel, p.bgr, options.maxImageSide);
        if (w.image.width == 0)
            return;
        w.scale = double(w.image.width) / p.width;
        for (int k = 0; k < 4; ++k)
            w.corners.corners[k] = p.paperCorners[k] * w.scale;
        w.corners.width = w.image.width;
        w.corners.height = w.image.height;
        if (p.subjectMask && p.maskWidth > 0 && p.maskHeight > 0)
            w.subject = resampleMask(p.subjectMask, p.maskWidth, p.maskHeight, p.maskStride > 0 ? p.maskStride : p.maskWidth,
                                     w.image.width, w.image.height);
        w.ok = true;
    });
    if (!report(0.35))
        return cancelled<R>();

    // The focal length: the photo's own, else estimated from all papers.
    bool anyMissing = false;
    std::vector<double> exif;
    std::vector<PaperCorners> allCorners;
    for (int i = 0; i < n; ++i) {
        if (!work[i].ok)
            continue;
        allCorners.push_back(work[i].corners);
        if (photos[i].focal35mm > 0)
            exif.push_back(photos[i].focal35mm);
        else
            anyMissing = true;
    }
    if (allCorners.size() < 3)
        return R::failure(ErrorCode::InvalidArgument, "The paper's corners are marked in fewer than 3 photos.",
                          "calibrate: " + std::to_string(allCorners.size()) + " photos with corners");
    double estimated = 0;
    if (anyMissing) {
        int used = 0;
        const auto f = estimateFocal35mm(allCorners, options.paper, &used);
        if (f) {
            estimated = *f;
            out.focalEstimated = true;
            out.focalPhotos = used;
        } else if (!exif.empty()) {
            std::nth_element(exif.begin(), exif.begin() + exif.size() / 2, exif.end());
            estimated = exif[exif.size() / 2];
        } else {
            return R::failure(ErrorCode::InvalidArgument,
                              "The camera's focal length could not be worked out from the paper. Take the photos from lower "
                              "down, about 30 to 60 degrees above the table.",
                              "calibrate: no oblique photo for the focal estimate");
        }
        out.focal35mm = estimated;
        if (out.focalEstimated && used < 3)
            out.warnings.push_back("The camera's focal length rests on only " + std::to_string(used)
                                   + " photos taken at a slant: sizes may be off by a few percent.");
    } else {
        std::nth_element(exif.begin(), exif.begin() + exif.size() / 2, exif.end());
        out.focal35mm = exif[exif.size() / 2];
    }

    // Candidate cameras and the masks.
    parallelFor(n, [&](int i) {
        Work& w = work[i];
        if (!w.ok)
            return;
        ScanPhotoReport& r = out.photos[i];
        r.focalFromExif = photos[i].focal35mm > 0;
        r.focal35mm = r.focalFromExif ? photos[i].focal35mm : estimated;
        r.focalPx = focalPxFrom35mm(r.focal35mm, w.image.width, w.image.height);
        w.poses = plausiblePoses(w.corners, options.paper, r.focalPx);
        r.labellings = int(w.poses.size());
        if (w.poses.empty()) {
            r.problem = "The paper's corners do not fit a sheet of " + options.paper.name + " seen from above.";
            w.ok = false;
            return;
        }
        double extent = 0;
        for (int k = 0; k < 4; ++k)
            extent = std::max(extent, (w.corners.corners[k] - w.corners.corners[(k + 2) % 4]).length());
        r.reprojectionErrorPx = w.poses.front().reprojectionErrorPx / w.scale;
        if (w.poses.front().reprojectionErrorPx > 0.02 * extent) {
            r.problem = "The paper's corners fit a " + options.paper.name
                        + " sheet badly (are they on the sheet's corners, and is the paper flat?).";
            w.ok = false;
            return;
        }
        ScanView view;
        view.camera = w.poses.front().camera;
        view.photoIndex = i;
        view.mask = segmentPhoto(w.image, view.camera, options.paper, w.subject.empty() ? nullptr : w.subject.data(),
                                 options.masks);
        // How much of the paper shows the object.
        long paperPixels = 0, objectPixels = 0;
        const double hw = std::max(options.paper.widthMm, options.paper.heightMm) * 0.5;
        const double hh = std::min(options.paper.widthMm, options.paper.heightMm) * 0.5;
        for (int y = 0; y < w.image.height; y += 2)
            for (int x = 0; x < w.image.width; x += 2) {
                Vec2 p;
                if (!pixelToPaperPlane(view.camera, {x + 0.5, y + 0.5}, p) || std::abs(p.x) > hw || std::abs(p.y) > hh)
                    continue;
                ++paperPixels;
                objectPixels += view.mask[std::size_t(y) * w.image.width + x] == kMaskObject;
            }
        r.objectFraction = paperPixels > 0 ? double(objectPixels) / paperPixels : 0;
        if (r.objectFraction < 0.002) {
            r.problem = "No object stands out from the paper in this photo.";
            w.ok = false;
            return;
        }
        if (r.objectFraction > 0.8) {
            r.problem = "Almost all of the paper looks like the object (is the paper white, and the light even?).";
            w.ok = false;
            return;
        }
        w.mask = std::move(view.mask);
        w.subject.clear();
    });
    if (!report(0.7))
        return cancelled<R>();

    std::vector<int> used;
    for (int i = 0; i < n; ++i)
        if (work[i].ok)
            used.push_back(i);
    if (used.size() < 3) {
        std::string why;
        for (const auto& r : out.photos)
            if (!r.problem.empty()) {
                why = " " + r.problem;
                break;
            }
        return R::failure(ErrorCode::InvalidArgument, "Fewer than 3 photos could be used." + why,
                          "calibrate: " + std::to_string(used.size()) + " usable photos");
    }

    // Views with the first candidate; their maps for the labelling.
    std::vector<ScanView> views(used.size());
    for (std::size_t u = 0; u < used.size(); ++u) {
        Work& w = work[used[u]];
        views[u].camera = w.poses.front().camera;
        views[u].rgb = std::move(w.image.rgb);
        views[u].mask = std::move(w.mask);
        views[u].photoIndex = used[u];
    }
    std::vector<ViewMaps> maps(views.size());
    parallelFor(int(views.size()), [&](int u) { maps[u] = viewMaps(views[u]); });

    // The labelling: a coarse grid over the sheet.
    LabelSolver solver;
    {
        const double W = std::max(options.paper.widthMm, options.paper.heightMm);
        const double H = std::min(options.paper.widthMm, options.paper.heightMm);
        const double height = carveHeight(options);
        const double voxel = std::max(options.coarseVoxelMm, W / 50);
        const int nx = std::max(1, int(std::ceil(W / voxel))), ny = std::max(1, int(std::ceil(H / voxel)));
        const int nz = std::max(1, int(std::ceil(height / voxel)));
        std::vector<Vec3> centers;
        centers.reserve(std::size_t(nx) * ny * nz);
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i)
                    centers.push_back({-W / 2 + (i + 0.5) * voxel, -H / 2 + (j + 0.5) * voxel, (k + 0.5) * voxel});
        solver.voxels = centers.size();
        solver.photos.resize(views.size());
        parallelFor(int(views.size()), [&](int u) {
            const Work& w = work[used[u]];
            LabelSolver::Photo& p = solver.photos[u];
            const ScanCamera& c0 = w.poses.front().camera;
            const double pxPerMm = c0.fx / std::max(1.0, c0.center().length());
            const int cs = std::max(4, int(voxel * pxPerMm));
            p.cellsX = (c0.width + cs - 1) / cs;
            p.cellsY = (c0.height + cs - 1) / cs;
            std::vector<int> count(std::size_t(p.cellsX) * p.cellsY, 0), total(count.size(), 0);
            for (int y = 0; y < c0.height; ++y)
                for (int x = 0; x < c0.width; ++x) {
                    const std::size_t cell = std::size_t(y / cs) * p.cellsX + x / cs;
                    ++total[cell];
                    count[cell] += views[u].mask[std::size_t(y) * c0.width + x] == kMaskObject;
                }
            p.objectCell.assign(count.size(), 0);
            for (std::size_t k = 0; k < count.size(); ++k)
                if (total[k] > 0 && 2 * count[k] >= total[k]) {
                    p.objectCell[k] = 1;
                    ++p.objectCells;
                }
            for (const PaperPose& pose : w.poses) {
                LabelSolver::Candidate cand;
                cand.azimuth = cameraPlacement(pose.camera).azimuthDeg;
                cand.carves.assign(centers.size(), 0);
                cand.objects.assign(centers.size(), 0);
                cand.cell.assign(centers.size(), -1);
                for (std::size_t v = 0; v < centers.size(); ++v) {
                    const Hit hit = look(pose.camera, centers[v]);
                    if (hit.pixel < 0)
                        continue;
                    const double half = 0.6 * voxel * hit.pxPerMm;
                    cand.carves[v] = maps[u].toNonBackground[hit.pixel] >= half + options.carveMarginPx;
                    cand.objects[v] = maps[u].toObject[hit.pixel] <= half;
                    const int px = int(hit.pixel % pose.camera.width), py = int(hit.pixel / pose.camera.width);
                    cand.cell[v] = (py / cs) * p.cellsX + px / cs;
                }
                p.candidates.push_back(std::move(cand));
            }
        });
    }
    if (!report(0.8))
        return cancelled<R>();
    double bestScore = -1e300;
    std::vector<int> choice;
    {
        std::vector<std::vector<int>> starts;
        starts.push_back(solver.smoothestPath());
        starts.push_back(std::vector<int>(views.size(), 0));
        // Greedy: photos added one by one, each with its best candidate.
        std::vector<int> greedy(views.size(), 0);
        for (std::size_t i = 1; i < views.size(); ++i) {
            double best = -1e300;
            std::vector<int> prefix(greedy.begin(), greedy.begin() + long(i) + 1);
            for (int k = 0; k < int(solver.photos[i].candidates.size()); ++k) {
                prefix[i] = k;
                const double s = solver.score(prefix);
                if (s > best) {
                    best = s;
                    greedy[i] = k;
                }
            }
        }
        starts.push_back(greedy);
        for (const auto& start : starts) {
            double s = 0;
            auto improved = solver.improve(start, s);
            if (s > bestScore + 1e-9) {
                bestScore = s;
                choice = improved;
            }
        }
    }
    if (!report(0.95))
        return cancelled<R>();

    // The chosen cameras, and how decisive each choice was.
    for (std::size_t u = 0; u < views.size(); ++u) {
        const PaperPose& pose = work[used[u]].poses[choice[u]];
        views[u].camera = pose.camera;
        ScanPhotoReport& r = out.photos[used[u]];
        r.used = true;
        r.firstCorner = pose.firstCorner;
        r.reversed = pose.reversed;
        double margin = 1e300;
        for (int k = 0; k < int(solver.photos[u].candidates.size()); ++k) {
            if (k == choice[u])
                continue;
            std::vector<int> trial = choice;
            trial[u] = k;
            margin = std::min(margin, bestScore - solver.score(trial));
        }
        r.labellingMargin = margin > 1e299 ? 0 : margin;
        const CameraPlacement place = cameraPlacement(pose.camera);
        r.azimuthDeg = place.azimuthDeg;
        r.elevationDeg = place.elevationDeg;
        r.distanceMm = place.distanceMm;
        if (place.elevationDeg < 12)
            r.problem = "Taken from very low: the paper is seen at a grazing angle.";
    }
    out.views = std::move(views);

    // Warnings about the set of photos.
    const int usedCount = int(out.views.size());
    if (usedCount < n)
        out.warnings.push_back(std::to_string(n - usedCount) + (n - usedCount == 1 ? " photo was" : " photos were")
                               + " left out (see the photos' notes).");
    if (usedCount < 6)
        out.warnings.push_back("Only " + std::to_string(usedCount)
                               + " photos could be used: take 8 to 16, walking all the way around the object.");
    std::vector<double> az;
    for (const ScanView& v : out.views)
        az.push_back(cameraPlacement(v.camera).azimuthDeg);
    std::sort(az.begin(), az.end());
    double gap = 0, gapFrom = 0;
    for (std::size_t i = 0; i < az.size(); ++i) {
        const double next = i + 1 < az.size() ? az[i + 1] : az[0] + 360;
        if (next - az[i] > gap) {
            gap = next - az[i];
            gapFrom = az[i];
        }
    }
    if (gap > 100)
        out.warnings.push_back("The photos leave a gap of " + format(gap) + "° around the object (from "
                               + format(wrapDeg(gapFrom)) + "° to " + format(wrapDeg(gapFrom + gap))
                               + "°): that side's shape is guessed. Add photos from there.");
    if (!report(1))
        return cancelled<R>();
    std::vector<std::string> warnings = out.warnings;
    return R::success(std::move(out), std::move(warnings));
}

// ---------------------------------------------------------------------- carve

namespace {

// Keeps the largest 6-connected group of occupied voxels.
void keepLargestComponent(std::vector<std::uint8_t>& occ, int nx, int ny, int nz)
{
    std::vector<int> label(occ.size(), -1);
    std::vector<std::size_t> stack;
    int bestLabel = -1;
    std::size_t bestSize = 0;
    int next = 0;
    for (std::size_t seed = 0; seed < occ.size(); ++seed) {
        if (!occ[seed] || label[seed] >= 0)
            continue;
        std::size_t size = 0;
        stack.push_back(seed);
        label[seed] = next;
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            ++size;
            const int x = int(i % nx), y = int((i / nx) % ny), z = int(i / (std::size_t(nx) * ny));
            const int dx[6] = {-1, 1, 0, 0, 0, 0}, dy[6] = {0, 0, -1, 1, 0, 0}, dz[6] = {0, 0, 0, 0, -1, 1};
            for (int k = 0; k < 6; ++k) {
                const int xx = x + dx[k], yy = y + dy[k], zz = z + dz[k];
                if (xx < 0 || yy < 0 || zz < 0 || xx >= nx || yy >= ny || zz >= nz)
                    continue;
                const std::size_t j = (std::size_t(zz) * ny + yy) * nx + xx;
                if (occ[j] && label[j] < 0) {
                    label[j] = next;
                    stack.push_back(j);
                }
            }
        }
        if (size > bestSize) {
            bestSize = size;
            bestLabel = next;
        }
        ++next;
    }
    for (std::size_t i = 0; i < occ.size(); ++i)
        occ[i] = occ[i] && label[i] == bestLabel;
}

// Separable Gaussian blur; below the grid's bottom the field is mirrored
// (the object stands on the paper: its bottom must not be eaten away).
std::vector<float> blur(const std::vector<std::uint8_t>& occ, int nx, int ny, int nz, double sigma)
{
    std::vector<float> a(occ.begin(), occ.end());
    if (!(sigma > 0.05))
        return a;
    const int r = std::max(1, int(std::ceil(2.5 * sigma)));
    std::vector<float> kernel(2 * r + 1);
    float sum = 0;
    for (int k = -r; k <= r; ++k)
        sum += kernel[k + r] = float(std::exp(-0.5 * k * k / (sigma * sigma)));
    for (float& k : kernel)
        k /= sum;
    std::vector<float> b(a.size());
    const int dims[3] = {nx, ny, nz};
    const std::size_t strides[3] = {1, std::size_t(nx), std::size_t(nx) * ny};
    for (int axis = 0; axis < 3; ++axis) {
        for (int z = 0; z < nz; ++z)
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    const int pos[3] = {x, y, z};
                    const std::size_t base = (std::size_t(z) * ny + y) * nx + x;
                    float s = 0;
                    for (int k = -r; k <= r; ++k) {
                        int q = pos[axis] + k;
                        if (axis == 2 && q < 0)
                            q = -q - 1;
                        if (q < 0 || q >= dims[axis])
                            continue;
                        s += kernel[k + r] * a[base + (std::ptrdiff_t(q) - pos[axis]) * std::ptrdiff_t(strides[axis])];
                    }
                    b[base] = s;
                }
        a.swap(b);
    }
    return a;
}

} // namespace

Result<ScanGrid> carve(const std::vector<ScanView>& views, const ScanOptions& options, const ScanProgress& progress)
{
    using R = Result<ScanGrid>;
    const auto report = [&](double f) { return !progress || progress(f); };
    if (views.empty())
        return R::failure(ErrorCode::InvalidArgument, "There are no photos to carve with.", "carve: no views");
    std::vector<ViewMaps> maps(views.size());
    parallelFor(int(views.size()), [&](int u) { maps[u] = viewMaps(views[u]); });
    if (!report(0.1))
        return cancelled<R>();

    const double W = std::max(options.paper.widthMm, options.paper.heightMm);
    const double H = std::min(options.paper.widthMm, options.paper.heightMm);
    const double height = carveHeight(options);

    // Coarse pass over the whole sheet (conservative: a voxel stays unless a
    // photo sees all of it as background).
    const double cv = options.coarseVoxelMm;
    const int cx = std::max(1, int(std::ceil(W / cv))), cy = std::max(1, int(std::ceil(H / cv)));
    const int cz = std::max(1, int(std::ceil(height / cv)));
    std::vector<std::uint8_t> coarse(std::size_t(cx) * cy * cz, 0);
    for (int k = 0; k < cz; ++k) {
        for (int j = 0; j < cy; ++j)
            for (int i = 0; i < cx; ++i) {
                const Vec3 p{-W / 2 + (i + 0.5) * cv, -H / 2 + (j + 0.5) * cv, (k + 0.5) * cv};
                bool carved = false, seen = false;
                for (std::size_t u = 0; u < views.size() && !carved; ++u) {
                    const Hit hit = look(views[u].camera, p);
                    if (hit.pixel < 0)
                        continue;
                    const double half = 0.87 * cv * hit.pxPerMm;
                    carved = maps[u].toNonBackground[hit.pixel] >= half + options.carveMarginPx;
                    seen = seen || maps[u].toObject[hit.pixel] <= half;
                }
                coarse[(std::size_t(k) * cy + j) * cx + i] = !carved && seen;
            }
        if (!report(0.1 + 0.2 * (k + 1) / cz))
            return cancelled<R>();
    }
    keepLargestComponent(coarse, cx, cy, cz);
    int lo[3] = {cx, cy, cz}, hi[3] = {-1, -1, -1};
    for (int k = 0; k < cz; ++k)
        for (int j = 0; j < cy; ++j)
            for (int i = 0; i < cx; ++i)
                if (coarse[(std::size_t(k) * cy + j) * cx + i]) {
                    lo[0] = std::min(lo[0], i), hi[0] = std::max(hi[0], i);
                    lo[1] = std::min(lo[1], j), hi[1] = std::max(hi[1], j);
                    lo[2] = std::min(lo[2], k), hi[2] = std::max(hi[2], k);
                }
    if (hi[0] < 0)
        return R::failure(ErrorCode::EmptyResult,
                          "Nothing was left after carving: the photos do not agree on an object standing on the paper.",
                          "carve: coarse pass empty");
    std::vector<std::string> warnings;
    if (hi[2] >= cz - 1)
        warnings.push_back("The object reaches the top of the scanned space (" + format(height)
                           + " mm): its top is cut off. Scan with a greater height.");

    // The fine grid around what is left, with a margin.
    const double m = options.boxMarginMm;
    const Vec3 boxMin{std::max(-W / 2, -W / 2 + lo[0] * cv - m), std::max(-H / 2, -H / 2 + lo[1] * cv - m), 0.0};
    const Vec3 boxMax{std::min(W / 2, -W / 2 + (hi[0] + 1) * cv + m), std::min(H / 2, -H / 2 + (hi[1] + 1) * cv + m),
                      std::min(height, (hi[2] + 1) * cv + m)};
    const Vec3 size = boxMax - boxMin;
    const double longest = std::max({size.x, size.y, size.z});
    const double voxel = std::max(options.fineVoxelMm, longest / std::max(8, options.maxFineCells));
    ScanGrid grid;
    grid.voxel = voxel;
    grid.nx = std::max(1, int(std::ceil(size.x / voxel)));
    grid.ny = std::max(1, int(std::ceil(size.y / voxel)));
    grid.nz = std::max(1, int(std::ceil(size.z / voxel)));
    grid.origin = {boxMin.x + 0.5 * (size.x - grid.nx * voxel), boxMin.y + 0.5 * (size.y - grid.ny * voxel), 0.0};
    std::vector<std::uint8_t> occ(std::size_t(grid.nx) * grid.ny * grid.nz, 0);
    std::atomic<int> slices{0};
    std::atomic<bool> stop{false};
    parallelFor(grid.nz, [&](int k) {
        if (stop)
            return;
        for (int j = 0; j < grid.ny; ++j)
            for (int i = 0; i < grid.nx; ++i) {
                const Vec3 p = grid.sampleCenter(i, j, k);
                bool carved = false, seen = false;
                int background = 0;
                for (std::size_t u = 0; u < views.size() && !carved; ++u) {
                    const Hit hit = look(views[u].camera, p);
                    if (hit.pixel < 0)
                        continue;
                    const int d = maps[u].toNonBackground[hit.pixel];
                    if (d >= options.carveMarginPx || (d >= 1 && ++background >= 2))
                        carved = true;
                    seen = seen || views[u].mask[std::size_t(hit.pixel)] == kMaskObject;
                }
                occ[grid.index(i, j, k)] = !carved && seen;
            }
        ++slices;
    });
    if (!report(0.85))
        return cancelled<R>();
    keepLargestComponent(occ, grid.nx, grid.ny, grid.nz);
    std::size_t count = 0;
    for (auto v : occ)
        count += v;
    if (count < 8)
        return R::failure(ErrorCode::EmptyResult,
                          "Nothing was left after carving: the photos do not agree on an object standing on the paper.",
                          "carve: fine pass empty");
    grid.field = blur(occ, grid.nx, grid.ny, grid.nz, options.fieldSigmaVoxels);
    for (float& f : grid.field)
        f -= 0.5f;
    if (!report(1))
        return cancelled<R>();
    return R::success(std::move(grid), std::move(warnings));
}

// ------------------------------------------------------------------- surface

Result<ScanSurface> extractSurface(const ScanGrid& field, const ScanOptions& options, const ScanProgress& progress)
{
    using R = Result<ScanSurface>;
    const auto report = [&](double f) { return !progress || progress(f); };
    if (field.empty() || field.field.size() != std::size_t(field.nx) * field.ny * field.nz)
        return R::failure(ErrorCode::InvalidArgument, "There is no carved shape to make a surface of.", "extractSurface: empty grid");
    std::vector<char> bottom;
    ScanSurface out;
    out.mesh = surfaceNets(field, true, &bottom);
    if (out.mesh.triangles.empty())
        return R::failure(ErrorCode::EmptyResult, "The carved shape is empty.", "extractSurface: no surface");
    if (!report(0.3))
        return cancelled<R>();
    taubinSmooth(out.mesh, options.taubinIterations, &bottom);
    if (!report(0.45))
        return cancelled<R>();
    // Decimated; a surface crossing itself (rare: a thin part) cannot become
    // a body, so it is decimated again more carefully, then less far.
    const ScanMesh full = out.mesh;
    const std::vector<char> fullBottom = bottom;
    const int target = std::max(4, options.targetTriangles);
    const std::array<std::pair<int, double>, 3> attempts{{{target, 0.2}, {target, 0.7}, {2 * target, 0.85}}};
    for (std::size_t a = 0; a < attempts.size(); ++a) {
        if (a > 0) {
            out.mesh = full;
            bottom = fullBottom;
        }
        decimate(out.mesh, attempts[a].first, &bottom, attempts[a].second);
        if (selfIntersections(out.mesh) == 0)
            break;
        if (a + 1 == attempts.size())
            out.warnings.push_back("The scanned surface crosses itself somewhere: it may not become a body.");
    }
    if (!report(0.95))
        return cancelled<R>();
    const MeshReport check = inspectMesh(out.mesh);
    if (!check.oriented() || !(check.volume > 0))
        return R::failure(ErrorCode::InvalidResultShape, "The scanned surface came out broken. Try again with other photos.",
                          "extractSurface: boundary " + std::to_string(check.boundaryEdges) + ", non-manifold "
                              + std::to_string(check.nonManifoldEdges) + ", misoriented "
                              + std::to_string(check.misorientedEdges) + ", volume " + std::to_string(check.volume));
    out.volumeMm3 = check.volume;
    out.boundsMin = check.min;
    out.boundsMax = check.max;
    out.triangleCount = check.triangles;
    if (!report(1))
        return cancelled<R>();
    std::vector<std::string> warnings = out.warnings;
    return R::success(std::move(out), std::move(warnings));
}

std::vector<double> silhouetteAgreement(const std::vector<ScanView>& views, const ScanGrid& field)
{
    std::vector<double> out(views.size(), 0.0);
    if (field.empty())
        return out;
    for (std::size_t u = 0; u < views.size(); ++u) {
        const ScanCamera& c = views[u].camera;
        // Each inside voxel paints the pixels its cube covers (about).
        std::vector<std::uint8_t> hit(std::size_t(c.width) * c.height, 0);
        for (int k = 0; k < field.nz; ++k)
            for (int j = 0; j < field.ny; ++j)
                for (int i = 0; i < field.nx; ++i) {
                    if (field.at(i, j, k) <= 0)
                        continue;
                    const Hit h = look(c, field.sampleCenter(i, j, k));
                    if (h.pixel < 0)
                        continue;
                    const int px = int(h.pixel % c.width), py = int(h.pixel / c.width);
                    const int r = std::max(1, int(std::ceil(0.6 * field.voxel * h.pxPerMm)));
                    for (int y = std::max(0, py - r); y <= std::min(c.height - 1, py + r); ++y)
                        for (int x = std::max(0, px - r); x <= std::min(c.width - 1, px + r); ++x)
                            hit[std::size_t(y) * c.width + x] = 1;
                }
        long object = 0, covered = 0;
        for (std::size_t p = 0; p < hit.size(); ++p) {
            if (views[u].mask[p] != kMaskObject)
                continue;
            ++object;
            covered += hit[p];
        }
        out[u] = object > 0 ? double(covered) / object : 0;
    }
    return out;
}

Result<ScanResult> runScan(const std::vector<ScanPhoto>& photos, const ScanOptions& options, const ScanProgress& progress)
{
    using R = Result<ScanResult>;
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    const auto stage = [&](double from, double to) -> ScanProgress {
        if (!progress)
            return {};
        return [&progress, from, to](double f) { return progress(from + (to - from) * f); };
    };
    ScanResult out;
    const auto t0 = Clock::now();
    auto calibration = calibrate(photos, options, stage(0, 0.5));
    if (!calibration)
        return R::failureFrom(calibration);
    const auto t1 = Clock::now();
    auto hull = carve(calibration->views, options, stage(0.5, 0.8));
    if (!hull)
        return R::failureFrom(hull);
    const auto t2 = Clock::now();
    auto surface = extractSurface(hull.value(), options, stage(0.8, 1.0));
    if (!surface)
        return R::failureFrom(surface);
    const auto t3 = Clock::now();
    out.calibrateMs = ms(t0, t1);
    out.carveMs = ms(t1, t2);
    out.surfaceMs = ms(t2, t3);
    out.photos = calibration->photos;
    out.focal35mm = calibration->focal35mm;
    out.focalEstimated = calibration->focalEstimated;
    const auto agreement = silhouetteAgreement(calibration->views, hull.value());
    for (std::size_t u = 0; u < calibration->views.size(); ++u)
        out.photos[calibration->views[u].photoIndex].silhouetteAgreement = agreement[u];
    for (const auto* list : {&calibration.warnings(), &hull.warnings(), &surface.warnings()})
        for (const std::string& w : *list)
            if (std::find(out.warnings.begin(), out.warnings.end(), w) == out.warnings.end())
                out.warnings.push_back(w);
    out.surface = std::move(surface.value());
    out.hull = std::move(hull.value());
    std::vector<std::string> warnings = out.warnings;
    return R::success(std::move(out), std::move(warnings));
}

} // namespace os::measure
