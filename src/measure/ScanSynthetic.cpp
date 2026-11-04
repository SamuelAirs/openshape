// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScanSynthetic.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <random>
#include <thread>

namespace os::measure::synthetic {

using measure::ScanCamera;

namespace {

constexpr double kRad = kPi / 180.0;

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

double lattice(int x, int y, int z, std::uint32_t seed) { return (hash3(x, y, z, seed) & 0xFFFFFF) / double(0xFFFFFF); }

// Smooth value noise in [0, 1].
double valueNoise(const Vec3& p, std::uint32_t seed)
{
    const int x0 = int(std::floor(p.x)), y0 = int(std::floor(p.y)), z0 = int(std::floor(p.z));
    const double fx = p.x - x0, fy = p.y - y0, fz = p.z - z0;
    const auto s = [](double t) { return t * t * (3 - 2 * t); };
    const double sx = s(fx), sy = s(fy), sz = s(fz);
    double v = 0;
    for (int c = 0; c < 8; ++c) {
        const int dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
        const double w = (dx ? sx : 1 - sx) * (dy ? sy : 1 - sy) * (dz ? sz : 1 - sz);
        v += w * lattice(x0 + dx, y0 + dy, z0 + dz, seed);
    }
    return v;
}

double fractalNoise(const Vec3& p, std::uint32_t seed)
{
    return 0.6 * valueNoise(p, seed) + 0.4 * valueNoise(p * 2.7 + Vec3{11.3, 5.1, 7.7}, seed + 1);
}

struct Ray {
    Vec3 o, d;
};

bool hitPart(const Part& part, const Ray& ray, double& t, Vec3& normal)
{
    if (part.kind == Part::Kind::Box) {
        const double c = std::cos(-part.turnDeg * kRad), s = std::sin(-part.turnDeg * kRad);
        const Vec3 o0 = ray.o - part.center;
        const Vec3 o{c * o0.x - s * o0.y, s * o0.x + c * o0.y, o0.z};
        const Vec3 d{c * ray.d.x - s * ray.d.y, s * ray.d.x + c * ray.d.y, ray.d.z};
        const double half[3] = {part.size.x / 2, part.size.y / 2, part.size.z / 2};
        const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
        double tmin = -1e300, tmax = 1e300;
        int axis = -1;
        double sign = 0;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dd[a]) < 1e-15) {
                if (std::abs(oo[a]) > half[a])
                    return false;
                continue;
            }
            double t1 = (-half[a] - oo[a]) / dd[a], t2 = (half[a] - oo[a]) / dd[a];
            double sg = -1;
            if (t1 > t2) {
                std::swap(t1, t2);
                sg = 1;
            }
            if (t1 > tmin) {
                tmin = t1;
                axis = a;
                sign = sg;
            }
            tmax = std::min(tmax, t2);
        }
        if (tmin > tmax || tmin < 1e-6 || axis < 0)
            return false;
        t = tmin;
        Vec3 n{axis == 0 ? sign : 0.0, axis == 1 ? sign : 0.0, axis == 2 ? sign : 0.0};
        const double cb = std::cos(part.turnDeg * kRad), sb = std::sin(part.turnDeg * kRad);
        normal = {cb * n.x - sb * n.y, sb * n.x + cb * n.y, n.z};
        return true;
    }
    // Upright cylinder: the side, then the top cap.
    bool found = false;
    double best = 1e300;
    const double ox = ray.o.x - part.center.x, oy = ray.o.y - part.center.y;
    const double a = ray.d.x * ray.d.x + ray.d.y * ray.d.y;
    if (a > 1e-15) {
        const double b = 2 * (ox * ray.d.x + oy * ray.d.y);
        const double c = ox * ox + oy * oy - part.radius * part.radius;
        const double disc = b * b - 4 * a * c;
        if (disc >= 0) {
            const double tt = (-b - std::sqrt(disc)) / (2 * a);
            const double z = ray.o.z + tt * ray.d.z;
            if (tt > 1e-6 && z >= part.center.z && z <= part.center.z + part.height) {
                best = tt;
                found = true;
                normal = Vec3{ox + tt * ray.d.x, oy + tt * ray.d.y, 0}.normalized();
            }
        }
    }
    if (std::abs(ray.d.z) > 1e-15) {
        const double tt = (part.center.z + part.height - ray.o.z) / ray.d.z;
        const double x = ox + tt * ray.d.x, y = oy + tt * ray.d.y;
        if (tt > 1e-6 && tt < best && x * x + y * y <= part.radius * part.radius) {
            best = tt;
            found = true;
            normal = {0, 0, ray.d.z < 0 ? 1.0 : -1.0};
        }
    }
    if (found)
        t = best;
    return found;
}

bool hitObject(const Object& object, const Ray& ray, double& t, Vec3& normal)
{
    // The bounding box first.
    double tmin = -1e300, tmax = 1e300;
    const double o[3] = {ray.o.x, ray.o.y, ray.o.z}, d[3] = {ray.d.x, ray.d.y, ray.d.z};
    const double lo[3] = {object.min.x - 0.01, object.min.y - 0.01, object.min.z - 0.01};
    const double hi[3] = {object.max.x + 0.01, object.max.y + 0.01, object.max.z + 0.01};
    for (int a = 0; a < 3; ++a) {
        if (std::abs(d[a]) < 1e-15) {
            if (o[a] < lo[a] || o[a] > hi[a])
                return false;
            continue;
        }
        double t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
        if (t1 > t2)
            std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
    }
    if (tmin > tmax || tmax < 1e-6)
        return false;
    bool found = false;
    t = 1e300;
    for (const Part& part : object.parts) {
        double tt;
        Vec3 n;
        if (hitPart(part, ray, tt, n) && tt < t) {
            t = tt;
            normal = n;
            found = true;
        }
    }
    return found;
}

void finishObject(Object& o)
{
    bool first = true;
    o.volume = 0;
    for (const Part& p : o.parts) {
        Vec3 lo, hi;
        if (p.kind == Part::Kind::Box) {
            o.volume += p.size.x * p.size.y * p.size.z;
            const double c = std::cos(p.turnDeg * kRad), s = std::sin(p.turnDeg * kRad);
            const double ex = std::abs(c) * p.size.x / 2 + std::abs(s) * p.size.y / 2;
            const double ey = std::abs(s) * p.size.x / 2 + std::abs(c) * p.size.y / 2;
            lo = p.center - Vec3{ex, ey, p.size.z / 2};
            hi = p.center + Vec3{ex, ey, p.size.z / 2};
        } else {
            o.volume += kPi * p.radius * p.radius * p.height;
            lo = p.center - Vec3{p.radius, p.radius, 0};
            hi = p.center + Vec3{p.radius, p.radius, p.height};
        }
        if (first) {
            o.min = lo;
            o.max = hi;
            first = false;
        } else {
            o.min = {std::min(o.min.x, lo.x), std::min(o.min.y, lo.y), std::min(o.min.z, lo.z)};
            o.max = {std::max(o.max.x, hi.x), std::max(o.max.y, hi.y), std::max(o.max.z, hi.z)};
        }
    }
}

Part box(Vec3 center, Vec3 size, double turn = 0)
{
    Part p;
    p.kind = Part::Kind::Box;
    p.center = center;
    p.size = size;
    p.turnDeg = turn;
    return p;
}

} // namespace

Object boxObject(Vec3 size, Vec2 at, double turnDeg)
{
    Object o;
    o.name = "box";
    o.parts.push_back(box({at.x, at.y, size.z / 2}, size, turnDeg));
    finishObject(o);
    return o;
}

Object cylinderObject(double diameter, double height, Vec2 at)
{
    Object o;
    o.name = "cylinder";
    Part p;
    p.kind = Part::Kind::Cylinder;
    p.center = {at.x, at.y, 0};
    p.radius = diameter / 2;
    p.height = height;
    o.parts.push_back(p);
    finishObject(o);
    return o;
}

Object lBlockObject(Vec2 at)
{
    Object o;
    o.name = "L-block";
    o.parts.push_back(box({at.x, at.y, 7.5}, {50, 30, 15}));
    o.parts.push_back(box({at.x + 25 - 7.5, at.y, 15 + 12.5}, {15, 30, 25}));
    finishObject(o);
    return o;
}

Object uChannelObject(Vec2 at)
{
    Object o;
    o.name = "U-channel";
    o.parts.push_back(box({at.x, at.y, 4}, {50, 30, 8}));
    o.parts.push_back(box({at.x - 21, at.y, 8 + 11}, {8, 30, 22}));
    o.parts.push_back(box({at.x + 21, at.y, 8 + 11}, {8, 30, 22}));
    finishObject(o);
    return o;
}

std::vector<CameraSpec> ring(int count, double elevLow, double elevHigh, double startDeg, double spanDeg, double distanceMm,
                             double focal35mm)
{
    std::vector<CameraSpec> out;
    for (int i = 0; i < count; ++i) {
        CameraSpec c;
        c.azimuthDeg = startDeg + spanDeg * i / (spanDeg >= 360 ? count : std::max(1, count - 1));
        c.elevationDeg = i % 2 == 0 ? elevLow : elevHigh;
        c.distanceMm = distanceMm;
        c.focal35mm = focal35mm;
        out.push_back(c);
    }
    return out;
}

ScanCamera cameraFor(const CameraSpec& spec)
{
    const double az = spec.azimuthDeg * kRad, el = spec.elevationDeg * kRad;
    const Vec3 eye = spec.target + Vec3{std::cos(el) * std::cos(az), std::cos(el) * std::sin(az), std::sin(el)} * spec.distanceMm;
    const Vec3 f = (spec.target - eye).normalized();
    const Vec3 r = f.cross(Vec3{0, 0, 1}).normalized();
    const Vec3 d = f.cross(r);
    ScanCamera c;
    c.R = {r.x, r.y, r.z, d.x, d.y, d.z, f.x, f.y, f.z};
    c.t = Vec3{-(r.dot(eye)), -(d.dot(eye)), -(f.dot(eye))};
    c.width = spec.width;
    c.height = spec.height;
    c.fx = c.fy = measure::focalPxFrom35mm(spec.focal35mm, spec.width, spec.height);
    c.cx = spec.width * 0.5;
    c.cy = spec.height * 0.5;
    return c;
}

namespace {

Photo renderOne(const Scene& scene, const CameraSpec& spec, unsigned seed, double cornerNoisePx)
{
    Photo out;
    out.width = spec.width;
    out.height = spec.height;
    out.focal35mm = spec.focal35mm;
    out.camera = cameraFor(spec);
    const ScanCamera& cam = out.camera;
    out.rgba.assign(std::size_t(out.width) * out.height * 4, 255);
    out.mask.assign(std::size_t(out.width) * out.height, 0);
    const Vec3 center = cam.center();
    const Vec3 light = scene.lightDirection.normalized();
    const double W = std::max(scene.paper.widthMm, scene.paper.heightMm), H = std::min(scene.paper.widthMm, scene.paper.heightMm);

    // Soft shadows: a few directions around the light's.
    const Vec3 u = light.cross(Vec3{0, 0, 1}).normalized();
    const Vec3 v = light.cross(u);
    std::vector<Vec3> lights{light};
    for (int k = 0; k < 8; ++k) {
        const double a = k * kPi / 4;
        lights.push_back((light + (u * std::cos(a) + v * std::sin(a)) * scene.softness).normalized());
    }
    // Where shadows can fall on the table (the object's box cast along the light).
    double sx0 = 1e300, sy0 = 1e300, sx1 = -1e300, sy1 = -1e300;
    for (int c = 0; c < 8; ++c) {
        const Vec3 q{c & 1 ? scene.object.max.x : scene.object.min.x, c & 2 ? scene.object.max.y : scene.object.min.y,
                     c & 4 ? scene.object.max.z : scene.object.min.z};
        for (const Vec3& l : lights) {
            const Vec3 g = q - l * (q.z / l.z);
            sx0 = std::min(sx0, g.x), sx1 = std::max(sx1, g.x);
            sy0 = std::min(sy0, g.y), sy1 = std::max(sy1, g.y);
        }
    }
    auto litFraction = [&](const Vec3& p) {
        if (!scene.shadows || scene.object.parts.empty())
            return 1.0;
        if (p.z < 0.01 && (p.x < sx0 - 1 || p.x > sx1 + 1 || p.y < sy0 - 1 || p.y > sy1 + 1))
            return 1.0;
        int lit = 0;
        for (const Vec3& l : lights) {
            double t;
            Vec3 n;
            lit += !hitObject(scene.object, {p + l * 1e-3, l}, t, n);
        }
        return double(lit) / lights.size();
    };

    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const Vec3 dc{(x + 0.5 - cam.cx) / cam.fx, (y + 0.5 - cam.cy) / cam.fy, 1.0};
            const auto& R = cam.R;
            const Vec3 dir = Vec3{R[0] * dc.x + R[3] * dc.y + R[6] * dc.z, R[1] * dc.x + R[4] * dc.y + R[7] * dc.z,
                                  R[2] * dc.x + R[5] * dc.y + R[8] * dc.z}
                                 .normalized();
            double tObj = 1e300;
            Vec3 n;
            const bool onObject = hitObject(scene.object, {center, dir}, tObj, n);
            const double tGround = dir.z < -1e-12 ? -center.z / dir.z : 1e300;
            Vec3 colour;
            if (onObject && tObj < tGround) {
                const Vec3 p = center + dir * tObj;
                double tex = 1;
                if (scene.textureStrength > 0)
                    tex = 1 + scene.textureStrength * (2 * fractalNoise(p / scene.textureScaleMm, 7) - 1);
                const double diffuse = std::max(0.0, n.dot(light));
                const double lit = diffuse > 0 ? litFraction(p + n * 0.01) : 0.0;
                const double shade = scene.ambient + (1 - scene.ambient) * diffuse * lit;
                colour = scene.albedo * (tex * shade);
                out.mask[std::size_t(y) * out.width + x] = 255;
            } else if (tGround < 1e299) {
                const Vec3 p = center + dir * tGround;
                const bool onPaper = std::abs(p.x) <= W / 2 && std::abs(p.y) <= H / 2;
                Vec3 albedo;
                if (onPaper) {
                    const double fall = 1 - scene.paperGradient * (p.x / W + 0.5) - 0.3 * scene.paperGradient * (p.y / H + 0.5);
                    const double grain = 1 + scene.paperGrain * (2 * fractalNoise(p / 1.2, 3) - 1);
                    albedo = Vec3{0.92, 0.91, 0.88} * (fall * grain);
                } else {
                    albedo = scene.table * (1 + 0.35 * (2 * fractalNoise(p / 15.0, 5) - 1));
                }
                const double diffuse = std::max(0.0, light.z);
                const double shade = scene.ambient + (1 - scene.ambient) * diffuse * litFraction(p);
                colour = albedo * shade;
            } else {
                colour = {0.3, 0.3, 0.32}; // the room beyond the table
            }
            std::uint8_t* px = &out.rgba[(std::size_t(y) * out.width + x) * 4];
            const double c[3] = {colour.x, colour.y, colour.z};
            for (int k = 0; k < 3; ++k) {
                const double noise = scene.noise * (2 * lattice(x, y, k, seed * 7919u + 13u) - 1);
                px[k] = std::uint8_t(std::clamp(255.0 * c[k] + noise + 0.5, 0.0, 255.0));
            }
            px[3] = 255;
        }

    // The paper's corners, as Vision or the handles would give them.
    const auto world = measure::paperWorldCorners(scene.paper);
    std::array<Vec2, 4> truth{};
    out.paperInView = true;
    std::mt19937 rng(seed * 2654435761u + 17u);
    std::normal_distribution<double> jitter(0.0, std::max(cornerNoisePx, 1e-9));
    for (int m = 0; m < 4; ++m) {
        truth[m] = cam.project(world[m]) + Vec2{jitter(rng), jitter(rng)};
        out.paperInView = out.paperInView && cam.depth(world[m]) > 0 && truth[m].x >= 0 && truth[m].y >= 0
                          && truth[m].x < out.width && truth[m].y < out.height;
    }
    const int start = int(rng() % 4);
    const bool reversed = (rng() % 2) == 1;
    for (int m = 0; m < 4; ++m)
        out.corners[m] = truth[reversed ? (start - m + 8) % 4 : (start + m) % 4];
    for (int m = 0; m < 4; ++m)
        if (out.corners[m].x == truth[0].x && out.corners[m].y == truth[0].y)
            out.trueFirstCorner = m;
    out.trueReversed = reversed;
    return out;
}

} // namespace

std::vector<Photo> render(const Scene& scene, const std::vector<CameraSpec>& cameras, unsigned seed, double cornerNoisePx)
{
    std::vector<Photo> photos(cameras.size());
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    const int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 12);
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (int i = next++; i < int(cameras.size()); i = next++)
                photos[i] = renderOne(scene, cameras[i], seed * 101u + unsigned(i), cornerNoisePx);
        });
    for (auto& th : pool)
        th.join();
    return photos;
}

std::vector<measure::ScanPhoto> asScanPhotos(const std::vector<Photo>& photos, bool withMask, bool withFocal)
{
    std::vector<measure::ScanPhoto> out;
    for (const Photo& p : photos) {
        measure::ScanPhoto s;
        s.pixels = p.rgba.data();
        s.width = p.width;
        s.height = p.height;
        s.stride = p.width * 4;
        s.bytesPerPixel = 4;
        s.paperCorners = p.corners;
        s.focal35mm = withFocal ? p.focal35mm : 0;
        if (withMask) {
            s.subjectMask = p.mask.data();
            s.maskWidth = p.width;
            s.maskHeight = p.height;
            s.maskStride = p.width;
        }
        out.push_back(s);
    }
    return out;
}

} // namespace os::measure::synthetic
