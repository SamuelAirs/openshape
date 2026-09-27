// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/Thumbnail.h"

#include "core/Camera.h"
#include "core/Lighting.h"
#include "core/Timer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace os::interact {

namespace {

struct Rgb {
    float r = 0, g = 0, b = 0;
};

// The viewport's colors and lights (render/ViewportRenderer, mesh.frag).
constexpr Rgb kBody{0.78f, 0.80f, 0.83f};
constexpr Rgb kEdge{0.17f, 0.19f, 0.22f};

// Shades a world normal seen from `toViewer` with the viewport's studio
// lighting (core/Lighting.h, which mesh.frag evaluates too). Two-sided.
Rgb shade(const Vec3& normal, const Vec3& toViewer, const Vec3& keyDirection)
{
    const StudioLighting::Shade s = StudioLighting{}.shade(normal, toViewer, keyDirection);
    auto channel = [&](float c) { return static_cast<float>(std::min(1.0, c * s.diffuse + s.specular)); };
    return {channel(kBody.r), channel(kBody.g), channel(kBody.b)};
}

struct Frame {
    Vec3 center, right, up, forward;
    double scale = 1; // samples per mm
    int n = 0;        // samples per side

    // Sample coordinates (x right, y down) and depth (larger = farther).
    Vec3 project(const Vec3& p) const
    {
        const Vec3 d = p - center;
        return {n * 0.5 + d.dot(right) * scale, n * 0.5 - d.dot(up) * scale, d.dot(forward)};
    }
};

struct Canvas {
    int n = 0;
    std::vector<float> depth;
    std::vector<Rgb> color;
    std::vector<std::uint8_t> covered;

    explicit Canvas(int size)
        : n(size), depth(std::size_t(size) * size, std::numeric_limits<float>::infinity()), color(std::size_t(size) * size),
          covered(std::size_t(size) * size, 0)
    {
    }
};

void drawTriangle(Canvas& canvas, const Vec3& a, const Vec3& b, const Vec3& c, const Rgb& ca, const Rgb& cb, const Rgb& cc)
{
    const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(area) < 1e-12)
        return;
    const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(canvas.n - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(canvas.n - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            // Barycentric weights (either winding: the mesh is two-sided here).
            const double wa = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            const double wb = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            const double wc = 1.0 - wa - wb;
            if (wa < -1e-9 || wb < -1e-9 || wc < -1e-9)
                continue;
            const float z = float(wa * a.z + wb * b.z + wc * c.z);
            const std::size_t i = std::size_t(y) * canvas.n + x;
            if (z >= canvas.depth[i])
                continue;
            canvas.depth[i] = z;
            canvas.color[i] = {float(wa * ca.r + wb * cb.r + wc * cc.r), float(wa * ca.g + wb * cb.g + wc * cc.g),
                               float(wa * ca.b + wb * cb.b + wc * cc.b)};
            canvas.covered[i] = 1;
        }
    }
}

// A line of `radius` samples, drawn where it is not behind a surface.
void drawEdge(Canvas& canvas, const Vec3& a, const Vec3& b, double radius, double bias)
{
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    const int steps = std::max(1, int(std::ceil(length * 2)));
    const int r = int(std::ceil(radius));
    for (int k = 0; k <= steps; ++k) {
        const double t = double(k) / steps;
        const double sx = a.x + (b.x - a.x) * t, sy = a.y + (b.y - a.y) * t, sz = a.z + (b.z - a.z) * t;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int x = int(std::floor(sx)) + dx, y = int(std::floor(sy)) + dy;
                if (x < 0 || y < 0 || x >= canvas.n || y >= canvas.n)
                    continue;
                if (std::hypot(x + 0.5 - sx, y + 0.5 - sy) > radius)
                    continue;
                const std::size_t i = std::size_t(y) * canvas.n + x;
                if (canvas.covered[i] && sz > canvas.depth[i] + bias)
                    continue; // hidden
                canvas.color[i] = kEdge;
                canvas.covered[i] = 1;
                canvas.depth[i] = std::min(canvas.depth[i], float(sz));
            }
        }
    }
}

} // namespace

ThumbnailImage renderThumbnail(const std::vector<std::shared_ptr<const geom::Mesh>>& meshes, int size)
{
    ScopedTimer timer("renderThumbnail");
    if (size <= 0)
        return {};
    // The isometric view, like the Iso button.
    Camera camera;
    camera.setStandardView(StandardView::Isometric);
    Frame frame;
    frame.right = camera.right();
    frame.up = camera.up();
    frame.forward = camera.forward();

    // Frame the visible extent (in view space) with a small margin.
    double minX = std::numeric_limits<double>::infinity(), maxX = -minX, minY = minX, maxY = -minX;
    for (const auto& mesh : meshes)
        for (std::size_t i = 0; mesh && i < mesh->vertexCount(); ++i) {
            const Vec3 p = mesh->vertex(i);
            minX = std::min(minX, p.dot(frame.right));
            maxX = std::max(maxX, p.dot(frame.right));
            minY = std::min(minY, p.dot(frame.up));
            maxY = std::max(maxY, p.dot(frame.up));
        }
    if (!(maxX >= minX) || !(maxY >= minY))
        return {};
    constexpr int kSamples = 2; // per pixel and side
    frame.n = size * kSamples;
    const double extent = std::max({maxX - minX, maxY - minY, 1e-6});
    frame.scale = frame.n * 0.88 / extent; // 6 % margin on each side
    frame.center = frame.right * ((minX + maxX) / 2) + frame.up * ((minY + maxY) / 2);
    const double bias = 0.015 * extent; // mm: an edge on a face in front hides behind it

    Canvas canvas(frame.n);
    const Vec3 backward = frame.forward * -1.0;
    const Vec3 key = StudioLighting{}.keyDirection(camera);
    for (const auto& mesh : meshes) {
        if (!mesh)
            continue;
        std::vector<Vec3> projected(mesh->vertexCount());
        std::vector<Rgb> colors(mesh->vertexCount());
        for (std::size_t i = 0; i < mesh->vertexCount(); ++i) {
            projected[i] = frame.project(mesh->vertex(i));
            const Vec3 n{mesh->normals[3 * i], mesh->normals[3 * i + 1], mesh->normals[3 * i + 2]};
            colors[i] = shade(n, backward, key);
        }
        for (std::size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
            const auto ia = mesh->indices[t], ib = mesh->indices[t + 1], ic = mesh->indices[t + 2];
            drawTriangle(canvas, projected[ia], projected[ib], projected[ic], colors[ia], colors[ib], colors[ic]);
        }
    }
    const double edgeRadius = 0.6 * kSamples;
    for (const auto& mesh : meshes) {
        if (!mesh)
            continue;
        for (const auto& edge : mesh->edges)
            for (std::size_t k = 0; k + 5 < edge.points.size(); k += 3) {
                const Vec3 a{edge.points[k], edge.points[k + 1], edge.points[k + 2]};
                const Vec3 b{edge.points[k + 3], edge.points[k + 4], edge.points[k + 5]};
                drawEdge(canvas, frame.project(a), frame.project(b), edgeRadius, bias);
            }
    }

    // Average the samples of each pixel (premultiplied, so edges blend into
    // the transparent background).
    ThumbnailImage image;
    image.width = image.height = size;
    image.rgba.assign(std::size_t(size) * size * 4, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < kSamples; ++sy)
                for (int sx = 0; sx < kSamples; ++sx) {
                    const std::size_t i = std::size_t(y * kSamples + sy) * frame.n + (x * kSamples + sx);
                    if (!canvas.covered[i])
                        continue;
                    r += canvas.color[i].r;
                    g += canvas.color[i].g;
                    b += canvas.color[i].b;
                    a += 1;
                }
            const double k = 1.0 / (kSamples * kSamples);
            std::uint8_t* out = &image.rgba[(std::size_t(y) * size + x) * 4];
            out[0] = std::uint8_t(std::lround(std::clamp(r * k, 0.0, 1.0) * 255));
            out[1] = std::uint8_t(std::lround(std::clamp(g * k, 0.0, 1.0) * 255));
            out[2] = std::uint8_t(std::lround(std::clamp(b * k, 0.0, 1.0) * 255));
            out[3] = std::uint8_t(std::lround(std::clamp(a * k, 0.0, 1.0) * 255));
        }
    return image;
}

} // namespace os::interact
