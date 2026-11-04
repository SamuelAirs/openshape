// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Scan's multi-view stereo (src/measure/ScanStereo) on synthetic photos: a
// small ray caster renders textured objects on a faintly textured sheet of
// paper from known cameras, a simple carver makes the silhouettes' visual
// hull, and refineWithStereo must carve what the silhouettes cannot show
// (a tray's pocket) without harming what they can (a box), and leave a
// texture-less object exactly as the hull.

#include "measure/ScanStereo.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace os;
using namespace os::measure;

namespace {

constexpr double kDeg = kPi / 180.0;

// ------------------------------------------------------------------ texture

std::uint32_t hash3(int x, int y, int z, std::uint32_t seed)
{
    std::uint32_t h = seed ^ 0x9E3779B9u;
    h ^= std::uint32_t(x) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= std::uint32_t(y) * 0xC2B2AE35u;
    h = (h << 13) | (h >> 19);
    h ^= std::uint32_t(z) * 0x27D4EB2Fu;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

double lattice(int x, int y, int z, std::uint32_t seed)
{
    return double(hash3(x, y, z, seed) >> 8) / double(1u << 24);
}

// Smooth value noise in [0, 1], features about `period` mm across.
double valueNoise(Vec3 p, double period, std::uint32_t seed)
{
    const double x = p.x / period, y = p.y / period, z = p.z / period;
    const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    auto smooth = [](double t) { return t * t * (3 - 2 * t); };
    const double fx = smooth(x - ix), fy = smooth(y - iy), fz = smooth(z - iz);
    double c[2][2][2];
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            for (int d = 0; d < 2; ++d)
                c[a][b][d] = lattice(ix + a, iy + b, iz + d, seed);
    auto lerp = [](double u, double v, double t) { return u + (v - u) * t; };
    const double x00 = lerp(c[0][0][0], c[1][0][0], fx), x10 = lerp(c[0][1][0], c[1][1][0], fx);
    const double x01 = lerp(c[0][0][1], c[1][0][1], fx), x11 = lerp(c[0][1][1], c[1][1][1], fx);
    return lerp(lerp(x00, x10, fy), lerp(x01, x11, fy), fz);
}

// ------------------------------------------------------------------ scenes

struct Hit {
    double t = -1;
    Vec3 normal;
    bool object = false;
};

// The ray's interval through an axis-aligned box, with the outward normals
// of the faces it enters and leaves by.
bool boxInterval(Vec3 o, Vec3 d, Vec3 lo, Vec3 hi, double& t0, double& t1, Vec3& n0, Vec3& n1)
{
    t0 = -1e300;
    t1 = 1e300;
    const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    const double l[3] = {lo.x, lo.y, lo.z}, h[3] = {hi.x, hi.y, hi.z};
    for (int a = 0; a < 3; ++a) {
        if (std::abs(dd[a]) < 1e-15) {
            if (oo[a] < l[a] || oo[a] > h[a])
                return false;
            continue;
        }
        double ta = (l[a] - oo[a]) / dd[a], tb = (h[a] - oo[a]) / dd[a];
        double sa = -1, sb = 1; // the outward normal's sign at ta, tb
        if (ta > tb) {
            std::swap(ta, tb);
            std::swap(sa, sb);
        }
        Vec3 axis;
        (a == 0 ? axis.x : a == 1 ? axis.y : axis.z) = 1;
        if (ta > t0) {
            t0 = ta;
            n0 = axis * sa;
        }
        if (tb < t1) {
            t1 = tb;
            n1 = axis * sb;
        }
    }
    return t1 > t0 && t1 > 0;
}

struct Scene {
    enum class Kind { Tray, Box, Cylinder };
    Kind kind = Kind::Box;
    bool textured = true;
    // Tray: the outer box minus the pocket (open at the top).
    Vec3 lo{-40, -30, 0}, hi{40, 30, 30};
    Vec3 pocketLo{-25, -15, 12}, pocketHi{25, 15, 60};
    double radius = 25, height = 40; // Cylinder (axis = z through the origin)

    Hit objectHit(Vec3 o, Vec3 d) const
    {
        Hit hit;
        if (kind == Kind::Box || kind == Kind::Tray) {
            double a0, a1;
            Vec3 na0, na1;
            if (!boxInterval(o, d, lo, hi, a0, a1, na0, na1))
                return hit;
            if (kind == Kind::Box) {
                if (a0 > 0)
                    hit = {a0, na0, true};
                return hit;
            }
            double b0, b1;
            Vec3 nb0, nb1;
            const bool inPocket = boxInterval(o, d, pocketLo, pocketHi, b0, b1, nb0, nb1) && a0 >= b0 && a0 < b1;
            if (!inPocket) {
                if (a0 > 0)
                    hit = {a0, na0, true};
            } else if (b1 < a1) {
                hit = {b1, -nb1, true};
            }
            return hit;
        }
        // Cylinder: the infinite side's interval within the slab 0 <= z <= height.
        const double A = d.x * d.x + d.y * d.y, B = 2 * (o.x * d.x + o.y * d.y), C = o.x * o.x + o.y * o.y - radius * radius;
        double s0 = -1e300, s1 = 1e300;
        if (A > 1e-15) {
            const double disc = B * B - 4 * A * C;
            if (disc < 0)
                return hit;
            s0 = (-B - std::sqrt(disc)) / (2 * A);
            s1 = (-B + std::sqrt(disc)) / (2 * A);
        } else if (C > 0) {
            return hit;
        }
        double z0 = -1e300, z1 = 1e300;
        Vec3 nz0;
        if (std::abs(d.z) > 1e-15) {
            double ta = (0 - o.z) / d.z, tb = (height - o.z) / d.z;
            nz0 = Vec3{0, 0, -1};
            if (ta > tb) {
                std::swap(ta, tb);
                nz0 = Vec3{0, 0, 1};
            }
            z0 = ta;
            z1 = tb;
        }
        const double t0 = std::max(s0, z0), t1 = std::min(s1, z1);
        if (!(t1 > t0) || t0 <= 0)
            return hit;
        Vec3 n = nz0;
        if (s0 >= z0) {
            const Vec3 p = o + d * s0;
            n = Vec3{p.x, p.y, 0}.normalized();
        }
        return {t0, n, true};
    }

    // Whether p lies inside the object, at least `margin` mm from its surface.
    bool inside(Vec3 p, double margin = 0) const
    {
        auto inBox = [&](Vec3 a, Vec3 b, double m) {
            return p.x > a.x + m && p.x < b.x - m && p.y > a.y + m && p.y < b.y - m && p.z > a.z + m && p.z < b.z - m;
        };
        if (kind == Kind::Box)
            return inBox(lo, hi, margin);
        if (kind == Kind::Tray)
            return inBox(lo, hi, margin) && !inBox(pocketLo, pocketHi, -margin);
        return std::hypot(p.x, p.y) < radius - margin && p.z > margin && p.z < height - margin;
    }

    // The object, or the paper at z = 0.
    Hit trace(Vec3 o, Vec3 d) const
    {
        Hit hit = objectHit(o, d);
        if (d.z < 0) {
            const double tp = -o.z / d.z;
            if (tp > 0 && (hit.t < 0 || tp < hit.t))
                hit = {tp, Vec3{0, 0, 1}, false};
        }
        return hit;
    }

    double albedo(Vec3 p, bool object) const
    {
        if (!object) // faint paper texture
            return 0.82 + 0.05 * valueNoise(p, 2.0, 7);
        if (!textured)
            return 0.7;
        const double n = 0.6 * valueNoise(p, 3.0, 1) + 0.4 * valueNoise(p, 1.3, 2);
        return 0.15 + 0.75 * n;
    }
};

// ------------------------------------------------------------------ cameras and photos

ScanCamera lookAtCamera(Vec3 eye, Vec3 target, int width, int height, double focal)
{
    const Vec3 f = (target - eye).normalized();
    const Vec3 r = f.cross(Vec3{0, 0, 1}).normalized();
    const Vec3 dn = f.cross(r); // image y grows downward
    ScanCamera c;
    c.fx = c.fy = focal;
    c.cx = width * 0.5;
    c.cy = height * 0.5;
    c.width = width;
    c.height = height;
    c.R = {r.x, r.y, r.z, dn.x, dn.y, dn.z, f.x, f.y, f.z};
    c.t = Vec3{-(r.dot(eye)), -(dn.dot(eye)), -(f.dot(eye))};
    return c;
}

template <class F>
void parallelRows(int rows, const F& fn)
{
    const int n = std::max(1, int(std::thread::hardware_concurrency()) / 2);
    std::vector<std::thread> pool;
    for (int t = 0; t < n; ++t)
        pool.emplace_back([&, t] {
            for (int y = t; y < rows; y += n)
                fn(y);
        });
    for (auto& th : pool)
        th.join();
}

ScanView render(const Scene& scene, const ScanCamera& cam)
{
    ScanView v;
    v.camera = cam;
    const int w = cam.width, h = cam.height;
    v.rgb.assign(std::size_t(w) * std::size_t(h) * 3, 0);
    v.mask.assign(std::size_t(w) * std::size_t(h), 0);
    const Vec3 eye = cam.center();
    const Vec3 light = Vec3{0.4, -0.3, 0.87}.normalized();
    parallelRows(h, [&](int y) {
        for (int x = 0; x < w; ++x) {
            const Vec3 rc{(x + 0.5 - cam.cx) / cam.fx, (y + 0.5 - cam.cy) / cam.fy, 1};
            const auto& R = cam.R;
            const Vec3 d = Vec3{R[0] * rc.x + R[3] * rc.y + R[6], R[1] * rc.x + R[4] * rc.y + R[7],
                                R[2] * rc.x + R[5] * rc.y + R[8]}
                               .normalized();
            const Hit hit = scene.trace(eye, d);
            double value = 0.1;
            if (hit.t > 0) {
                const Vec3 p = eye + d * hit.t;
                const double shade = 0.3 + 0.7 * std::max(0.0, hit.normal.dot(light));
                value = scene.albedo(p, hit.object) * shade;
            }
            // A little sensor noise (+-1.5 grey levels).
            const double noise = (double(hash3(x, y, int(cam.t.x * 1000), 99) >> 8) / double(1u << 24) - 0.5) * 3.0;
            const auto level = std::uint8_t(std::clamp(value * 255.0 + noise, 0.0, 255.0));
            const std::size_t p = std::size_t(y) * std::size_t(w) + std::size_t(x);
            v.rgb[3 * p] = v.rgb[3 * p + 1] = v.rgb[3 * p + 2] = level;
            v.mask[p] = hit.object ? 255 : 0;
        }
    });
    return v;
}

// 12 photos around the object, alternately 38 and 58 degrees up.
std::vector<ScanView> photograph(const Scene& scene, int count = 12, int width = 1200, int height = 900)
{
    std::vector<ScanView> views;
    const Vec3 target{0, 0, 12};
    const double distance = 230;
    for (int i = 0; i < count; ++i) {
        const double az = (15.0 + 360.0 * i / count) * kDeg;
        const double el = (i % 2 == 0 ? 38.0 : 58.0) * kDeg;
        const Vec3 eye = target + Vec3{std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)} * distance;
        views.push_back(render(scene, lookAtCamera(eye, target, width, height, 1500.0 * width / 1200.0)));
    }
    return views;
}

// ------------------------------------------------------------------ the hull

// Voxels whose center every photo's mask calls object; the occupancy blurred
// a little and shifted by 0.5, like the silhouette stage's field. Sample
// (i, j, k) sits at origin + (i + 0.5, j + 0.5, k + 0.5) * voxel: half-way
// between the objects' faces (which lie on whole millimeters), so counting
// samples measures volumes fairly; column (50, 40) is at x = y = 0.5.
ScanGrid carveHull(const std::vector<ScanView>& views)
{
    ScanGrid g;
    g.voxel = 1.0;
    g.origin = Vec3{-50, -40, 0};
    g.nx = 101;
    g.ny = 81;
    g.nz = 80; // the hull's roof over a wide top rises well above it
    const std::size_t n = std::size_t(g.nx) * std::size_t(g.ny) * std::size_t(g.nz);
    std::vector<float> occ(n, 0.0f);
    auto at = [&](int i, int j, int k) { return (std::size_t(k) * std::size_t(g.ny) + std::size_t(j)) * std::size_t(g.nx) + std::size_t(i); };
    parallelRows(g.nz, [&](int k) {
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i) {
                const Vec3 p = g.origin + Vec3{i + 0.5, j + 0.5, k + 0.5} * g.voxel;
                bool inside = true;
                for (const ScanView& v : views) {
                    const Vec2 px = v.camera.project(p);
                    const int x = int(std::floor(px.x)), y = int(std::floor(px.y));
                    if (v.camera.depth(p) <= 0 || x < 0 || y < 0 || x >= v.camera.width || y >= v.camera.height ||
                        v.mask[std::size_t(y) * std::size_t(v.camera.width) + std::size_t(x)] != 255) {
                        inside = false;
                        break;
                    }
                }
                occ[at(i, j, k)] = inside ? 1.0f : 0.0f;
            }
    });
    // Separable Gaussian (sigma 0.8 voxel), clamped at the grid's faces (so
    // the bottom on the paper stays put).
    const float kernel[5] = {0.0228f, 0.2275f, 0.4994f, 0.2275f, 0.0228f};
    auto blurAxis = [&](std::vector<float>& f, int axis) {
        std::vector<float> out(n);
        for (int k = 0; k < g.nz; ++k)
            for (int j = 0; j < g.ny; ++j)
                for (int i = 0; i < g.nx; ++i) {
                    float s = 0;
                    for (int q = -2; q <= 2; ++q) {
                        int a = i, b = j, c = k;
                        if (axis == 0)
                            a = std::clamp(i + q, 0, g.nx - 1);
                        else if (axis == 1)
                            b = std::clamp(j + q, 0, g.ny - 1);
                        else
                            c = std::clamp(k + q, 0, g.nz - 1);
                        s += kernel[q + 2] * f[at(a, b, c)];
                    }
                    out[at(i, j, k)] = s;
                }
        f = std::move(out);
    };
    blurAxis(occ, 0);
    blurAxis(occ, 1);
    blurAxis(occ, 2);
    for (float& f : occ)
        f -= 0.5f;
    g.field = std::move(occ);
    return g;
}

double volume(const ScanGrid& g)
{
    std::size_t inside = 0;
    for (float f : g.field)
        inside += f > 0 ? 1 : 0;
    return double(inside) * g.voxel * g.voxel * g.voxel;
}

// Where the column (i, j) first turns inside, coming down from the top.
std::optional<double> topSurfaceZ(const ScanGrid& g, int i, int j)
{
    auto f = [&](int k) {
        return g.field[(std::size_t(k) * std::size_t(g.ny) + std::size_t(j)) * std::size_t(g.nx) + std::size_t(i)];
    };
    for (int k = g.nz - 2; k >= 0; --k)
        if (f(k) > 0 && f(k + 1) <= 0) {
            const double a = f(k), b = f(k + 1);
            return g.origin.z + (k + 0.5 + a / (a - b)) * g.voxel;
        }
    return std::nullopt;
}

// Where the row (j, k) last leaves the inside, coming from +x.
std::optional<double> plusXSurface(const ScanGrid& g, int j, int k)
{
    auto f = [&](int i) {
        return g.field[(std::size_t(k) * std::size_t(g.ny) + std::size_t(j)) * std::size_t(g.nx) + std::size_t(i)];
    };
    for (int i = g.nx - 2; i >= 0; --i)
        if (f(i) > 0 && f(i + 1) <= 0) {
            const double a = f(i), b = f(i + 1);
            return g.origin.x + (i + 0.5 + a / (a - b)) * g.voxel;
        }
    return std::nullopt;
}

struct MvsRun {
    ScanGrid hull, refined;
    StereoStats stats;
    double seconds = 0;
};

MvsRun refine(const Scene& scene)
{
    MvsRun run;
    const std::vector<ScanView> views = photograph(scene);
    run.hull = carveHull(views);
    const auto t0 = std::chrono::steady_clock::now();
    int calls = 0;
    StereoOptions options;
    // OPENSHAPE_MVS_THREADS=4 times the stage as on a phone's cores.
    if (const char* threads = std::getenv("OPENSHAPE_MVS_THREADS"))
        options.threads = std::atoi(threads);
    run.refined = refineWithStereo(views, run.hull, options, [&](double) { return ++calls > 0; }, &run.stats);
    run.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_GT(calls, 10); // progress is reported regularly
    const StereoStats& s = run.stats;
    std::printf("  stereo: %d views, %lld searched px, %lld confident, %lld consistent; carved %lld voxels "
                "(%lld restored); depth %.2f s, filter %.2f s, fusion %.2f s, total %.2f s\n",
                s.viewsUsed, s.depthPixels, s.confidentPixels, s.consistentPixels, s.carvedVoxels, s.restoredVoxels,
                s.secondsDepth, s.secondsFilter, s.secondsFusion, run.seconds);
    return run;
}

void expectNeverAboveHull(const MvsRun& run)
{
    ASSERT_EQ(run.refined.field.size(), run.hull.field.size());
    std::size_t above = 0;
    for (std::size_t i = 0; i < run.hull.field.size(); ++i)
        above += run.refined.field[i] > run.hull.field[i] ? 1 : 0;
    EXPECT_EQ(above, 0u);
}

// How a field differs from the true object: volume outside it (extra) and
// volume of its inside more than 1.5 mm deep that the field lost (damage).
struct Comparison {
    double extra = 0, damage = 0;
    double extraAbove = 0; // of the extra, above the object's top
    double shallowLoss = 0; // inside volume lost within 1.5 mm of the surface
};

Comparison compare(const ScanGrid& g, const Scene& scene)
{
    Comparison c;
    const double v3 = g.voxel * g.voxel * g.voxel;
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i) {
                const Vec3 p = g.origin + Vec3{i + 0.5, j + 0.5, k + 0.5} * g.voxel;
                const bool in = g.field[(std::size_t(k) * std::size_t(g.ny) + std::size_t(j)) * std::size_t(g.nx) +
                                        std::size_t(i)] > 0;
                const double top = scene.kind == Scene::Kind::Cylinder ? scene.height : scene.hi.z;
                if (in && !scene.inside(p)) {
                    c.extra += v3;
                    if (p.z > top)
                        c.extraAbove += v3;
                }
                if (!in && scene.inside(p, 1.5))
                    c.damage += v3;
                else if (!in && scene.inside(p))
                    c.shallowLoss += v3;
            }
    return c;
}

} // namespace

// The camera model the stages share.
TEST(ScanTypes, CameraProjectsAndKnowsItsCenter)
{
    const Vec3 eye{100, -50, 80};
    const ScanCamera c = lookAtCamera(eye, Vec3{0, 0, 10}, 1200, 900, 1500);
    const Vec3 ctr = c.center();
    EXPECT_NEAR((ctr - eye).length(), 0.0, 1e-9);
    const Vec2 p = c.project(Vec3{0, 0, 10});
    EXPECT_NEAR(p.x, 600.0, 1e-9);
    EXPECT_NEAR(p.y, 450.0, 1e-9);
    EXPECT_NEAR(c.depth(Vec3{0, 0, 10}), (Vec3{0, 0, 10} - eye).length(), 1e-9);
    // Up in the world is up in the picture (smaller y).
    EXPECT_LT(c.project(Vec3{0, 0, 30}).y, 450.0);
}

// (a) A tray: the hull fills its pocket; the photos' texture shows it.
TEST(ScanStereo, CarvesATraysPocket)
{
    Scene tray;
    tray.kind = Scene::Kind::Tray;
    const MvsRun run = refine(tray);
    expectNeverAboveHull(run);

    const double truth = 80.0 * 60 * 30 - 50.0 * 30 * 18; // 117000 mm^3
    const double hullError = std::abs(volume(run.hull) - truth) / truth;
    const double error = std::abs(volume(run.refined) - truth) / truth;
    const auto hullFloor = topSurfaceZ(run.hull, 50, 40);
    const auto floor = topSurfaceZ(run.refined, 50, 40);
    const Comparison ch = compare(run.hull, tray), cr = compare(run.refined, tray);
    std::printf("  tray: extra %.0f mm^3 (hull %.0f), damage %.0f mm^3 (hull %.0f)\n", cr.extra, ch.extra, cr.damage,
                ch.damage);
    EXPECT_LT(cr.damage, 0.01 * truth);
    ASSERT_TRUE(hullFloor && floor);
    std::printf("  tray: volume %.0f (hull %.0f, truth %.0f): error %.1f %% (hull %.1f %%); pocket floor at the "
                "center z = %.2f (hull %.2f, truth 12)\n",
                volume(run.refined), volume(run.hull), truth, 100 * error, 100 * hullError, *floor, *hullFloor);
    EXPECT_GT(hullError, 0.15);
    EXPECT_LT(error, 0.10);
    EXPECT_LT(error, hullError / 2);
    EXPECT_GT(*hullFloor, 30.0 - 1.0); // the pocket is filled (and roofed over)
    EXPECT_NEAR(*floor, 12.0, 2.0);
    // Off center too: a quarter of the way to the pocket's walls.
    for (const auto& ij : {std::pair{38, 40}, std::pair{62, 40}, std::pair{50, 33}, std::pair{50, 47}}) {
        const auto z = topSurfaceZ(run.refined, ij.first, ij.second);
        ASSERT_TRUE(z);
        EXPECT_NEAR(*z, 12.0, 2.0) << "at column " << ij.first << ", " << ij.second;
    }
    // The rim stays where it was.
    const auto rim = topSurfaceZ(run.refined, 50 + 32, 40); // x = 32: between the pocket and the outer wall
    ASSERT_TRUE(rim);
    EXPECT_NEAR(*rim, 30.0, 1.0);
}

// (b) A textured box: nothing to carve, nothing must be damaged.
TEST(ScanStereo, LeavesABoxIntact)
{
    Scene box;
    box.kind = Scene::Kind::Box;
    box.lo = Vec3{-25, -20, 0};
    box.hi = Vec3{25, 20, 35};
    const MvsRun run = refine(box);
    expectNeverAboveHull(run);
    const double truth = 50.0 * 40 * 35;
    const double v = volume(run.refined), vh = volume(run.hull);
    const Comparison ch = compare(run.hull, box), cr = compare(run.refined, box);
    std::printf("  box: volume %.0f (hull %.0f, truth %.0f); extra %.0f mm^3 (%.0f above the top; hull %.0f), "
                "damage %.0f mm^3, lost within 1.5 mm of the surface %.0f mm^3\n",
                v, vh, truth, cr.extra, cr.extraAbove, ch.extra, cr.damage, cr.shallowLoss);
    // Cameras that all look down leave the hull a roof over the top face:
    // stereo may take that away, but nothing of the box itself.
    // Where the faces ended up: the top over a few columns, the +x side over a few rows.
    for (const auto& ij : {std::pair{50, 40}, std::pair{30, 25}, std::pair{70, 55}, std::pair{27, 22}}) {
        const auto zt = topSurfaceZ(run.refined, ij.first, ij.second), zh = topSurfaceZ(run.hull, ij.first, ij.second);
        std::printf("    top at (%d, %d): %.2f (hull %.2f, truth 35)\n", ij.first - 50, ij.second - 40, zt ? *zt : -1.0,
                    zh ? *zh : -1.0);
    }
    for (const auto& jk : {std::pair{40, 17}, std::pair{40, 3}, std::pair{40, 32}, std::pair{25, 17}}) {
        const auto xs = plusXSurface(run.refined, jk.first, jk.second), xh = plusXSurface(run.hull, jk.first, jk.second);
        std::printf("    +x side at y %d, z %.1f: %.2f (hull %.2f, truth 25)\n", jk.first - 40, jk.second + 0.5,
                    xs ? *xs : -1.0, xh ? *xh : -1.0);
    }
    EXPECT_LE(v, vh);
    EXPECT_NEAR(v / truth, 1.0, 0.03);
    EXPECT_LT(cr.damage, 0.005 * truth);
    const auto top = topSurfaceZ(run.refined, 50, 40);
    ASSERT_TRUE(top);
    EXPECT_NEAR(*top, 35.0, 1.0);
}

// (c) No texture: no depth to trust, so the hull stands, without speckle.
TEST(ScanStereo, TexturelessObjectKeepsTheHull)
{
    Scene cylinder;
    cylinder.kind = Scene::Kind::Cylinder;
    cylinder.textured = false;
    const MvsRun run = refine(cylinder);
    expectNeverAboveHull(run);
    std::size_t flipped = 0;
    for (std::size_t i = 0; i < run.hull.field.size(); ++i)
        flipped += (run.refined.field[i] > 0) != (run.hull.field[i] > 0) ? 1 : 0;
    std::printf("  texture-less cylinder: %zu voxels changed side (hull %.0f mm^3)\n", flipped, volume(run.hull));
    EXPECT_EQ(flipped, 0u);
    EXPECT_EQ(run.stats.carvedVoxels, 0);
}

// (d) Cancelling returns the hull unchanged.
TEST(ScanStereo, CancelReturnsTheHull)
{
    Scene tray;
    tray.kind = Scene::Kind::Tray;
    const std::vector<ScanView> views = photograph(tray, 6, 600, 450);
    const ScanGrid hull = carveHull(views);
    for (const double stopAt : {0.0, 0.5}) {
        StereoStats stats;
        // 0: cancel at the first call; 0.5: halfway through the depth maps.
        const ScanGrid out =
            refineWithStereo(views, hull, StereoOptions{}, [&](double f) { return f < stopAt; }, &stats);
        EXPECT_TRUE(stats.cancelled);
        EXPECT_EQ(out.field, hull.field);
        EXPECT_EQ(out.nx, hull.nx);
    }
}

// Too few photos (or none with pixels): the hull, unchanged.
TEST(ScanStereo, NeedsTwoViews)
{
    Scene box;
    const std::vector<ScanView> views = photograph(box, 1, 400, 300);
    const ScanGrid hull = carveHull(views);
    const ScanGrid out = refineWithStereo(views, hull, StereoOptions{}, nullptr);
    EXPECT_EQ(out.field, hull.field);
    const ScanGrid none = refineWithStereo({}, hull, StereoOptions{}, nullptr);
    EXPECT_EQ(none.field, hull.field);
}
