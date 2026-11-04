// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScanStereo.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace os::measure {

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t0)
{
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// ------------------------------------------------------------------ helpers

struct V3f {
    float x = 0, y = 0, z = 0;
    float dot(V3f o) const { return x * o.x + y * o.y + z * o.z; }
    V3f operator*(float s) const { return {x * s, y * s, z * s}; }
    V3f operator+(V3f o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3f normalized() const
    {
        const float l = std::sqrt(dot(*this));
        return l > 0 ? V3f{x / l, y / l, z / l} : V3f{};
    }
};

// Runs fn(i) for i in [0, count) on up to `threads` threads (the caller's
// among them), handing out indices one at a time.
template <class F>
void parallelFor(int count, int threads, const F& fn)
{
    if (count <= 0)
        return;
    const int n = std::min(threads, count);
    if (n <= 1) {
        for (int i = 0; i < count; ++i)
            fn(i);
        return;
    }
    std::atomic<int> next{0};
    auto worker = [&] {
        for (int i = next++; i < count; i = next++)
            fn(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(std::size_t(n - 1));
    for (int t = 1; t < n; ++t)
        pool.emplace_back(worker);
    worker();
    for (auto& th : pool)
        th.join();
}

// A small hash-based generator: each pixel and pass gets its own stream, so
// the result does not depend on how rows are spread over threads.
std::uint64_t splitmix(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

struct Rng {
    std::uint64_t s;
    float uniform() // [0, 1)
    {
        s = splitmix(s);
        return float(s >> 40) * (1.0f / 16777216.0f);
    }
    float symmetric() { return 2.0f * uniform() - 1.0f; } // [-1, 1)
};

Rng makeRng(int view, int pixel, int pass)
{
    return Rng{splitmix((std::uint64_t(unsigned(view)) << 48) ^ (std::uint64_t(unsigned(pass)) << 32) ^
                        std::uint64_t(unsigned(pixel)))};
}

// ------------------------------------------------------------------ the hull

struct HullField {
    const ScanGrid* g = nullptr;
    float outside = -1; // the value beyond the grid
    float positiveAmp = 1;
    float negativeAmp = 1;

    float cell(int i, int j, int k) const
    {
        return g->field[(std::size_t(k) * std::size_t(g->ny) + std::size_t(j)) * std::size_t(g->nx) + std::size_t(i)];
    }

    // Trilinear between sample centers; beyond half a voxel outside the grid
    // the field is `outside`.
    float sample(Vec3 p) const
    {
        double u = (p.x - g->origin.x) / g->voxel - 0.5;
        double v = (p.y - g->origin.y) / g->voxel - 0.5;
        double w = (p.z - g->origin.z) / g->voxel - 0.5;
        if (!(u >= -0.5 && v >= -0.5 && w >= -0.5 && u <= g->nx - 0.5 && v <= g->ny - 0.5 && w <= g->nz - 0.5))
            return outside;
        u = std::clamp(u, 0.0, double(g->nx - 1));
        v = std::clamp(v, 0.0, double(g->ny - 1));
        w = std::clamp(w, 0.0, double(g->nz - 1));
        const int i = std::min(int(u), g->nx - 2);
        const int j = std::min(int(v), g->ny - 2);
        const int k = std::min(int(w), g->nz - 2);
        const float a = float(u - i), b = float(v - j), c = float(w - k);
        const float c00 = cell(i, j, k) * (1 - a) + cell(i + 1, j, k) * a;
        const float c10 = cell(i, j + 1, k) * (1 - a) + cell(i + 1, j + 1, k) * a;
        const float c01 = cell(i, j, k + 1) * (1 - a) + cell(i + 1, j, k + 1) * a;
        const float c11 = cell(i, j + 1, k + 1) * (1 - a) + cell(i + 1, j + 1, k + 1) * a;
        return (c00 * (1 - b) + c10 * b) * (1 - c) + (c01 * (1 - b) + c11 * b) * c;
    }

    // Points into the inside (the field grows inward).
    Vec3 gradient(Vec3 p) const
    {
        const double h = g->voxel;
        return Vec3{sample(p + Vec3{h, 0, 0}) - sample(p - Vec3{h, 0, 0}),
                    sample(p + Vec3{0, h, 0}) - sample(p - Vec3{0, h, 0}),
                    sample(p + Vec3{0, 0, h}) - sample(p - Vec3{0, 0, h})};
    }

    // The parameter range of o + t d inside the grid's box (t > 0).
    bool boxRange(Vec3 o, Vec3 d, double& t0, double& t1) const
    {
        const double lo[3] = {g->origin.x, g->origin.y, g->origin.z};
        const double hi[3] = {g->origin.x + g->nx * g->voxel, g->origin.y + g->ny * g->voxel,
                              g->origin.z + g->nz * g->voxel};
        const double oo[3] = {o.x, o.y, o.z};
        const double dd[3] = {d.x, d.y, d.z};
        t0 = 1e-6;
        t1 = std::numeric_limits<double>::max();
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dd[a]) < 1e-12) {
                if (oo[a] < lo[a] || oo[a] > hi[a])
                    return false;
                continue;
            }
            double ta = (lo[a] - oo[a]) / dd[a];
            double tb = (hi[a] - oo[a]) / dd[a];
            if (ta > tb)
                std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
        }
        return t1 > t0;
    }
};

// ------------------------------------------------------------------ cameras

struct Cam {
    double fx = 0, fy = 0, cx = 0, cy = 0;
    int w = 0, h = 0;
    std::array<double, 9> R{};
    Vec3 t;
    Vec3 center;

    Vec3 toCamera(Vec3 X) const
    {
        return {R[0] * X.x + R[1] * X.y + R[2] * X.z + t.x, R[3] * X.x + R[4] * X.y + R[5] * X.z + t.y,
                R[6] * X.x + R[7] * X.y + R[8] * X.z + t.z};
    }
    Vec3 toWorldDir(Vec3 c) const // R^T c
    {
        return {R[0] * c.x + R[3] * c.y + R[6] * c.z, R[1] * c.x + R[4] * c.y + R[7] * c.z,
                R[2] * c.x + R[5] * c.y + R[8] * c.z};
    }
    // The camera-frame ray through pixel (u, v) with z = 1.
    Vec3 ray(double u, double v) const { return {(u - cx) / fx, (v - cy) / fy, 1.0}; }
};

// One reference photo at stereo resolution, with its depth map.
struct StereoView {
    int source = -1; // index into the input views
    Cam cam;
    std::vector<float> grey;           // w * h, 0..255
    std::vector<std::uint8_t> usable;  // pixels to search (window inside mask & hull, textured)
    std::vector<float> dmin, dmax;     // the hull's depth range along each pixel's ray
    std::vector<float> hullDepth;      // where the ray enters the hull
    std::vector<V3f> hullNormal;       // the hull's outward normal there (camera frame)
    std::vector<float> refMean, refNorm; // the window's mean and sqrt(sum of squared deviations)
    std::vector<float> depth, cost;    // the PatchMatch state
    std::vector<V3f> normal;           // camera frame, facing the camera
    std::vector<float> confident;      // depth after the confidence checks (0: none)
    std::vector<float> fused;          // depth after the consistency check (0: none)
    std::vector<float> weight;         // fusion weight of fused pixels
    std::vector<int> neighbours;       // indices into the stereo views

    // For each neighbour: H(plane) = M + e a^T maps this view's pixels to the
    // neighbour's (M = K_j R_rel K_i^-1, e = K_j t_rel).
    struct Pair {
        int view = -1;
        float M[9]{};
        float e[3]{};
    };
    std::vector<Pair> pairs;
};

// Area-averaging resampling of a w x h float image to ow x oh.
std::vector<float> resampleArea(const std::vector<float>& src, int w, int h, int ow, int oh)
{
    struct Tap {
        int index;
        float weight;
    };
    auto taps = [](int n, int on) {
        std::vector<std::vector<Tap>> out(static_cast<std::size_t>(on));
        const double scale = double(n) / on;
        for (int o = 0; o < on; ++o) {
            const double a = o * scale, b = (o + 1) * scale;
            double total = 0;
            for (int i = int(std::floor(a)); i < int(std::ceil(b)) && i < n; ++i) {
                const double cover = std::min(b, double(i + 1)) - std::max(a, double(i));
                if (cover > 1e-9) {
                    out[std::size_t(o)].push_back({i, float(cover)});
                    total += cover;
                }
            }
            for (Tap& t : out[std::size_t(o)])
                t.weight = float(t.weight / total);
        }
        return out;
    };
    const auto tx = taps(w, ow);
    const auto ty = taps(h, oh);
    std::vector<float> rows(std::size_t(ow) * std::size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < ow; ++x) {
            float s = 0;
            for (const Tap& t : tx[std::size_t(x)])
                s += src[std::size_t(y) * std::size_t(w) + std::size_t(t.index)] * t.weight;
            rows[std::size_t(y) * std::size_t(ow) + std::size_t(x)] = s;
        }
    std::vector<float> out(std::size_t(ow) * std::size_t(oh));
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            float s = 0;
            for (const Tap& t : ty[std::size_t(y)])
                s += rows[std::size_t(t.index) * std::size_t(ow) + std::size_t(x)] * t.weight;
            out[std::size_t(y) * std::size_t(ow) + std::size_t(x)] = s;
        }
    return out;
}

float angleDeg(Vec3 a, Vec3 b)
{
    const double c = std::clamp(a.normalized().dot(b.normalized()), -1.0, 1.0);
    return float(std::acos(c) * 180.0 / kPi);
}

// ------------------------------------------------------------------ the engine

class Engine {
public:
    Engine(const std::vector<ScanView>& views, const ScanGrid& hull, const StereoOptions& options,
           const std::function<bool(double)>& progress, StereoStats& stats)
        : input_(views), grid_(hull), o_(options), progress_(progress), stats_(stats)
    {
        threads_ = o_.threads > 0 ? o_.threads : std::max(1, int(std::thread::hardware_concurrency()) / 2);
        radius_ = std::clamp(o_.windowRadius, 1, 10);
        step_ = std::clamp(o_.windowStep, 1, radius_);
        for (int dy = -radius_; dy <= radius_; dy += step_)
            for (int dx = -radius_; dx <= radius_; dx += step_)
                offsets_.push_back({dx, dy});
        samples_ = int(offsets_.size());
    }

    // false: cancelled or nothing to do (the hull stands).
    bool run(ScanGrid& out)
    {
        const auto t0 = Clock::now();
        if (!setUpHull())
            return false;
        prepareViews();
        if (views_.size() < 2)
            return false;
        selectNeighbours();
        int used = 0;
        for (const StereoView& v : views_)
            used += v.pairs.empty() ? 0 : 1;
        stats_.viewsUsed = used;
        if (used == 0)
            return false;
        if (!report(0.03))
            return false;

        const auto tDepth = Clock::now();
        // Work units: per view, the range pass, the initial pass, two half
        // sweeps per iteration and the final pass.
        unitsPerView_ = 3 + 2 * std::max(0, o_.iterations);
        totalUnits_ = unitsPerView_ * int(views_.size());
        for (std::size_t i = 0; i < views_.size(); ++i)
            if (!depthMap(int(i)))
                return false;
        stats_.secondsDepth = secondsSince(tDepth);

        const auto tFilter = Clock::now();
        checkConsistency();
        stats_.secondsFilter = secondsSince(tFilter);
        if (!report(0.82))
            return false;

        const auto tFusion = Clock::now();
        if (!fuse(out))
            return false;
        stats_.secondsFusion = secondsSince(tFusion);
        stats_.secondsTotal = secondsSince(t0);
        return report(1.0);
    }

private:
    bool report(double fraction)
    {
        if (progress_ && !progress_(std::clamp(fraction, 0.0, 1.0))) {
            stats_.cancelled = true;
            return false;
        }
        return true;
    }

    bool reportUnit()
    {
        ++unitsDone_;
        return report(0.03 + 0.77 * double(unitsDone_) / std::max(1, totalUnits_));
    }

    // ---- the hull's scale and the object's center

    bool setUpHull()
    {
        const ScanGrid& g = grid_;
        if (g.nx < 2 || g.ny < 2 || g.nz < 2 || !(g.voxel > 0) ||
            g.field.size() != std::size_t(g.nx) * std::size_t(g.ny) * std::size_t(g.nz))
            return false;
        float lo = 0, hi = 0;
        Vec3 sum;
        double count = 0;
        for (int k = 0; k < g.nz; ++k)
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    const float f = g.field[index(i, j, k)];
                    lo = std::min(lo, f);
                    hi = std::max(hi, f);
                    if (f > 0) {
                        const Vec3 p = center(i, j, k);
                        sum += p;
                        count += 1;
                        hullMin_ = count == 1 ? p : Vec3{std::min(hullMin_.x, p.x), std::min(hullMin_.y, p.y), std::min(hullMin_.z, p.z)};
                        hullMax_ = count == 1 ? p : Vec3{std::max(hullMax_.x, p.x), std::max(hullMax_.y, p.y), std::max(hullMax_.z, p.z)};
                    }
                }
        if (count < 1 || hi <= 0)
            return false;
        hull_.g = &grid_;
        hull_.positiveAmp = hi;
        hull_.negativeAmp = lo < 0 ? -lo : hi;
        hull_.outside = -hull_.negativeAmp;
        objectCenter_ = sum / count;

        // The field's change per voxel across its surface (median over the
        // sample pairs whose signs differ): the TSDF is brought to this slope.
        std::vector<float> steps;
        for (int k = 0; k < g.nz; ++k)
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    const float f = g.field[index(i, j, k)];
                    if (i + 1 < g.nx && ((f > 0) != (g.field[index(i + 1, j, k)] > 0)))
                        steps.push_back(std::abs(f - g.field[index(i + 1, j, k)]));
                    if (j + 1 < g.ny && ((f > 0) != (g.field[index(i, j + 1, k)] > 0)))
                        steps.push_back(std::abs(f - g.field[index(i, j + 1, k)]));
                    if (k + 1 < g.nz && ((f > 0) != (g.field[index(i, j, k + 1)] > 0)))
                        steps.push_back(std::abs(f - g.field[index(i, j, k + 1)]));
                }
        if (!steps.empty()) {
            auto mid = steps.begin() + std::ptrdiff_t(steps.size() / 2);
            std::nth_element(steps.begin(), mid, steps.end());
            slopePerVoxel_ = *mid;
        }
        if (!(slopePerVoxel_ > 0))
            slopePerVoxel_ = hull_.positiveAmp;
        return true;
    }

    std::size_t index(int i, int j, int k) const
    {
        return (std::size_t(k) * std::size_t(grid_.ny) + std::size_t(j)) * std::size_t(grid_.nx) + std::size_t(i);
    }
    Vec3 center(int i, int j, int k) const
    {
        return grid_.origin + Vec3{(i + 0.5) * grid_.voxel, (j + 0.5) * grid_.voxel, (k + 0.5) * grid_.voxel};
    }

    // ---- the photos at stereo resolution

    void prepareViews()
    {
        for (std::size_t s = 0; s < input_.size(); ++s) {
            const ScanView& in = input_[s];
            const ScanCamera& c = in.camera;
            const std::size_t pixels = std::size_t(std::max(0, c.width)) * std::size_t(std::max(0, c.height));
            if (c.width < 16 || c.height < 16 || in.rgb.size() != pixels * 3 || !(c.fx > 0) || !(c.fy > 0))
                continue;
            StereoView v;
            v.source = int(s);
            // The hull's box as this photo sees it: stereo works at the
            // resolution the object needs, not the photo's.
            double extent = 0;
            {
                double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
                bool front = true;
                for (int corner = 0; corner < 8; ++corner) {
                    const Vec3 X{corner & 1 ? hullMax_.x : hullMin_.x, corner & 2 ? hullMax_.y : hullMin_.y,
                                 corner & 4 ? hullMax_.z : hullMin_.z};
                    if (c.depth(X) <= 1e-6) {
                        front = false;
                        break;
                    }
                    const Vec2 q = c.project(X);
                    u0 = std::min(u0, q.x);
                    u1 = std::max(u1, q.x);
                    v0 = std::min(v0, q.y);
                    v1 = std::max(v1, q.y);
                }
                extent = front ? std::max(u1 - u0, v1 - v0) : 0;
            }
            double scale = std::min(1.0, double(std::max(64, o_.maxImageWidth)) / c.width);
            if (extent > 0)
                scale = std::min(scale, double(std::max(32, o_.maxObjectPixels)) / extent);
            v.cam.w = std::max(16, int(std::lround(c.width * scale)));
            v.cam.h = std::max(16, int(std::lround(c.height * scale)));
            const double sx = double(v.cam.w) / c.width, sy = double(v.cam.h) / c.height;
            v.cam.fx = c.fx * sx;
            v.cam.cx = c.cx * sx;
            v.cam.fy = c.fy * sy;
            v.cam.cy = c.cy * sy;
            v.cam.R = c.R;
            v.cam.t = c.t;
            v.cam.center = c.center();
            views_.push_back(std::move(v));
        }
        // Grey images, masks, depth ranges and window statistics, a view per task.
        parallelFor(int(views_.size()), threads_, [&](int i) { prepareView(views_[std::size_t(i)]); });
    }

    void prepareView(StereoView& v)
    {
        const ScanView& in = input_[std::size_t(v.source)];
        const int W = in.camera.width, H = in.camera.height;
        const int w = v.cam.w, h = v.cam.h;
        const std::size_t n = std::size_t(w) * std::size_t(h);
        std::vector<float> grey(std::size_t(W) * std::size_t(H));
        for (std::size_t p = 0; p < grey.size(); ++p)
            grey[p] = 0.299f * in.rgb[3 * p] + 0.587f * in.rgb[3 * p + 1] + 0.114f * in.rgb[3 * p + 2];
        v.grey = (w == W && h == H) ? std::move(grey) : resampleArea(grey, W, H, w, h);

        // Pixels to search: the mask says object (or unknown) and the ray
        // meets the hull.
        const bool hasMask = in.mask.size() == std::size_t(W) * std::size_t(H);
        std::vector<std::uint8_t> inside(n, 0);
        v.dmin.assign(n, 0);
        v.dmax.assign(n, 0);
        v.hullDepth.assign(n, 0);
        v.hullNormal.assign(n, V3f{});
        const double voxel = grid_.voxel;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
                if (hasMask) {
                    const int sx = std::min(W - 1, int((x + 0.5) * W / w));
                    const int sy = std::min(H - 1, int((y + 0.5) * H / h));
                    if (in.mask[std::size_t(sy) * std::size_t(W) + std::size_t(sx)] == 0)
                        continue;
                }
                const Vec3 r = v.cam.ray(x + 0.5, y + 0.5);
                const Vec3 d = v.cam.toWorldDir(r); // the parameter is the camera depth
                double t0 = 0, t1 = 0;
                if (!hull_.boxRange(v.cam.center, d, t0, t1))
                    continue;
                const double stepT = 0.8 * voxel / d.length();
                auto at = [&](double t) { return hull_.sample(v.cam.center + d * t); };
                auto refine = [&](double outsideT, double insideT) {
                    for (int it = 0; it < 6; ++it) {
                        const double m = 0.5 * (outsideT + insideT);
                        if (at(m) > 0)
                            insideT = m;
                        else
                            outsideT = m;
                    }
                    return 0.5 * (outsideT + insideT);
                };
                double entry = -1;
                for (double t = t0; t <= t1; t += stepT)
                    if (at(t) > 0) {
                        entry = refine(std::max(t0, t - stepT), t);
                        break;
                    }
                if (entry < 0)
                    continue;
                double exitT = entry;
                for (double t = t1; t > entry; t -= stepT)
                    if (at(t) > 0) {
                        exitT = refine(std::min(t1, t + stepT), t);
                        break;
                    }
                const double pad = 0.5 * voxel / d.length();
                v.dmin[p] = float(std::max(1e-3, entry - pad));
                v.dmax[p] = float(exitT + pad);
                v.hullDepth[p] = float(entry);
                const Vec3 g = hull_.gradient(v.cam.center + d * entry);
                // Outward = -gradient, in the camera frame.
                const Vec3 nc = v.cam.toCamera(-g) - v.cam.toCamera(Vec3{});
                V3f nn{float(nc.x), float(nc.y), float(nc.z)};
                nn = nn.normalized();
                const V3f rr = V3f{float(r.x), float(r.y), float(r.z)}.normalized();
                if (nn.dot(rr) > -0.1f)
                    nn = rr * -1.0f;
                v.hullNormal[p] = nn;
                inside[p] = 1;
            }

        // The window must lie wholly inside: erode by the window's radius
        // (an integral image of the inside pixels).
        std::vector<int> sat(std::size_t(w + 1) * std::size_t(h + 1), 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                sat[std::size_t(y + 1) * std::size_t(w + 1) + std::size_t(x + 1)] =
                    inside[std::size_t(y) * std::size_t(w) + std::size_t(x)] +
                    sat[std::size_t(y) * std::size_t(w + 1) + std::size_t(x + 1)] +
                    sat[std::size_t(y + 1) * std::size_t(w + 1) + std::size_t(x)] -
                    sat[std::size_t(y) * std::size_t(w + 1) + std::size_t(x)];
        const int r = radius_;
        const int full = (2 * r + 1) * (2 * r + 1);
        v.usable.assign(n, 0);
        v.refMean.assign(n, 0);
        v.refNorm.assign(n, 0);
        const float minVarN = float(o_.minTextureStd * o_.minTextureStd) * float(samples_);
        long long searched = 0;
        for (int y = r; y < h - r; ++y)
            for (int x = r; x < w - r; ++x) {
                const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
                if (!inside[p])
                    continue;
                const int x0 = x - r, y0 = y - r, x1 = x + r + 1, y1 = y + r + 1;
                const int cnt = sat[std::size_t(y1) * std::size_t(w + 1) + std::size_t(x1)] -
                                sat[std::size_t(y0) * std::size_t(w + 1) + std::size_t(x1)] -
                                sat[std::size_t(y1) * std::size_t(w + 1) + std::size_t(x0)] +
                                sat[std::size_t(y0) * std::size_t(w + 1) + std::size_t(x0)];
                if (cnt != full)
                    continue;
                ++searched;
                float s = 0, ss = 0;
                for (const auto& off : offsets_) {
                    const float g = v.grey[std::size_t(y + off.second) * std::size_t(w) + std::size_t(x + off.first)];
                    s += g;
                    ss += g * g;
                }
                const float mean = s / float(samples_);
                const float varN = ss - float(samples_) * mean * mean;
                if (varN < minVarN)
                    continue; // too flat to match
                v.usable[p] = 1;
                v.refMean[p] = mean;
                v.refNorm[p] = std::sqrt(varN);
            }
        searchedPixels_ += searched;
        v.depth.assign(n, 0);
        v.cost.assign(n, 2.0f);
        v.normal.assign(n, V3f{});
        v.confident.assign(n, 0);
        v.fused.assign(n, 0);
        v.weight.assign(n, 0);
    }

    // ---- view selection

    void selectNeighbours()
    {
        stats_.depthPixels = searchedPixels_;
        for (std::size_t i = 0; i < views_.size(); ++i) {
            StereoView& v = views_[i];
            const Vec3 ri = objectCenter_ - v.cam.center;
            struct Cand {
                int view;
                double score;
                double angle;
            };
            std::vector<Cand> inRange, other;
            for (std::size_t j = 0; j < views_.size(); ++j) {
                if (j == i)
                    continue;
                const Vec3 rj = objectCenter_ - views_[j].cam.center;
                const double a = angleDeg(ri, rj);
                const double ratio = rj.length() / std::max(1e-9, ri.length());
                const double scalePenalty = std::abs(std::log(ratio)) * 20.0; // degrees per e-fold
                const Cand c{int(j), std::abs(a - o_.bestBaselineDeg) + scalePenalty, a};
                if (a >= o_.minBaselineDeg && a <= o_.maxBaselineDeg && ratio < 2.0 && ratio > 0.5)
                    inRange.push_back(c);
                else if (a >= 3.0 && a <= 60.0)
                    other.push_back(c);
            }
            auto byScore = [](const Cand& a, const Cand& b) { return a.score < b.score; };
            std::sort(inRange.begin(), inRange.end(), byScore);
            std::sort(other.begin(), other.end(), byScore);
            const int want = std::clamp(o_.neighbours, 1, 4);
            for (const Cand& c : inRange)
                if (int(v.neighbours.size()) < want)
                    v.neighbours.push_back(c.view);
            for (const Cand& c : other)
                if (int(v.neighbours.size()) < std::min(want, 2))
                    v.neighbours.push_back(c.view);
            // The pair homographies' fixed parts.
            const Cam& a = v.cam;
            for (int j : v.neighbours) {
                const Cam& b = views_[std::size_t(j)].cam;
                // R_rel = Rb Ra^T, t_rel = tb - R_rel ta.
                double Rr[9];
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        Rr[r * 3 + c] = b.R[std::size_t(r * 3)] * a.R[std::size_t(c * 3)] +
                                        b.R[std::size_t(r * 3 + 1)] * a.R[std::size_t(c * 3 + 1)] +
                                        b.R[std::size_t(r * 3 + 2)] * a.R[std::size_t(c * 3 + 2)];
                const double tr[3] = {b.t.x - (Rr[0] * a.t.x + Rr[1] * a.t.y + Rr[2] * a.t.z),
                                      b.t.y - (Rr[3] * a.t.x + Rr[4] * a.t.y + Rr[5] * a.t.z),
                                      b.t.z - (Rr[6] * a.t.x + Rr[7] * a.t.y + Rr[8] * a.t.z)};
                const double Kb[9] = {b.fx, 0, b.cx, 0, b.fy, b.cy, 0, 0, 1};
                const double Kai[9] = {1 / a.fx, 0, -a.cx / a.fx, 0, 1 / a.fy, -a.cy / a.fy, 0, 0, 1};
                double KR[9], M[9];
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        KR[r * 3 + c] = Kb[r * 3] * Rr[c] + Kb[r * 3 + 1] * Rr[3 + c] + Kb[r * 3 + 2] * Rr[6 + c];
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        M[r * 3 + c] = KR[r * 3] * Kai[c] + KR[r * 3 + 1] * Kai[3 + c] + KR[r * 3 + 2] * Kai[6 + c];
                StereoView::Pair pair;
                pair.view = j;
                for (int q = 0; q < 9; ++q)
                    pair.M[q] = float(M[q]);
                for (int r = 0; r < 3; ++r)
                    pair.e[r] = float(Kb[r * 3] * tr[0] + Kb[r * 3 + 1] * tr[1] + Kb[r * 3 + 2] * tr[2]);
                v.pairs.push_back(pair);
            }
        }
    }

    // ---- PatchMatch

    // 1 - ZNCC of the plane (depth d along pixel (x, y)'s ray, normal n in
    // the camera frame), aggregated over the neighbours: 0 perfect, 2 worst.
    float planeCost(const StereoView& v, int x, int y, float d, V3f n) const
    {
        const Cam& c = v.cam;
        const float fx = float(c.fx), fy = float(c.fy), cx = float(c.cx), cy = float(c.cy);
        const float u0 = float(x) + 0.5f, v0 = float(y) + 0.5f;
        const float rx = (u0 - cx) / fx, ry = (v0 - cy) / fy;
        const float ndr = n.x * rx + n.y * ry + n.z;
        if (!(ndr < -1e-4f) || !(d > 0))
            return 2.0f;
        const float rho = d * ndr;
        const float a0 = n.x / fx / rho, a1 = n.y / fy / rho, a2 = (n.z - n.x * cx / fx - n.y * cy / fy) / rho;
        const int w = c.w;
        const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
        const float mr = v.refMean[p], nr = v.refNorm[p];
        const float N = float(samples_);
        float costs[4];
        int nc = 0;
        for (const StereoView::Pair& pr : v.pairs) {
            const StereoView& nb = views_[std::size_t(pr.view)];
            const int nw = nb.cam.w, nh = nb.cam.h;
            const float* g = nb.grey.data();
            float H[9];
            for (int r = 0; r < 3; ++r) {
                H[r * 3] = pr.M[r * 3] + pr.e[r] * a0;
                H[r * 3 + 1] = pr.M[r * 3 + 1] + pr.e[r] * a1;
                H[r * 3 + 2] = pr.M[r * 3 + 2] + pr.e[r] * a2;
            }
            float S = 0, SS = 0, RS = 0;
            bool ok = true;
            for (const auto& off : offsets_) {
                const float u = u0 + float(off.first), vv = v0 + float(off.second);
                const float hz = H[6] * u + H[7] * vv + H[8];
                if (!(hz > 1e-6f)) {
                    ok = false;
                    break;
                }
                const float iz = 1.0f / hz;
                const float px = (H[0] * u + H[1] * vv + H[2]) * iz - 0.5f;
                const float py = (H[3] * u + H[4] * vv + H[5]) * iz - 0.5f;
                if (!(px >= 0 && py >= 0 && px < float(nw - 1) && py < float(nh - 1))) {
                    ok = false;
                    break;
                }
                const int ix = int(px), iy = int(py);
                const float ax = px - float(ix), ay = py - float(iy);
                const float* row = g + std::size_t(iy) * std::size_t(nw) + std::size_t(ix);
                const float top = row[0] + (row[1] - row[0]) * ax;
                const float bottom = row[nw] + (row[nw + 1] - row[nw]) * ax;
                const float s = top + (bottom - top) * ay;
                const float r = v.grey[p + std::size_t(std::ptrdiff_t(off.second) * w + off.first)];
                S += s;
                SS += s * s;
                RS += r * s;
            }
            float cost = 2.0f;
            if (ok) {
                const float ms = S / N;
                const float varS = SS - N * ms * ms;
                if (varS < 1e-2f * N)
                    cost = 1.5f; // flat where the plane lands: no evidence
                else
                    cost = 1.0f - std::clamp((RS - N * mr * ms) / (nr * std::sqrt(varS)), -1.0f, 1.0f);
            }
            costs[nc++] = cost;
        }
        if (nc == 0)
            return 2.0f;
        std::sort(costs, costs + nc);
        const int k = std::clamp(o_.bestNeighbours, 1, nc);
        float sum = 0;
        for (int i = 0; i < k; ++i)
            sum += costs[i];
        return sum / float(k);
    }

    static V3f rayAt(const Cam& c, int x, int y)
    {
        return V3f{float((x + 0.5 - c.cx) / c.fx), float((y + 0.5 - c.cy) / c.fy), 1.0f};
    }

    // A normal facing the camera within ~80 degrees of the ray, or nothing.
    static bool facing(V3f n, V3f rayUnit) { return n.dot(rayUnit) < -0.17f; }

    static V3f randomNormal(Rng& rng, V3f rayUnit)
    {
        for (int attempt = 0; attempt < 6; ++attempt) {
            const float z = rng.symmetric();
            const float phi = 2.0f * float(kPi) * rng.uniform();
            const float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
            V3f n{s * std::cos(phi), s * std::sin(phi), z};
            if (n.dot(rayUnit) > 0)
                n = n * -1.0f;
            if (facing(n, rayUnit))
                return n;
        }
        return rayUnit * -1.0f;
    }

    bool depthMap(int vi)
    {
        StereoView& v = views_[std::size_t(vi)];
        if (!reportUnit())
            return false;
        if (v.pairs.empty()) {
            for (int u = 1; u < unitsPerView_; ++u)
                if (!reportUnit())
                    return false;
            return true;
        }
        const int w = v.cam.w, h = v.cam.h;

        // Initial planes: the hull's own surface, or a random one.
        parallelFor(h, threads_, [&](int y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
                if (!v.usable[p])
                    continue;
                const V3f ru = rayAt(v.cam, x, y).normalized();
                Rng rng = makeRng(vi, int(p), 0);
                float bestD = v.hullDepth[p];
                V3f bestN = v.hullNormal[p];
                float best = planeCost(v, x, y, bestD, bestN);
                const float d2 = v.dmin[p] + rng.uniform() * (v.dmax[p] - v.dmin[p]);
                const V3f n2 = randomNormal(rng, ru);
                const float c2 = planeCost(v, x, y, d2, n2);
                if (c2 < best) {
                    best = c2;
                    bestD = d2;
                    bestN = n2;
                }
                v.depth[p] = bestD;
                v.normal[p] = bestN;
                v.cost[p] = best;
            }
        });
        if (!reportUnit())
            return false;

        // Red-black sweeps: a pixel takes its neighbours' planes (all of the
        // other color, so nothing it reads changes meanwhile), then tries
        // random refinements that narrow with each iteration.
        static const int kOffsets[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-5, 0}, {5, 0}, {0, -5}, {0, 5}};
        for (int it = 0; it < o_.iterations; ++it) {
            const float scale = std::pow(0.5f, float(it));
            for (int color = 0; color < 2; ++color) {
                parallelFor(h, threads_, [&](int y) {
                    for (int x = (y + color) & 1; x < w; x += 2) {
                        const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
                        if (!v.usable[p])
                            continue;
                        const V3f r = rayAt(v.cam, x, y);
                        const V3f ru = r.normalized();
                        const float lo = v.dmin[p], hi = v.dmax[p];
                        float bestD = v.depth[p];
                        V3f bestN = v.normal[p];
                        float best = v.cost[p];
                        for (const auto& off : kOffsets) {
                            const int xn = x + off[0], yn = y + off[1];
                            if (xn < 0 || yn < 0 || xn >= w || yn >= h)
                                continue;
                            const std::size_t q = std::size_t(yn) * std::size_t(w) + std::size_t(xn);
                            if (!v.usable[q])
                                continue;
                            const V3f nq = v.normal[q];
                            // The neighbour's plane, met by this pixel's ray.
                            const float rhoq = v.depth[q] * nq.dot(rayAt(v.cam, xn, yn));
                            const float den = nq.dot(r);
                            if (!(den < -1e-4f))
                                continue;
                            const float d = rhoq / den;
                            if (!(d >= lo && d <= hi) || !facing(nq, ru))
                                continue;
                            if (std::abs(d - bestD) <= 1e-5f * bestD && nq.dot(bestN) > 0.99999f)
                                continue; // the plane it already has

                            const float c = planeCost(v, x, y, d, nq);
                            if (c < best) {
                                best = c;
                                bestD = d;
                                bestN = nq;
                            }
                        }
                        Rng rng = makeRng(vi, int(p), 1 + it * 2 + color);
                        // A perturbed plane, a random depth, a random normal.
                        {
                            const float d = std::clamp(bestD + rng.symmetric() * scale * 0.25f * (hi - lo), lo, hi);
                            V3f n = (bestN + V3f{rng.symmetric(), rng.symmetric(), rng.symmetric()} * (0.5f * scale))
                                        .normalized();
                            if (!facing(n, ru))
                                n = bestN;
                            const float c = planeCost(v, x, y, d, n);
                            if (c < best) {
                                best = c;
                                bestD = d;
                                bestN = n;
                            }
                        }
                        {
                            const float d = lo + rng.uniform() * (hi - lo);
                            const float c = planeCost(v, x, y, d, bestN);
                            if (c < best) {
                                best = c;
                                bestD = d;
                            }
                        }
                        {
                            const V3f n = randomNormal(rng, ru);
                            const float c = planeCost(v, x, y, bestD, n);
                            if (c < best) {
                                best = c;
                                bestN = n;
                            }
                        }
                        v.depth[p] = bestD;
                        v.normal[p] = bestN;
                        v.cost[p] = best;
                    }
                });
                if (!reportUnit())
                    return false;
            }
        }

        // Confidence: the score, and how distinct it is (moving the depth so
        // the window shifts ~1.5 px in the neighbours must cost more).
        std::atomic<long long> confident{0};
        parallelFor(h, threads_, [&](int y) {
            long long count = 0;
            for (int x = 0; x < w; ++x) {
                const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
                if (!v.usable[p])
                    continue;
                const float d = v.depth[p];
                const V3f n = v.normal[p];
                const float c0 = v.cost[p];
                const V3f ru = rayAt(v.cam, x, y).normalized();
                if (!(c0 <= o_.maxCost) || std::abs(n.dot(ru)) < 0.1f)
                    continue;
                // Pixel motion in the neighbours per unit of depth.
                const float u0 = float(x) + 0.5f, v0 = float(y) + 0.5f;
                double motion = 0;
                for (const StereoView::Pair& pr : v.pairs) {
                    auto project = [&](float depth) {
                        const float hx = depth * (pr.M[0] * u0 + pr.M[1] * v0 + pr.M[2]) + pr.e[0];
                        const float hy = depth * (pr.M[3] * u0 + pr.M[4] * v0 + pr.M[5]) + pr.e[1];
                        const float hz = depth * (pr.M[6] * u0 + pr.M[7] * v0 + pr.M[8]) + pr.e[2];
                        return Vec2{hx / hz, hy / hz};
                    };
                    const float eps = 0.01f * d;
                    motion += (project(d + eps) - project(d)).length() / eps;
                }
                motion /= double(v.pairs.size());
                if (!(motion > 1e-6))
                    continue;
                const float delta = float(1.5 / motion);
                const float cPlus = planeCost(v, x, y, d + delta, n);
                const float cMinus = planeCost(v, x, y, d - delta, n);
                if (std::min(cPlus, cMinus) - c0 < o_.minDistinctness)
                    continue;
                v.confident[p] = d;
                v.weight[p] = std::max(0.05f, 1.0f - c0) * std::max(0.2f, std::abs(n.dot(ru)));
                ++count;
            }
            confident += count;
        });
        stats_.confidentPixels += confident.load();
        return reportUnit();
    }

    // ---- geometric consistency

    void checkConsistency()
    {
        const double tolVoxels = o_.consistencyVoxels * grid_.voxel;
        std::atomic<long long> kept{0};
        for (std::size_t i = 0; i < views_.size(); ++i) {
            StereoView& v = views_[i];
            if (v.pairs.empty())
                continue;
            const int w = v.cam.w, h = v.cam.h;
            parallelFor(h, threads_, [&](int y) {
                long long count = 0;
                for (int x = 0; x < w; ++x) {
                    const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
                    const float d = v.confident[p];
                    if (!(d > 0))
                        continue;
                    const Vec3 rc = v.cam.ray(x + 0.5, y + 0.5) * double(d);
                    const Vec3 X = v.cam.toWorldDir(rc - v.cam.t);
                    int agree = 0;
                    for (std::size_t j = 0; j < views_.size() && agree < o_.minConsistentViews; ++j) {
                        if (j == i || views_[j].pairs.empty())
                            continue;
                        const Cam& c = views_[j].cam;
                        const Vec3 xc = c.toCamera(X);
                        if (!(xc.z > 1e-6))
                            continue;
                        const double u = c.fx * xc.x / xc.z + c.cx, vv = c.fy * xc.y / xc.z + c.cy;
                        if (!(u >= 0 && vv >= 0 && u < c.w && vv < c.h))
                            continue;
                        const float dj = views_[j].confident[std::size_t(int(vv)) * std::size_t(c.w) + std::size_t(int(u))];
                        const double tol = std::max(tolVoxels, o_.consistencyPixels * xc.z / c.fx);
                        if (dj > 0 && std::abs(double(dj) - xc.z) < tol)
                            ++agree;
                    }
                    if (agree >= o_.minConsistentViews) {
                        v.fused[p] = d;
                        ++count;
                    }
                }
                kept += count;
            });
        }
        stats_.consistentPixels = kept.load();
    }

    // ---- fusion

    bool fuse(ScanGrid& out)
    {
        const ScanGrid& g = grid_;
        const std::size_t total = g.field.size();
        const double truncVoxels = o_.truncationVoxels * g.voxel;
        std::vector<float> tsdf(total, 0.0f), weight(total, 0.0f);
        std::vector<std::uint8_t> views(total, 0);
        // A slice of the grid per task, in a few batches so progress moves.
        const int batches = 6;
        for (int b = 0; b < batches; ++b) {
            const int k0 = g.nz * b / batches, k1 = g.nz * (b + 1) / batches;
            parallelFor(k1 - k0, threads_, [&](int kk) {
                const int k = k0 + kk;
                for (int j = 0; j < g.ny; ++j)
                    for (int i = 0; i < g.nx; ++i) {
                        const std::size_t idx = index(i, j, k);
                        if (!(g.field[idx] > 0))
                            continue;
                        const Vec3 X = center(i, j, k);
                        float sw = 0, swf = 0;
                        int seen = 0;
                        for (const StereoView& v : views_) {
                            if (v.pairs.empty())
                                continue;
                            const Cam& c = v.cam;
                            const Vec3 xc = c.toCamera(X);
                            if (!(xc.z > 1e-6))
                                continue;
                            const double u = c.fx * xc.x / xc.z + c.cx, vv = c.fy * xc.y / xc.z + c.cy;
                            if (!(u >= 1 && vv >= 1 && u < c.w - 1 && vv < c.h - 1))
                                continue;
                            const int ix = int(u), iy = int(vv);
                            const std::size_t p = std::size_t(iy) * std::size_t(c.w) + std::size_t(ix);
                            const float d = v.fused[p];
                            if (!(d > 0))
                                continue;
                            // At a depth edge (a step of more than the band
                            // among the pixels around), free space only in
                            // front of the nearest surface there: no carving
                            // through an edge. Elsewhere the pixel's own depth
                            // (the nearest would pull slanted faces outward).
                            const float T = float(std::max(truncVoxels, o_.truncationPixels * xc.z / c.fx));
                            float dNear = d;
                            for (int dy = -1; dy <= 1; ++dy)
                                for (int dx = -1; dx <= 1; ++dx) {
                                    const float dq =
                                        v.fused[p + std::size_t(std::ptrdiff_t(dy) * c.w + dx)];
                                    if (dq > 0)
                                        dNear = std::min(dNear, dq);
                                }
                            if (d - dNear <= T)
                                dNear = d;
                            const float f = float(xc.z) - dNear; // > 0 behind the surface (inside)
                            if (f > T)
                                continue; // hidden: no evidence
                            const float wv = v.weight[p];
                            sw += wv;
                            swf += wv * std::max(f, -T);
                            ++seen;
                        }
                        if (seen > 0) {
                            tsdf[idx] = swf / sw;
                            weight[idx] = sw;
                            views[idx] = std::uint8_t(std::min(seen, 255));
                        }
                    }
            });
            if (!report(0.82 + 0.12 * double(b + 1) / batches))
                return false;
        }

        // Accepted: enough views and weight.
        std::vector<std::uint8_t> accepted(total, 0);
        for (std::size_t idx = 0; idx < total; ++idx)
            accepted[idx] = (g.field[idx] > 0 && views[idx] >= o_.minFusionViews && weight[idx] >= o_.minFusionWeight)
                                ? 1
                                : 0;

        // A 7-point median (the voxel and its accepted face neighbours) against speckle.
        std::vector<float> smooth = tsdf;
        parallelFor(g.nz, threads_, [&](int k) {
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    const std::size_t idx = index(i, j, k);
                    if (!accepted[idx])
                        continue;
                    float vals[7];
                    int n = 0;
                    vals[n++] = tsdf[idx];
                    const int nb[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
                    for (const auto& d : nb) {
                        const int a = i + d[0], b = j + d[1], c = k + d[2];
                        if (a < 0 || b < 0 || c < 0 || a >= g.nx || b >= g.ny || c >= g.nz)
                            continue;
                        const std::size_t q = index(a, b, c);
                        if (accepted[q])
                            vals[n++] = tsdf[q];
                    }
                    if (n >= 3) {
                        std::nth_element(vals, vals + n / 2, vals + n);
                        smooth[idx] = vals[n / 2];
                    }
                }
        });

        // Combine: stereo may only carve.
        out = g;
        const float toHull = slopePerVoxel_ / float(g.voxel);
        for (std::size_t idx = 0; idx < total; ++idx)
            if (accepted[idx]) {
                // The TSDF is > 0 behind the surface, inside: the hull's sign.
                const float t = std::clamp(smooth[idx] * toHull, -hull_.negativeAmp, hull_.positiveAmp);
                out.field[idx] = std::min(g.field[idx], t);
            }

        // Clean-up: open the empty space (erode, then dilate, with the
        // 6-neighbourhood; beyond the grid counts as empty). Carved voxels the
        // opening does not reach were speckle or a sliver thinner than about
        // two voxels: they get the hull back.
        auto empty = [&](const std::vector<float>& f, int i, int j, int k) {
            if (i < 0 || j < 0 || k < 0 || i >= g.nx || j >= g.ny || k >= g.nz)
                return true;
            return !(f[index(i, j, k)] > 0);
        };
        std::vector<std::uint8_t> eroded(total, 0);
        parallelFor(g.nz, threads_, [&](int k) {
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i)
                    eroded[index(i, j, k)] = (empty(out.field, i, j, k) && empty(out.field, i - 1, j, k) &&
                                              empty(out.field, i + 1, j, k) && empty(out.field, i, j - 1, k) &&
                                              empty(out.field, i, j + 1, k) && empty(out.field, i, j, k - 1) &&
                                              empty(out.field, i, j, k + 1))
                                                 ? 1
                                                 : 0;
        });
        auto erodedAt = [&](int i, int j, int k) {
            if (i < 0 || j < 0 || k < 0 || i >= g.nx || j >= g.ny || k >= g.nz)
                return true;
            return eroded[index(i, j, k)] != 0;
        };
        long long carved = 0, restored = 0;
        for (int k = 0; k < g.nz; ++k)
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    const std::size_t idx = index(i, j, k);
                    if (!(g.field[idx] > 0) || out.field[idx] > 0)
                        continue;
                    const bool kept = erodedAt(i, j, k) || erodedAt(i - 1, j, k) || erodedAt(i + 1, j, k) ||
                                      erodedAt(i, j - 1, k) || erodedAt(i, j + 1, k) || erodedAt(i, j, k - 1) ||
                                      erodedAt(i, j, k + 1);
                    if (kept)
                        ++carved;
                    else {
                        out.field[idx] = g.field[idx];
                        ++restored;
                    }
                }
        if (carved > 0 && o_.minCarvedComponent > 1) {
            const long long small = restoreSmallCarvings(out);
            carved -= small;
            restored += small;
        }
        stats_.restoredVoxels = restored;
        // Nothing carved: no concavity was seen, and the hull stands exactly
        // (no nudging of its surface by the few depths there were).
        if (carved == 0)
            out.field = g.field;
        if (carved > 0 && o_.thinVoxels > 0)
            carved += removeThinSolid(out);
        stats_.carvedVoxels = carved;
        return true;
    }

    // 6-neighbourhood morphology on a voxel mask; beyond the grid counts as
    // `outsideBelow` under its bottom (the paper side) and as 0 elsewhere.
    std::vector<std::uint8_t> morph(const std::vector<std::uint8_t>& in, bool dilate, std::uint8_t outsideBelow) const
    {
        const ScanGrid& g = grid_;
        std::vector<std::uint8_t> outMask(in.size(), 0);
        auto at = [&](int i, int j, int k) -> std::uint8_t {
            if (k < 0)
                return outsideBelow;
            if (i < 0 || j < 0 || i >= g.nx || j >= g.ny || k >= g.nz)
                return 0;
            return in[index(i, j, k)];
        };
        parallelFor(g.nz, threads_, [&](int k) {
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    const std::uint8_t a[7] = {at(i, j, k),     at(i - 1, j, k), at(i + 1, j, k), at(i, j - 1, k),
                                               at(i, j + 1, k), at(i, j, k - 1), at(i, j, k + 1)};
                    bool r = !dilate;
                    for (std::uint8_t q : a)
                        r = dilate ? (r || q) : (r && q);
                    outMask[index(i, j, k)] = r ? 1 : 0;
                }
        });
        return outMask;
    }

    // Carved regions (6-connected) of fewer than minCarvedComponent voxels get
    // the hull back: a handful of voxels is no evidence of a concavity.
    long long restoreSmallCarvings(ScanGrid& out) const
    {
        const ScanGrid& g = grid_;
        const std::size_t total = g.field.size();
        std::vector<int> label(total, -1);
        std::vector<std::size_t> stack, members;
        long long restoredCount = 0;
        auto carvedAt = [&](std::size_t idx) { return g.field[idx] > 0 && !(out.field[idx] > 0); };
        for (std::size_t seed = 0; seed < total; ++seed) {
            if (label[seed] >= 0 || !carvedAt(seed))
                continue;
            members.clear();
            stack.assign(1, seed);
            label[seed] = 1;
            while (!stack.empty()) {
                const std::size_t idx = stack.back();
                stack.pop_back();
                members.push_back(idx);
                const int i = int(idx % std::size_t(g.nx));
                const int j = int((idx / std::size_t(g.nx)) % std::size_t(g.ny));
                const int k = int(idx / (std::size_t(g.nx) * std::size_t(g.ny)));
                const int nb[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
                for (const auto& dlt : nb) {
                    const int a = i + dlt[0], b = j + dlt[1], cc = k + dlt[2];
                    if (a < 0 || b < 0 || cc < 0 || a >= g.nx || b >= g.ny || cc >= g.nz)
                        continue;
                    const std::size_t q = index(a, b, cc);
                    if (label[q] < 0 && carvedAt(q)) {
                        label[q] = 1;
                        stack.push_back(q);
                    }
                }
            }
            if (int(members.size()) < o_.minCarvedComponent) {
                for (std::size_t idx : members)
                    out.field[idx] = g.field[idx];
                restoredCount += (long long)members.size();
            }
        }
        return restoredCount;
    }

    // Carves solid thinner than about 2 * thinVoxels next to carved voxels
    // (an opening of the solid, applied only near the carving). Returns how
    // many voxels it carved.
    long long removeThinSolid(ScanGrid& out) const
    {
        const ScanGrid& g = grid_;
        const std::size_t total = g.field.size();
        std::vector<std::uint8_t> solid(total), carvedMask(total);
        for (std::size_t idx = 0; idx < total; ++idx) {
            solid[idx] = out.field[idx] > 0 ? 1 : 0;
            carvedMask[idx] = (g.field[idx] > 0 && !(out.field[idx] > 0)) ? 1 : 0;
        }
        std::vector<std::uint8_t> opened = solid;
        for (int n = 0; n < o_.thinVoxels; ++n)
            opened = morph(opened, false, 1);
        for (int n = 0; n < o_.thinVoxels; ++n)
            opened = morph(opened, true, 1);
        std::vector<std::uint8_t> near = carvedMask;
        for (int n = 0; n <= o_.thinVoxels; ++n)
            near = morph(near, true, 0);
        const float carvedValue = -0.5f * std::min(hull_.negativeAmp, slopePerVoxel_);
        long long count = 0;
        for (std::size_t idx = 0; idx < total; ++idx)
            if (solid[idx] && !opened[idx] && near[idx]) {
                out.field[idx] = std::min(out.field[idx], carvedValue);
                ++count;
            }
        stats_.thinVoxels = count;
        return count;
    }

    const std::vector<ScanView>& input_;
    const ScanGrid& grid_;
    const StereoOptions& o_;
    const std::function<bool(double)>& progress_;
    StereoStats& stats_;

    int threads_ = 1;
    int radius_ = 4, step_ = 2, samples_ = 25;
    std::vector<std::pair<int, int>> offsets_;
    HullField hull_;
    Vec3 objectCenter_;
    Vec3 hullMin_, hullMax_; // the inside samples' box
    float slopePerVoxel_ = 0;
    std::vector<StereoView> views_;
    std::atomic<long long> searchedPixels_{0};
    int unitsPerView_ = 1, totalUnits_ = 1, unitsDone_ = 0;
};

} // namespace

ScanGrid refineWithStereo(const std::vector<ScanView>& views, const ScanGrid& hull, const StereoOptions& options,
                          const std::function<bool(double fraction)>& progress, StereoStats* stats)
{
    StereoStats local;
    StereoStats& s = stats ? *stats : local;
    s = StereoStats{};
    Engine engine(views, hull, options, progress, s);
    ScanGrid out;
    if (!engine.run(out))
        return hull;
    return out;
}

} // namespace os::measure
