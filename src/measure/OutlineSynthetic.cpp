// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/OutlineSynthetic.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace os::measure {

double boxDistance(Vec2 p, Vec2 center, Vec2 half, double angle, double cornerRadius)
{
    const double c = std::cos(-angle), s = std::sin(-angle);
    const Vec2 d = p - center;
    const Vec2 q{std::abs(c * d.x - s * d.y) - half.x + cornerRadius, std::abs(s * d.x + c * d.y) - half.y + cornerRadius};
    const Vec2 outside{std::max(q.x, 0.0), std::max(q.y, 0.0)};
    return outside.length() + std::min(std::max(q.x, q.y), 0.0) - cornerRadius;
}

double circleDistance(Vec2 p, Vec2 center, double radius) { return (p - center).length() - radius; }

namespace {

double coverage(double distanceMm, double footprintMm) { return std::clamp(0.5 - distanceMm / footprintMm, 0.0, 1.0); }

double quadDistance(Vec2 p, const std::array<Vec2, 4>& q)
{
    double area2 = 0;
    for (std::size_t i = 0; i < 4; ++i)
        area2 += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
    const double sign = area2 >= 0 ? 1.0 : -1.0;
    double inside = 1e18;
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 a = q[i], e = q[(i + 1) % 4] - a;
        const double len = e.length();
        inside = std::min(inside, sign * (e.x * (p.y - a.y) - e.y * (p.x - a.x)) / len);
    }
    return -inside;
}

void gaussianBlur(std::vector<float>& f, int w, int h, double sigma)
{
    if (sigma <= 0.05)
        return;
    const int r = std::max(1, int(std::ceil(3 * sigma)));
    std::vector<float> k(std::size_t(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) {
        k[std::size_t(i + r)] = float(std::exp(-0.5 * i * i / (sigma * sigma)));
        sum += k[std::size_t(i + r)];
    }
    for (float& v : k)
        v /= sum;
    std::vector<float> tmp(f.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i)
                acc += k[std::size_t(i + r)] * f[std::size_t(y) * w + std::clamp(x + i, 0, w - 1)];
            tmp[std::size_t(y) * w + x] = acc;
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int i = -r; i <= r; ++i)
                acc += k[std::size_t(i + r)] * tmp[std::size_t(std::clamp(y + i, 0, h - 1)) * w + x];
            f[std::size_t(y) * w + x] = acc;
        }
}

} // namespace

std::vector<std::uint8_t> renderSyntheticPhoto(const SyntheticScene& scene, const Homography& surfaceToImage, int width, int height)
{
    std::vector<std::uint8_t> out(std::size_t(width) * height * 4, 255);
    const auto toSurface = surfaceToImage.inverse();
    if (!toSurface || width <= 0 || height <= 0)
        return out;
    std::array<std::vector<float>, 3> channels;
    for (auto& c : channels)
        c.assign(std::size_t(width) * height, 0.f);
    auto mix = [](double base, double over, double amount) { return base + (over - base) * amount; };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const Vec2 pix{x + 0.5, y + 0.5};
            const Vec2 p = toSurface->map(pix);
            const Vec2 px = toSurface->map(pix + Vec2{1, 0}), py = toSurface->map(pix + Vec2{0, 1});
            const double footprint = std::sqrt(std::abs((px - p).x * (py - p).y - (px - p).y * (py - p).x));
            const double paperDist = boxDistance(p, (scene.paperMin + scene.paperMax) * 0.5, (scene.paperMax - scene.paperMin) * 0.5);
            const double paper = coverage(paperDist, footprint);
            const double card = coverage(quadDistance(p, scene.card), footprint);
            double shade = 0;
            for (const auto& part : scene.parts)
                shade = std::max(shade, std::clamp(0.5 - part.shape(p - scene.shadowOffset) / scene.shadowSoftness, 0.0, 1.0));
            const double light = (1 - scene.falloff * (x / double(width) * 0.7 + y / double(height) * 0.3)) * (1 - scene.shadow * shade);
            for (std::size_t k = 0; k < 3; ++k) {
                double c = mix(scene.table[k], scene.paper[k], paper);
                c = mix(c, scene.cardColour[k], card);
                double cover = 0;
                for (const auto& part : scene.parts) {
                    const double a = coverage(part.shape(p), footprint);
                    if (a > 0) {
                        c = mix(c, part.colour[k], a);
                        cover = std::max(cover, a);
                    }
                }
                // A part's own top is lit, not in its shadow.
                const double lit = (1 - scene.falloff * (x / double(width) * 0.7 + y / double(height) * 0.3));
                channels[k][std::size_t(y) * width + x] = float(c * mix(light, lit, cover));
            }
        }
    std::mt19937 rng(scene.seed);
    std::normal_distribution<float> noise(0.f, float(scene.noise));
    for (std::size_t k = 0; k < 3; ++k) {
        gaussianBlur(channels[k], width, height, scene.blurPx);
    }
    for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
        for (std::size_t k = 0; k < 3; ++k) {
            const float v = channels[k][i] + (scene.noise > 0 ? noise(rng) : 0.f);
            out[4 * i + k] = std::uint8_t(std::clamp(std::lround(v), 0L, 255L));
        }
    return out;
}

} // namespace os::measure
