// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScanMask.h"

#include "measure/ScanMath.h"
#include "measure/ScanPose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace os::measure {

namespace {

struct Tap {
    int index;
    float weight;
};

// For each output sample, the input samples its footprint covers and how much.
std::vector<std::vector<Tap>> areaTaps(int inSize, int outSize)
{
    std::vector<std::vector<Tap>> taps(outSize);
    const double scale = double(inSize) / outSize;
    for (int o = 0; o < outSize; ++o) {
        const double a = o * scale, b = (o + 1) * scale;
        for (int i = int(std::floor(a)); i < int(std::ceil(b)); ++i) {
            const double lo = std::max(a, double(i)), hi = std::min(b, double(i + 1));
            if (hi > lo && i >= 0 && i < inSize)
                taps[o].push_back({i, float((hi - lo) / scale)});
        }
    }
    return taps;
}

// 3 x 3 binary erosion / dilation (outside the image counts as false).
std::vector<std::uint8_t> morph(const std::vector<std::uint8_t>& in, int w, int h, bool dilate)
{
    // Separable: a 3 x 1 pass, then a 1 x 3 pass.
    const auto combine = [dilate](bool a, bool b, bool c) { return dilate ? (a || b || c) : (a && b && c); };
    std::vector<std::uint8_t> rows(in.size()), out(in.size());
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* src = &in[std::size_t(y) * w];
        std::uint8_t* dst = &rows[std::size_t(y) * w];
        for (int x = 0; x < w; ++x)
            dst[x] = combine(x > 0 && src[x - 1], src[x] != 0, x + 1 < w && src[x + 1]) ? 1 : 0;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            out[i] = combine(y > 0 && rows[i - w], rows[i] != 0, y + 1 < h && rows[i + w]) ? 1 : 0;
        }
    return out;
}

} // namespace

RgbImage downsampleRgb(const std::uint8_t* data, int width, int height, int stride, int bytesPerPixel, bool bgr, int maxSide)
{
    RgbImage out;
    if (!data || width <= 0 || height <= 0 || (bytesPerPixel != 3 && bytesPerPixel != 4))
        return out;
    const double scale = std::min(1.0, double(std::max(1, maxSide)) / std::max(width, height));
    out.width = std::max(1, int(std::lround(width * scale)));
    out.height = std::max(1, int(std::lround(height * scale)));
    out.rgb.resize(std::size_t(out.width) * out.height * 3);
    const int r = bgr ? 2 : 0, b = bgr ? 0 : 2;
    if (out.width == width && out.height == height) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* p = data + std::ptrdiff_t(y) * stride + std::ptrdiff_t(x) * bytesPerPixel;
                std::uint8_t* q = &out.rgb[(std::size_t(y) * width + x) * 3];
                q[0] = p[r];
                q[1] = p[1];
                q[2] = p[b];
            }
        return out;
    }
    const auto tx = areaTaps(width, out.width);
    const auto ty = areaTaps(height, out.height);
    std::vector<float> row(std::size_t(out.width) * 3);
    std::vector<float> acc(std::size_t(out.width) * 3);
    for (int oy = 0; oy < out.height; ++oy) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        for (const Tap& vy : ty[oy]) {
            const std::uint8_t* src = data + std::ptrdiff_t(vy.index) * stride;
            for (int ox = 0; ox < out.width; ++ox) {
                float s0 = 0, s1 = 0, s2 = 0;
                for (const Tap& vx : tx[ox]) {
                    const std::uint8_t* p = src + std::ptrdiff_t(vx.index) * bytesPerPixel;
                    s0 += vx.weight * p[r];
                    s1 += vx.weight * p[1];
                    s2 += vx.weight * p[b];
                }
                acc[3 * ox] += vy.weight * s0;
                acc[3 * ox + 1] += vy.weight * s1;
                acc[3 * ox + 2] += vy.weight * s2;
            }
        }
        std::uint8_t* q = &out.rgb[std::size_t(oy) * out.width * 3];
        for (std::size_t k = 0; k < acc.size(); ++k)
            q[k] = std::uint8_t(std::clamp(acc[k] + 0.5f, 0.0f, 255.0f));
    }
    return out;
}

std::vector<std::uint8_t> resampleMask(const std::uint8_t* data, int width, int height, int stride, int outWidth, int outHeight)
{
    std::vector<std::uint8_t> out;
    if (!data || width <= 0 || height <= 0 || outWidth <= 0 || outHeight <= 0)
        return out;
    out.resize(std::size_t(outWidth) * outHeight);
    const double sx = double(width) / outWidth, sy = double(height) / outHeight;
    for (int y = 0; y < outHeight; ++y) {
        const double fy = std::clamp((y + 0.5) * sy - 0.5, 0.0, height - 1.0);
        const int y0 = int(fy), y1 = std::min(y0 + 1, height - 1);
        const double ty = fy - y0;
        for (int x = 0; x < outWidth; ++x) {
            const double fx = std::clamp((x + 0.5) * sx - 0.5, 0.0, width - 1.0);
            const int x0 = int(fx), x1 = std::min(x0 + 1, width - 1);
            const double tx = fx - x0;
            const auto at = [&](int xx, int yy) { return double(data[std::ptrdiff_t(yy) * stride + xx]); };
            const double top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
            const double bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
            out[std::size_t(y) * outWidth + x] = std::uint8_t(std::clamp(top + (bottom - top) * ty + 0.5, 0.0, 255.0));
        }
    }
    return out;
}

bool pixelToPaperPlane(const ScanCamera& camera, Vec2 pixel, Vec2& planeMm)
{
    const Vec3 d{(pixel.x - camera.cx) / camera.fx, (pixel.y - camera.cy) / camera.fy, 1.0};
    const auto& R = camera.R;
    const Vec3 dir{R[0] * d.x + R[3] * d.y + R[6] * d.z, R[1] * d.x + R[4] * d.y + R[7] * d.z,
                   R[2] * d.x + R[5] * d.y + R[8] * d.z};
    const Vec3 c = camera.center();
    if (!(dir.z < -1e-9))
        return false;
    const double s = -c.z / dir.z;
    planeMm = {c.x + s * dir.x, c.y + s * dir.y};
    return true;
}

std::array<double, 3> PaperColourModel::at(Vec2 p) const
{
    std::array<double, 3> out{};
    for (int c = 0; c < 3; ++c)
        out[c] = coefficients[c][0] + coefficients[c][1] * p.x + coefficients[c][2] * p.y;
    return out;
}

PaperColourModel fitPaperColour(const RgbImage& image, const ScanCamera& camera, const Reference& paper)
{
    PaperColourModel model;
    const double hw = std::max(paper.widthMm, paper.heightMm) * 0.5;
    const double hh = std::min(paper.widthMm, paper.heightMm) * 0.5;
    struct Sample {
        double x, y;
        std::array<double, 3> c;
    };
    std::vector<Sample> samples;
    const int step = std::max(1, std::max(image.width, image.height) / 800);
    for (int y = 0; y < image.height; y += step)
        for (int x = 0; x < image.width; x += step) {
            Vec2 p;
            if (!pixelToPaperPlane(camera, {x + 0.5, y + 0.5}, p))
                continue;
            const double inset = std::min(hw - std::abs(p.x), hh - std::abs(p.y));
            if (inset < 4 || inset > 15)
                continue;
            const std::uint8_t* q = &image.rgb[(std::size_t(y) * image.width + x) * 3];
            samples.push_back({p.x / hw, p.y / hh, {double(q[0]), double(q[1]), double(q[2])}});
        }
    if (samples.size() < 30)
        return model;
    // The paper is the brightest large thing near its edge: start from the
    // samples near the bright end, then trim on the fitted model.
    std::vector<double> bright;
    bright.reserve(samples.size());
    for (const Sample& s : samples)
        bright.push_back(s.c[0] + s.c[1] + s.c[2]);
    std::vector<double> sorted = bright;
    const std::size_t p90 = sorted.size() * 9 / 10;
    std::nth_element(sorted.begin(), sorted.begin() + p90, sorted.end());
    const double top = sorted[p90];
    std::vector<char> inlier(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
        inlier[i] = bright[i] >= 0.7 * top;
    std::array<std::array<double, 3>, 3> coef{};
    for (int round = 0; round < 4; ++round) {
        std::vector<double> a(9, 0.0);
        std::array<std::vector<double>, 3> rhs{std::vector<double>(3, 0.0), std::vector<double>(3, 0.0),
                                               std::vector<double>(3, 0.0)};
        int n = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (!inlier[i])
                continue;
            const double v[3] = {1, samples[i].x, samples[i].y};
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c)
                    a[r * 3 + c] += v[r] * v[c];
                for (int ch = 0; ch < 3; ++ch)
                    rhs[ch][r] += v[r] * samples[i].c[ch];
            }
            ++n;
        }
        if (n < 20)
            return model;
        for (int ch = 0; ch < 3; ++ch) {
            std::vector<double> x;
            if (!scanmath::solve(a, rhs[ch], 3, x))
                return model;
            coef[ch] = {x[0], x[1], x[2]};
        }
        const double tolerance = round < 2 ? 0.15 : 0.08;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            bool ok = true;
            for (int ch = 0; ch < 3 && ok; ++ch) {
                const double m = coef[ch][0] + coef[ch][1] * samples[i].x + coef[ch][2] * samples[i].y;
                ok = m > 1 && std::abs(samples[i].c[ch] / m - 1) < tolerance;
            }
            inlier[i] = ok;
        }
        model.samples = n;
    }
    // Back to millimeters.
    for (int ch = 0; ch < 3; ++ch)
        model.coefficients[ch] = {coef[ch][0], coef[ch][1] / hw, coef[ch][2] / hh};
    model.valid = true;
    return model;
}

std::vector<std::uint8_t> segmentPhoto(const RgbImage& image, const ScanCamera& camera, const Reference& paper,
                                       const std::uint8_t* subjectMask, const MaskOptions& options)
{
    const int w = image.width, h = image.height;
    std::vector<std::uint8_t> state(std::size_t(w) * h, kMaskUnknown);
    if (w <= 0 || h <= 0)
        return state;
    const PaperColourModel model = fitPaperColour(image, camera, paper);
    const double hw = std::max(paper.widthMm, paper.heightMm) * 0.5;
    const double hh = std::min(paper.widthMm, paper.heightMm) * 0.5;
    // Which pixels were judged by their colour on the paper (only those are
    // cleaned up below; a subject mask's verdict is kept as it is).
    std::vector<std::uint8_t> byColour(state.size(), 0);
    std::vector<std::uint8_t> interior(state.size(), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            const int subject = subjectMask ? (subjectMask[i] >= 128 ? 1 : 0) : -1;
            Vec2 p;
            const bool onTable = pixelToPaperPlane(camera, {x + 0.5, y + 0.5}, p);
            const double inset = onTable ? std::min(hw - std::abs(p.x), hh - std::abs(p.y)) : -1;
            if (inset < options.edgeMm) {
                // Off the paper, or on its edge: only a subject mask knows.
                if (subject >= 0)
                    state[i] = subject ? kMaskObject : kMaskBackground;
                continue;
            }
            interior[i] = 1;
            if (!model.valid) {
                if (subject >= 0)
                    state[i] = subject ? kMaskObject : kMaskBackground;
                continue;
            }
            const std::uint8_t* q = &image.rgb[i * 3];
            const auto m = model.at(p);
            const double ms = std::max(1.0, m[0]) + std::max(1.0, m[1]) + std::max(1.0, m[2]);
            const double lum = (double(q[0]) + q[1] + q[2]) / ms;
            double chroma = 0;
            for (int c = 0; c < 3; ++c)
                chroma = std::max(chroma, std::abs(q[c] / std::max(1.0, m[c]) - lum));
            chroma /= std::max(lum, 0.05);
            const double slack = std::clamp((1 - lum) / (1 - options.shadowFloor), 0.0, 1.0);
            const bool paperColoured = lum >= options.shadowFloor && lum <= 1.3 && chroma < 0.10 + 0.10 * slack;
            const bool litPaper = paperColoured && lum >= 0.8 && chroma < 0.08;
            if (subject < 0) {
                state[i] = paperColoured ? kMaskBackground : kMaskObject;
                byColour[i] = 1;
            } else if (subject == 1) {
                state[i] = options.trustLitPaperOverMask && litPaper ? kMaskBackground : kMaskObject;
            } else {
                // The mask says background but the colour is not the paper's:
                // a part the mask missed, or a mark on the paper. Not sure.
                state[i] = paperColoured ? kMaskBackground : kMaskUnknown;
            }
        }
    }

    // Colour noise: an opening drops specks called object, a closing fills
    // pinholes; pinholes inside the object (highlights) are filled.
    std::vector<std::uint8_t> object(state.size());
    for (std::size_t i = 0; i < state.size(); ++i)
        object[i] = state[i] == kMaskObject;
    const auto opened = morph(morph(object, w, h, false), w, h, true);
    for (std::size_t i = 0; i < state.size(); ++i)
        if (object[i] && !opened[i] && byColour[i])
            state[i] = kMaskBackground, object[i] = 0;
    const auto closed = morph(morph(object, w, h, true), w, h, false);
    for (std::size_t i = 0; i < state.size(); ++i)
        if (closed[i] && !object[i] && interior[i])
            state[i] = kMaskObject;

    // Holes: background regions on the paper enclosed by the object and
    // small (a highlight, a label) become object; carving there would bore
    // through it. A real through-hole stays filled: carving is conservative.
    const std::size_t maxHole = std::max<std::size_t>(16, state.size() / 400);
    std::vector<int> label(state.size(), -1);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> members;
    for (std::size_t seed = 0; seed < state.size(); ++seed) {
        if (label[seed] != -1 || state[seed] != kMaskBackground)
            continue;
        members.clear();
        bool enclosed = true;
        stack.push_back(seed);
        label[seed] = 0;
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            members.push_back(i);
            const int x = int(i % w), y = int(i / w);
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
                enclosed = false;
            const int nx[4] = {x - 1, x + 1, x, x};
            const int ny[4] = {y, y, y - 1, y + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h)
                    continue;
                const std::size_t j = std::size_t(ny[k]) * w + nx[k];
                if (state[j] == kMaskUnknown)
                    enclosed = false;
                if (label[j] == -1 && state[j] == kMaskBackground) {
                    label[j] = 0;
                    stack.push_back(j);
                }
            }
        }
        if (enclosed && members.size() <= maxHole)
            for (std::size_t i : members)
                state[i] = kMaskObject;
    }
    return state;
}

} // namespace os::measure
