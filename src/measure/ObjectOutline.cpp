// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ObjectOutline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace os::measure {

namespace {

using Rgb = std::array<double, 3>;

double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

Vec2 unit(Vec2 v)
{
    const double l = v.length();
    return l > 0 ? v * (1.0 / l) : Vec2{};
}

// The signed distance from p to a convex quadrilateral's edges, positive
// inside (either winding).
double insideDistance(Vec2 p, const std::array<Vec2, 4>& q)
{
    double area2 = 0;
    for (std::size_t i = 0; i < 4; ++i)
        area2 += cross(q[i], q[(i + 1) % 4]);
    const double sign = area2 >= 0 ? 1.0 : -1.0;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec2 a = q[i], e = q[(i + 1) % 4] - a;
        const double len = e.length();
        if (len <= 0)
            continue;
        best = std::min(best, sign * cross(e, p - a) / len);
    }
    return best;
}

// ---------------------------------------------------------------- masks

// A square max (dilate) or min (erode) filter of radius r, in two passes;
// the window is clipped at the image's edge.
void squareFilter(std::vector<std::uint8_t>& m, int w, int h, int r, bool dilate)
{
    if (r <= 0)
        return;
    std::vector<std::uint8_t> tmp(m.size());
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = m.data() + std::size_t(y) * w;
        std::uint8_t* out = tmp.data() + std::size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - r), x1 = std::min(w - 1, x + r);
            std::uint8_t v = dilate ? 0 : 1;
            for (int k = x0; k <= x1; ++k)
                v = dilate ? std::max(v, row[k]) : std::min(v, row[k]);
            out[x] = v;
        }
    }
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - r), y1 = std::min(h - 1, y + r);
        std::uint8_t* out = m.data() + std::size_t(y) * w;
        for (int x = 0; x < w; ++x) {
            std::uint8_t v = dilate ? 0 : 1;
            for (int k = y0; k <= y1; ++k) {
                const std::uint8_t s = tmp[std::size_t(k) * w + x];
                v = dilate ? std::max(v, s) : std::min(v, s);
            }
            out[x] = v;
        }
    }
}

// Connected pieces of the set pixels (8- or 4-connected): a label per
// pixel (0 = none), and how many.
int labelPieces(const std::vector<std::uint8_t>& m, int w, int h, bool eight, std::vector<int>& labels)
{
    labels.assign(m.size(), 0);
    int count = 0;
    std::vector<int> stack;
    for (int start = 0; start < w * h; ++start) {
        if (!m[std::size_t(start)] || labels[std::size_t(start)])
            continue;
        ++count;
        labels[std::size_t(start)] = count;
        stack.push_back(start);
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int x = i % w, y = i / w;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dy == 0) || (!eight && dx != 0 && dy != 0))
                        continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const int j = ny * w + nx;
                    if (m[std::size_t(j)] && !labels[std::size_t(j)]) {
                        labels[std::size_t(j)] = count;
                        stack.push_back(j);
                    }
                }
        }
    }
    return count;
}

// A [1 2 1] / 4 smoothing in both directions (edges clamped).
void smooth121(std::vector<float>& f, int w, int h)
{
    std::vector<float> tmp(f.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* row = f.data() + std::size_t(y) * w;
            tmp[std::size_t(y) * w + x] = 0.25f * (row[std::max(0, x - 1)] + 2 * row[x] + row[std::min(w - 1, x + 1)]);
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int ya = std::max(0, y - 1), yb = std::min(h - 1, y + 1);
            f[std::size_t(y) * w + x] = 0.25f * (tmp[std::size_t(ya) * w + x] + 2 * tmp[std::size_t(y) * w + x] + tmp[std::size_t(yb) * w + x]);
        }
}

// ---------------------------------------------------------------- the paper

Rgb pixel(const RgbImage& img, std::size_t i)
{
    return {double(img.rgb[3 * i]), double(img.rgb[3 * i + 1]), double(img.rgb[3 * i + 2])};
}

double colourDistance(const Rgb& a, const Rgb& b)
{
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

double norm(const Rgb& a) { return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

// The most common colour among the chosen pixels (16 levels a channel),
// refined as the mean of the pixels near it; candidates distinct from
// `taken` by `apart`. Up to `count` of them, most common first.
std::vector<Rgb> commonColours(const RgbImage& img, const std::vector<std::size_t>& pixels, int count, const std::vector<Rgb>& taken,
                               double apart)
{
    std::vector<int> hist(4096, 0);
    for (std::size_t i : pixels)
        ++hist[std::size_t((img.rgb[3 * i] >> 4) << 8 | (img.rgb[3 * i + 1] >> 4) << 4 | (img.rgb[3 * i + 2] >> 4))];
    std::vector<int> order(4096);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return hist[std::size_t(a)] > hist[std::size_t(b)]; });
    std::vector<Rgb> out;
    std::vector<Rgb> all = taken;
    const double minCount = 0.03 * double(pixels.size());
    for (int bin : order) {
        if (int(out.size()) >= count || hist[std::size_t(bin)] < std::max(1.0, minCount))
            break;
        const Rgb centre{double((bin >> 8) & 15) * 16 + 8, double((bin >> 4) & 15) * 16 + 8, double(bin & 15) * 16 + 8};
        if (std::any_of(all.begin(), all.end(), [&](const Rgb& c) { return colourDistance(c, centre) < apart; }))
            continue;
        Rgb sum{0, 0, 0};
        int n = 0;
        for (std::size_t i : pixels) {
            const Rgb c = pixel(img, i);
            if (colourDistance(c, centre) < 28) {
                for (int k = 0; k < 3; ++k)
                    sum[std::size_t(k)] += c[std::size_t(k)];
                ++n;
            }
        }
        const Rgb mean = n ? Rgb{sum[0] / n, sum[1] / n, sum[2] / n} : centre;
        out.push_back(mean);
        all.push_back(mean);
    }
    return out;
}

// The paper's colour as a plane over the photo, per channel (a + b x + c y
// on x / w, y / h), fitted to pixels near `guess` (twice, the second time
// near the first fit: the falloff of the light, not the part or shadows).
struct PaperModel {
    std::array<Rgb, 3> coefficients{}; // [channel] = {a, b, c}
    Rgb at(double x, double y) const
    {
        Rgb c;
        for (std::size_t k = 0; k < 3; ++k)
            c[k] = std::max(6.0, coefficients[k][0] + coefficients[k][1] * x + coefficients[k][2] * y);
        return c;
    }
};

PaperModel fitPaper(const RgbImage& img, const std::vector<std::uint8_t>& usable, const Rgb& guess)
{
    PaperModel model;
    for (std::size_t k = 0; k < 3; ++k)
        model.coefficients[k] = {guess[k], 0, 0};
    const int w = img.width, h = img.height;
    const int step = std::max(1, int(std::sqrt(double(w) * h / 120000.0)));
    for (int pass = 0; pass < 2; ++pass) {
        double ata[3][3] = {};
        double atb[3][3] = {};
        int n = 0;
        for (int y = 0; y < h; y += step)
            for (int x = 0; x < w; x += step) {
                const std::size_t i = std::size_t(y) * w + x;
                if (!usable[i])
                    continue;
                const double u = (x + 0.5) / w, v = (y + 0.5) / h;
                const Rgb c = pixel(img, i);
                const Rgb ref = model.at(u, v);
                const double limit = pass == 0 ? 0.2 * norm(ref) + 20 : 0.1 * norm(ref) + 10;
                if (colourDistance(c, ref) > limit)
                    continue;
                const double a[3] = {1, u, v};
                for (int r = 0; r < 3; ++r) {
                    for (int s = 0; s < 3; ++s)
                        ata[r][s] += a[r] * a[s];
                    for (int k = 0; k < 3; ++k)
                        atb[k][r] += a[r] * c[std::size_t(k)];
                }
                ++n;
            }
        if (n < 300)
            break;
        auto det3 = [](const double m[3][3]) {
            return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                 + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        };
        const double d = det3(ata);
        if (std::abs(d) < 1e-9 * std::pow(ata[0][0], 3))
            break;
        for (std::size_t k = 0; k < 3; ++k)
            for (int col = 0; col < 3; ++col) {
                double mk[3][3];
                for (int r = 0; r < 3; ++r)
                    for (int s = 0; s < 3; ++s)
                        mk[r][s] = s == col ? atb[k][r] : ata[r][s];
                model.coefficients[k][std::size_t(col)] = det3(mk) / d;
            }
    }
    return model;
}

// How unlike the paper a colour is: darkening (shading) counts as itself
// (0 for the paper, 1 for black), lightening as 1 - 1/s, and a change of
// colour (what shading alone can't explain) twice.
double difference(const Rgb& c, const Rgb& p)
{
    const double pp = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    const double s = (c[0] * p[0] + c[1] * p[1] + c[2] * p[2]) / pp;
    const double dark = std::max(0.0, 1 - s);
    const double light = s > 1 ? 1 - 1 / s : 0.0;
    const Rgb r{c[0] - s * p[0], c[1] - s * p[1], c[2] - s * p[2]};
    const double e = norm(r) / std::max({norm(c), std::sqrt(pp), 1.0});
    return std::sqrt(dark * dark + light * light + 4 * e * e);
}

// Otsu's threshold of values in [0, top].
double otsu(const std::vector<float>& values, const std::vector<std::uint8_t>& usable, double top)
{
    std::array<double, 256> hist{};
    double total = 0;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (usable[i]) {
            const int b = std::clamp(int(values[i] / top * 255.0), 0, 255);
            hist[std::size_t(b)] += 1;
            total += 1;
        }
    if (total <= 0)
        return top / 2;
    double sumAll = 0;
    for (int b = 0; b < 256; ++b)
        sumAll += b * hist[std::size_t(b)];
    double w0 = 0, sum0 = 0, best = -1;
    int bestB = 128;
    for (int b = 0; b < 255; ++b) {
        w0 += hist[std::size_t(b)];
        sum0 += b * hist[std::size_t(b)];
        const double w1 = total - w0;
        if (w0 <= 0 || w1 <= 0)
            continue;
        const double m0 = sum0 / w0, m1 = (sumAll - sum0) / w1;
        const double between = w0 * w1 * (m0 - m1) * (m0 - m1);
        if (between > best) {
            best = between;
            bestB = b;
        }
    }
    return (bestB + 1) * top / 255.0;
}

// ---------------------------------------------------------------- the edge

// The boundary of the set pixels of `m` (w x h) as closed loops of pixel
// corners, the set pixels on the right (y down): the outer edge runs one
// way (positive shoelace area), holes the other. Diagonal neighbours stay
// joined.
std::vector<std::vector<Vec2>> crackLoops(const std::vector<std::uint8_t>& m, int w, int h, int ox, int oy)
{
    const int vw = w + 1;
    std::vector<std::uint8_t> out(std::size_t(vw) * (h + 1), 0);
    auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && m[std::size_t(y) * w + x]; };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!at(x, y))
                continue;
            if (!at(x, y - 1))
                out[std::size_t(y) * vw + x] |= 1; // +x
            if (!at(x + 1, y))
                out[std::size_t(y) * vw + x + 1] |= 2; // +y
            if (!at(x, y + 1))
                out[std::size_t(y + 1) * vw + x + 1] |= 4; // -x
            if (!at(x - 1, y))
                out[std::size_t(y + 1) * vw + x] |= 8; // -y
        }
    const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, 1, 0, -1};
    std::vector<std::vector<Vec2>> loops;
    for (int v0 = 0; v0 < int(out.size()); ++v0) {
        while (out[std::size_t(v0)]) {
            int d0 = 0;
            while (!(out[std::size_t(v0)] & (1 << d0)))
                ++d0;
            out[std::size_t(v0)] &= std::uint8_t(~(1 << d0));
            std::vector<Vec2> loop;
            int v = v0, d = d0;
            for (;;) {
                loop.push_back({double(v % vw + ox), double(v / vw + oy)});
                v = (v / vw + dy[d]) * vw + (v % vw + dx[d]);
                bool closed = false, moved = false;
                for (const int c : {(d + 3) % 4, d, (d + 1) % 4}) {
                    if (v == v0 && c == d0) {
                        closed = true;
                        break;
                    }
                    if (out[std::size_t(v)] & (1 << c)) {
                        out[std::size_t(v)] &= std::uint8_t(~(1 << c));
                        d = c;
                        moved = true;
                        break;
                    }
                }
                if (closed || !moved)
                    break;
            }
            if (loop.size() >= 4)
                loops.push_back(std::move(loop));
        }
    }
    return loops;
}

double shoelace(const std::vector<Vec2>& pts)
{
    double s = 0;
    for (std::size_t i = 0; i < pts.size(); ++i)
        s += cross(pts[i], pts[(i + 1) % pts.size()]);
    return s * 0.5;
}

// A float field over a window of the photo, sampled bilinearly at pixel
// coordinates (pixel centers at +0.5).
struct Field {
    int ox = 0, oy = 0, w = 0, h = 0;
    std::vector<float> v;
    double sample(Vec2 p) const
    {
        const double fx = std::clamp(p.x - 0.5 - ox, 0.0, double(w - 1)), fy = std::clamp(p.y - 0.5 - oy, 0.0, double(h - 1));
        const int x0 = std::min(int(fx), w - 2 < 0 ? 0 : w - 2), y0 = std::min(int(fy), h - 2 < 0 ? 0 : h - 2);
        const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const double tx = fx - x0, ty = fy - y0;
        const double a = v[std::size_t(y0) * w + x0], b = v[std::size_t(y0) * w + x1];
        const double c = v[std::size_t(y1) * w + x0], d = v[std::size_t(y1) * w + x1];
        return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
    }
};

// The crack loop moved onto the edge. Along each point's normal (within
// `reach` pixels) the part's share of the colour (`share`: 0 paper, 1 part)
// is sampled; the part's edge is its steepest rise (a soft shadow beside the
// part rises gently; the part's own edge is a step as sharp as the lens),
// and the point goes where the share is half way between its levels just
// outside and just inside that step: the edge stays where the part begins,
// not where its shadow ends, even when the mask took in some of the shadow.
// Points with no clear step stay; then all are lightly smoothed.
std::vector<Vec2> refineEdge(const std::vector<Vec2>& loop, const Field& share, double reach)
{
    const std::size_t n = loop.size();
    std::vector<Vec2> out(n);
    const std::size_t k = 3;
    const double step = 0.25;
    const int m = int(std::ceil(reach / step));
    std::vector<double> f(std::size_t(2 * m + 1));
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 t = unit(loop[(i + k) % n] - loop[(i + n - k) % n]);
        const Vec2 inward{-t.y, t.x}; // the set pixels lie on the right, y down
        for (int j = -m; j <= m; ++j)
            f[std::size_t(j + m)] = share.sample(loop[i] + inward * (j * step));
        auto at = [&](double tt) {
            const double u = std::clamp(tt / step + m, 0.0, double(2 * m));
            const int j0 = std::min(int(u), 2 * m - 1);
            return f[std::size_t(j0)] + (f[std::size_t(j0 + 1)] - f[std::size_t(j0)]) * (u - j0);
        };
        // The part's edge: a rise over one pixel into the part's own colour
        // (a shadow's edge rises only to the shadow's level), the one nearest
        // the crack (a hole's far side further in rises as much); failing
        // that, the steepest rise.
        auto mean = [&](double from, double to) {
            double sum = 0;
            int count = 0;
            for (double tt = from; tt <= to + 1e-9; tt += 0.25, ++count)
                sum += at(tt);
            return count ? sum / count : 0.0;
        };
        double best = 0, edgeT = 0, steepest = 0, steepestT = 0;
        bool intoPart = false;
        for (int j = -m + 3; j <= m - 3; ++j) {
            const double g = f[std::size_t(j + m + 2)] - f[std::size_t(j + m - 2)];
            const double tt = j * step;
            if (g > steepest) {
                steepest = g;
                steepestT = tt;
            }
            const bool peak = g >= f[std::size_t(j + m + 1)] - f[std::size_t(j + m - 3)]
                           && g >= f[std::size_t(j + m + 3)] - f[std::size_t(j + m - 1)];
            if (!peak || g < 0.15)
                continue;
            const double in = mean(tt + 1.5, tt + 2.5), before = mean(tt - 2.5, tt - 1.5);
            if (in < 0.7 || in - before < 0.25)
                continue;
            if (!intoPart || std::abs(tt) < std::abs(edgeT)) {
                intoPart = true;
                best = g;
                edgeT = tt;
            }
        }
        if (!intoPart) {
            best = steepest;
            edgeT = steepestT;
        }
        double center = 0;
        if (best >= 0.08) {
            double outside = 0, inside = 0;
            for (int s = 0; s < 5; ++s) {
                outside += at(edgeT - 2 - 0.25 * s) / 5;
                inside += at(edgeT + 2 + 0.25 * s) / 5;
            }
            const double level = 0.5 * (inside + outside);
            center = edgeT;
            double bestD = 1e9;
            for (double tt = edgeT - 1.5; tt < edgeT + 1.5; tt += 0.0625) {
                const double a = at(tt) - level, b = at(tt + 0.0625) - level;
                if (a < 0 && b >= 0) {
                    const double x = tt + 0.0625 * (-a / (b - a));
                    if (std::abs(x - edgeT) < bestD) {
                        bestD = std::abs(x - edgeT);
                        center = x;
                    }
                }
            }
        }
        out[i] = loop[i] + inward * center;
    }
    std::vector<Vec2> smooth(n);
    for (std::size_t i = 0; i < n; ++i)
        smooth[i] = (out[(i + n - 1) % n] + out[i] * 2.0 + out[(i + 1) % n]) * 0.25;
    std::vector<Vec2> kept;
    for (const Vec2& p : smooth)
        if (kept.empty() || (p - kept.back()).length() > 0.2)
            kept.push_back(p);
    while (kept.size() > 3 && (kept.back() - kept.front()).length() <= 0.2)
        kept.pop_back();
    return kept;
}

// ---------------------------------------------------------------- fitting

double distanceToSegment(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const double len2 = ab.dot(ab);
    if (len2 <= 0)
        return (p - a).length();
    const double t = std::clamp((p - a).dot(ab) / len2, 0.0, 1.0);
    return (p - (a + ab * t)).length();
}

void rdp(const std::vector<Vec2>& pts, std::size_t first, std::size_t last, double tol, std::vector<std::size_t>& keep)
{
    if (last <= first + 1)
        return;
    double worst = -1;
    std::size_t at = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double d = distanceToSegment(pts[i], pts[first], pts[last]);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (worst > tol) {
        rdp(pts, first, at, tol, keep);
        keep.push_back(at);
        rdp(pts, at, last, tol, keep);
    }
}

// RDP on a closed loop given as pts[0..n] with pts[n] == pts[0]: split at
// the point farthest from the first.
std::vector<std::size_t> rdpClosed(const std::vector<Vec2>& pts, double tol)
{
    const std::size_t n = pts.size() - 1;
    std::size_t far = 1;
    double farD = -1;
    for (std::size_t i = 1; i < n; ++i) {
        const double d = (pts[i] - pts[0]).length();
        if (d > farD) {
            farD = d;
            far = i;
        }
    }
    std::vector<std::size_t> keep{0, far, n};
    rdp(pts, 0, far, tol, keep);
    rdp(pts, far, n, tol, keep);
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    return keep;
}

double wrapAngle(double a)
{
    while (a < 0)
        a += 2 * kPi;
    while (a >= 2 * kPi)
        a -= 2 * kPi;
    return a;
}

// The signed sweep from a to b about c, passing m.
double signedSweep(Vec2 c, Vec2 a, Vec2 m, Vec2 b)
{
    const double a0 = std::atan2(a.y - c.y, a.x - c.x);
    const double am = std::atan2(m.y - c.y, m.x - c.x);
    const double a1 = std::atan2(b.y - c.y, b.x - c.x);
    const double toEnd = wrapAngle(a1 - a0), toMid = wrapAngle(am - a0);
    return toMid <= toEnd ? toEnd : toEnd - 2 * kPi;
}

bool monotone(Vec2 c, const std::vector<Vec2>& pts, std::size_t from, std::size_t to)
{
    double total = 0, back = 0;
    double prev = std::atan2(pts[from].y - c.y, pts[from].x - c.x);
    for (std::size_t i = from + 1; i <= to; ++i) {
        const double a = std::atan2(pts[i].y - c.y, pts[i].x - c.x);
        double d = a - prev;
        while (d > kPi)
            d -= 2 * kPi;
        while (d < -kPi)
            d += 2 * kPi;
        total += d;
        back += std::abs(d);
        prev = a;
    }
    return back > 0 && std::abs(total) >= 0.9 * back;
}

struct Piece {
    bool arc = false;
    std::size_t from = 0, to = 0; // samples [from, to] of the rotated loop (to <= n)
    Vec2 p, d;                    // a line: a point and its direction
    Circle c;                     // an arc
};

// The samples of [from, to] not within `trim` of either end (all of them
// when too few would stay).
std::vector<Vec2> inner(const std::vector<Vec2>& pts, std::size_t from, std::size_t to, double trim)
{
    std::vector<Vec2> out;
    for (std::size_t i = from; i <= to; ++i)
        if ((pts[i] - pts[from]).length() > trim && (pts[i] - pts[to]).length() > trim)
            out.push_back(pts[i]);
    if (out.size() < 4) {
        out.assign(pts.begin() + std::ptrdiff_t(from), pts.begin() + std::ptrdiff_t(to) + 1);
    }
    return out;
}

void fitLine(const std::vector<Vec2>& pts, Vec2& point, Vec2& dir)
{
    Vec2 m;
    for (const Vec2& p : pts)
        m = m + p;
    m = m * (1.0 / double(std::max<std::size_t>(1, pts.size())));
    double sxx = 0, syy = 0, sxy = 0;
    for (const Vec2& p : pts) {
        sxx += (p.x - m.x) * (p.x - m.x);
        syy += (p.y - m.y) * (p.y - m.y);
        sxy += (p.x - m.x) * (p.y - m.y);
    }
    const double angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
    point = m;
    dir = {std::cos(angle), std::sin(angle)};
    // Along the samples' order.
    if (pts.size() >= 2 && (pts.back() - pts.front()).dot(dir) < 0)
        dir = dir * -1.0;
}

// The farthest sample of [from, to] from a line or a circle, leaving out
// those within `trim` of the ends (a corner's blur).
double lineResidual(const std::vector<Vec2>& pts, std::size_t from, std::size_t to, Vec2 p, Vec2 d, double trim = 0)
{
    double worst = 0;
    for (std::size_t i = from; i <= to; ++i)
        if ((pts[i] - pts[from]).length() >= trim && (pts[i] - pts[to]).length() >= trim)
            worst = std::max(worst, std::abs(cross(d, pts[i] - p)));
    return worst;
}

double circleResidual(const std::vector<Vec2>& pts, std::size_t from, std::size_t to, const Circle& c, double trim = 0)
{
    double worst = 0;
    for (std::size_t i = from; i <= to; ++i)
        if ((pts[i] - pts[from]).length() >= trim && (pts[i] - pts[to]).length() >= trim)
            worst = std::max(worst, std::abs((pts[i] - c.center).length() - c.radius));
    return worst;
}

bool refit(Piece& piece, const std::vector<Vec2>& pts, double trim)
{
    const std::vector<Vec2> s = inner(pts, piece.from, piece.to, trim);
    if (piece.arc) {
        const auto c = fitCircle(s);
        if (!c)
            return false;
        piece.c = *c;
        return true;
    }
    fitLine(s, piece.p, piece.d);
    return true;
}

// Where two neighbouring pieces meet, near `near`.
Vec2 junction(const Piece& a, const Piece& b, Vec2 near, double tol)
{
    auto lineLine = [&](const Piece& l, const Piece& m) -> Vec2 {
        const double den = cross(l.d, m.d);
        if (std::abs(den) < 0.03) { // nearly parallel: half way between them
            const Vec2 pl = l.p + l.d * (near - l.p).dot(l.d);
            const Vec2 pm = m.p + m.d * (near - m.p).dot(m.d);
            return (pl + pm) * 0.5;
        }
        const double t = cross(m.p - l.p, m.d) / den;
        const Vec2 x = l.p + l.d * t;
        return (x - near).length() < 40 * tol ? x : near;
    };
    auto lineCircle = [&](const Piece& l, const Circle& c) -> Vec2 {
        const Vec2 foot = l.p + l.d * (c.center - l.p).dot(l.d);
        const double dist = (foot - c.center).length();
        Vec2 at = foot;
        if (std::abs(dist - c.radius) > tol && dist < c.radius) {
            // Crossing, not touching: the crossing nearer the joint.
            const double half = std::sqrt(c.radius * c.radius - dist * dist);
            const Vec2 x1 = foot + l.d * half, x2 = foot - l.d * half;
            at = (x1 - near).length() < (x2 - near).length() ? x1 : x2;
        }
        // On the circle (an arc's ends lie on it exactly).
        const Vec2 r = at - c.center;
        return r.length() > 0 ? c.center + r * (c.radius / r.length()) : at;
    };
    if (!a.arc && !b.arc)
        return lineLine(a, b);
    if (!a.arc)
        return lineCircle(a, b.c);
    if (!b.arc)
        return lineCircle(b, a.c);
    // Two arcs: where the circles cross nearer the joint, else between them.
    const Vec2 d = b.c.center - a.c.center;
    const double dist = d.length();
    if (dist > 1e-9 && dist < a.c.radius + b.c.radius && dist > std::abs(a.c.radius - b.c.radius)) {
        const double along = (a.c.radius * a.c.radius - b.c.radius * b.c.radius + dist * dist) / (2 * dist);
        const double half = std::sqrt(std::max(0.0, a.c.radius * a.c.radius - along * along));
        const Vec2 u = d * (1.0 / dist), base = a.c.center + u * along;
        const Vec2 x1 = base + Vec2{-u.y, u.x} * half, x2 = base - Vec2{-u.y, u.x} * half;
        return (x1 - near).length() < (x2 - near).length() ? x1 : x2;
    }
    return near;
}

// Lines and arcs for the loop pts[0..n] (pts[n] == pts[0]).
std::vector<Piece> fitPieces(const std::vector<Vec2>& pts, const LoopFitOptions& o, double size)
{
    const double tol = o.tolerance;
    const std::vector<std::size_t> v = rdpClosed(pts, tol);
    std::vector<Piece> pieces;
    std::size_t i = 0;
    while (i + 1 < v.size()) {
        std::size_t best = i + 1;
        std::optional<Circle> bestCircle;
        for (std::size_t j = i + 2; j < v.size(); ++j) {
            const std::size_t from = v[i], to = v[j];
            const auto c = fitCircle(std::vector<Vec2>(pts.begin() + std::ptrdiff_t(from), pts.begin() + std::ptrdiff_t(to) + 1));
            if (!c || c->radius > 4 * size)
                break;
            const double res = circleResidual(pts, from, to, *c);
            if (res > tol)
                break;
            const double sweep = std::abs(signedSweep(c->center, pts[from], pts[(from + to) / 2], pts[to]));
            if (sweep < 10 * kPi / 180 || sweep > 300 * kPi / 180 || !monotone(c->center, pts, from, to))
                break;
            // Two RDP pieces may be a blunt corner: an arc must fit them well.
            if (j == i + 2 && res > 0.5 * tol)
                continue;
            best = j;
            bestCircle = c;
        }
        Piece p;
        p.arc = bestCircle.has_value();
        p.from = v[i];
        p.to = v[best];
        if (bestCircle)
            p.c = *bestCircle;
        pieces.push_back(p);
        i = best;
    }
    for (auto& p : pieces)
        refit(p, pts, o.trim);

    for (int iter = 0; iter < 6; ++iter) {
        bool changed = false;
        // Tight arcs between pieces are corners.
        for (std::size_t k = 0; k < pieces.size() && pieces.size() > 3; ++k)
            if (pieces[k].arc && pieces[k].c.radius < o.minArcRadius) {
                const std::size_t n = pieces.size();
                // Its samples go to its neighbours (half each).
                const std::size_t mid = (pieces[k].from + pieces[k].to) / 2;
                if (k > 0)
                    pieces[k - 1].to = mid;
                if (k + 1 < n)
                    pieces[k + 1].from = mid;
                if (k == 0)
                    pieces[k + 1].from = pieces[k].from;
                if (k + 1 == n)
                    pieces[k - 1].to = pieces[k].to;
                pieces.erase(pieces.begin() + std::ptrdiff_t(k));
                changed = true;
                break;
            }
        // Neighbours of one kind that one piece fits.
        for (std::size_t k = 0; k + 1 < pieces.size(); ++k) {
            Piece& a = pieces[k];
            Piece& b = pieces[k + 1];
            if (a.arc != b.arc)
                continue;
            Piece merged = a;
            merged.to = b.to;
            if (!refit(merged, pts, o.trim))
                continue;
            const double res = merged.arc ? circleResidual(pts, merged.from, merged.to, merged.c, o.trim)
                                          : lineResidual(pts, merged.from, merged.to, merged.p, merged.d, o.trim);
            const bool sweepOk = !merged.arc
                              || std::abs(signedSweep(merged.c.center, pts[merged.from], pts[(merged.from + merged.to) / 2], pts[merged.to]))
                                     < 300 * kPi / 180;
            if (res <= tol && sweepOk) {
                a = merged;
                pieces.erase(pieces.begin() + std::ptrdiff_t(k) + 1);
                changed = true;
                break;
            }
        }
        // Pieces shorter than minPiece (a corner's blur cut off): their
        // neighbours meet.
        if (pieces.size() > 3) {
            const std::size_t n = pieces.size();
            for (std::size_t k = 0; k < n; ++k) {
                const Vec2 s = junction(pieces[(k + n - 1) % n], pieces[k], pts[pieces[k].from], tol);
                const Vec2 e = junction(pieces[k], pieces[(k + 1) % n], pts[pieces[k].to], tol);
                if ((e - s).length() < std::max(2 * tol, o.minPiece) && (pieces[k].to - pieces[k].from) < pts.size() / 4) {
                    const std::size_t mid = (pieces[k].from + pieces[k].to) / 2;
                    if (k > 0)
                        pieces[k - 1].to = mid;
                    if (k + 1 < n)
                        pieces[k + 1].from = mid;
                    if (k == 0)
                        pieces[k + 1].from = pieces[k].from;
                    if (k + 1 == n)
                        pieces[k - 1].to = pieces[k].to;
                    pieces.erase(pieces.begin() + std::ptrdiff_t(k));
                    changed = true;
                    break;
                }
            }
        }
        if (changed)
            for (auto& p : pieces)
                refit(p, pts, o.trim);
        else
            break;
    }
    return pieces;
}

} // namespace

RgbImage rgbFromRgba(const std::uint8_t* data, int width, int height, int stride, bool bgra, std::vector<std::uint8_t>* valid)
{
    RgbImage img;
    if (!data || width <= 0 || height <= 0)
        return img;
    img.width = width;
    img.height = height;
    img.rgb.resize(std::size_t(width) * height * 3);
    if (valid)
        valid->assign(std::size_t(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = data + std::ptrdiff_t(y) * stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px = row + 4 * x;
            const std::size_t i = std::size_t(y) * width + x;
            img.rgb[3 * i] = bgra ? px[2] : px[0];
            img.rgb[3 * i + 1] = px[1];
            img.rgb[3 * i + 2] = bgra ? px[0] : px[2];
            if (valid)
                (*valid)[i] = px[3] >= 250 ? 1 : 0;
        }
    }
    return img;
}

OutlineLoop fitClosedEdge(const std::vector<Vec2>& edgeIn, const LoopFitOptions& o)
{
    OutlineLoop loop;
    loop.edge = edgeIn;
    std::vector<Vec2> edge = edgeIn;
    while (edge.size() > 3 && (edge.back() - edge.front()).length() < 1e-9)
        edge.pop_back();
    if (edge.size() < 6)
        return loop;
    Vec2 lo = edge.front(), hi = edge.front();
    for (const Vec2& p : edge) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    const double size = std::max(hi.x - lo.x, hi.y - lo.y);
    loop.areaMm2 = std::abs(shoelace(edge));
    const double tol = o.tolerance;

    // One circle?
    if (const auto c = fitCircle(edge)) {
        double worst = 0, sum2 = 0;
        for (const Vec2& p : edge) {
            const double r = std::abs((p - c->center).length() - c->radius);
            worst = std::max(worst, r);
            sum2 += r * r;
        }
        if (worst <= 1.5 * tol && std::sqrt(sum2 / double(edge.size())) <= 0.5 * tol && c->radius > 2 * tol) {
            loop.circle = true;
            loop.circleShape = *c;
            loop.maxErrorMm = worst;
            return loop;
        }
    }

    const std::size_t n = edge.size();
    auto rotated = [&](std::size_t start) {
        std::vector<Vec2> pts(n + 1);
        for (std::size_t i = 0; i <= n; ++i)
            pts[i] = edge[(start + i) % n];
        return pts;
    };
    // Start at the sharpest turn RDP finds, fit, then start again at a
    // real joint (a corner, or where a line meets an arc) so no piece is
    // split where the loop closes.
    std::size_t start = 0;
    {
        const auto pts = rotated(0);
        const auto v = rdpClosed(pts, tol);
        double sharpest = -1;
        for (std::size_t k = 0; k + 1 < v.size(); ++k) {
            const Vec2 a = pts[v[(k + v.size() - 2) % (v.size() - 1)]], b = pts[v[k]], c = pts[v[k + 1]];
            const double turn = std::abs(std::atan2(cross(b - a, c - b), (b - a).dot(c - b)));
            if (turn > sharpest) {
                sharpest = turn;
                start = v[k] % n;
            }
        }
    }
    std::vector<Vec2> pts = rotated(start);
    std::vector<Piece> pieces = fitPieces(pts, o, size);
    for (std::size_t k = 0; k < pieces.size() && pieces.size() > 1; ++k) {
        const Piece& a = pieces[(k + pieces.size() - 1) % pieces.size()];
        const Piece& b = pieces[k];
        const bool joint = a.arc != b.arc || (!a.arc && !b.arc && std::abs(cross(a.d, b.d)) > std::sin(15 * kPi / 180));
        if (joint) {
            if (b.from != 0) {
                start = (start + b.from) % n;
                pts = rotated(start);
                pieces = fitPieces(pts, o, size);
            }
            break;
        }
    }
    if (pieces.size() < 2) {
        // Fall back to the RDP polygon.
        pieces.clear();
        const auto v = rdpClosed(pts, tol);
        for (std::size_t k = 0; k + 1 < v.size(); ++k) {
            Piece p;
            p.from = v[k];
            p.to = v[k + 1];
            refit(p, pts, o.trim);
            pieces.push_back(p);
        }
        if (pieces.size() < 2)
            return loop;
    }
    const std::size_t m = pieces.size();
    std::vector<Vec2> joints(m);
    for (std::size_t k = 0; k < m; ++k)
        joints[k] = junction(pieces[(k + m - 1) % m], pieces[k], pts[pieces[k].from], tol);
    for (std::size_t k = 0; k < m; ++k) {
        const Piece& p = pieces[k];
        FittedSegment seg;
        seg.start = joints[k];
        seg.end = joints[(k + 1) % m];
        if (p.arc) {
            seg.arc = true;
            seg.center = p.c.center;
            seg.radius = p.c.radius;
            const double sweep = signedSweep(p.c.center, seg.start, pts[(p.from + p.to) / 2], seg.end);
            const double a0 = std::atan2(seg.start.y - p.c.center.y, seg.start.x - p.c.center.x) + sweep / 2;
            seg.through = p.c.center + Vec2{std::cos(a0), std::sin(a0)} * p.c.radius;
        } else {
            seg.through = (seg.start + seg.end) * 0.5;
        }
        loop.segments.push_back(seg);
    }
    // The fitting error: the farthest edge point from the pieces (nearest
    // piece each), leaving out the blur within `trim` of a corner.
    auto distanceTo = [](Vec2 q, const FittedSegment& s) {
        if (!s.arc)
            return distanceToSegment(q, s.start, s.end);
        const double sweep = signedSweep(s.center, s.start, s.through, s.end);
        const double a0 = std::atan2(s.start.y - s.center.y, s.start.x - s.center.x);
        double along = std::atan2(q.y - s.center.y, q.x - s.center.x) - a0;
        if (sweep < 0)
            along = -along;
        if (wrapAngle(along) <= std::abs(sweep))
            return std::abs((q - s.center).length() - s.radius);
        return std::min((q - s.start).length(), (q - s.end).length());
    };
    double worst = 0;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        bool nearCorner = false;
        for (const Vec2& j : joints)
            nearCorner = nearCorner || (pts[i] - j).length() < o.trim;
        if (nearCorner)
            continue;
        double nearest = 1e18;
        for (const auto& seg : loop.segments)
            nearest = std::min(nearest, distanceTo(pts[i], seg));
        worst = std::max(worst, nearest);
    }
    loop.maxErrorMm = worst;
    return loop;
}

ObjectOutline findObjectOutline(const RgbImage& image, const std::vector<std::uint8_t>& validIn, const ObjectOutlineOptions& opt)
{
    ObjectOutline result;
    const int w = image.width, h = image.height;
    const std::size_t count = std::size_t(std::max(0, w)) * std::size_t(std::max(0, h));
    if (w < 16 || h < 16 || image.rgb.size() < 3 * count || !(opt.mmPerPixel > 0)) {
        result.message = "The straightened photo can't be read.";
        return result;
    }
    const double mmpp = opt.mmPerPixel;
    const double tolPx = std::max(opt.toleranceMm / mmpp, 0.6);

    // Where to look: the photo (not the empty corners of the straightened
    // view), inside the search area, away from the card.
    std::vector<std::uint8_t> valid(count, 1), usable(count, 0), ring(count, 0);
    const double margin = opt.excludeMarginMm / mmpp, inset = opt.searchInsetMm / mmpp;
    const double ringNear = margin + 1.0 / mmpp, ringFar = margin + 10.0 / mmpp;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * w + x;
            const Vec2 p{x + 0.5, y + 0.5};
            bool ok = validIn.size() != count || validIn[i];
            if (ok && opt.searchArea && insideDistance(p, *opt.searchArea) < inset)
                ok = false;
            valid[i] = ok ? 1 : 0;
            bool excluded = false, near = false;
            for (const auto& q : opt.exclude) {
                const double d = insideDistance(p, q);
                if (d >= -margin)
                    excluded = true;
                else if (d >= -ringFar && d <= -ringNear)
                    near = true;
            }
            usable[i] = ok && !excluded ? 1 : 0;
            ring[i] = usable[i] && near ? 1 : 0;
        }
    std::size_t usableCount = 0;
    for (std::uint8_t u : usable)
        usableCount += u;
    if (usableCount < 1000) {
        result.message = "There's too little of the photo beside the card to look at.";
        return result;
    }

    // The paper's colour: what the card lies on first, then the photo's
    // most common colours.
    std::vector<Rgb> candidates;
    {
        const int step = std::max(1, int(std::sqrt(double(usableCount) / 200000.0)));
        std::vector<std::size_t> around, all;
        for (int y = 0; y < h; y += step)
            for (int x = 0; x < w; x += step) {
                const std::size_t i = std::size_t(y) * w + x;
                if (ring[i])
                    around.push_back(i);
                if (usable[i])
                    all.push_back(i);
            }
        if (around.size() >= 200)
            candidates = commonColours(image, around, 1, {}, 0);
        const auto more = commonColours(image, all, 3 - int(candidates.size()), candidates, 60);
        candidates.insert(candidates.end(), more.begin(), more.end());
    }

    std::string why = "Nothing found on the paper: lay the part on plain paper of a contrasting colour, with the card beside it.";
    for (const Rgb& guess : candidates) {
        const PaperModel paper = fitPaper(image, usable, guess);
        std::vector<float> diff(count, 0.f);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const std::size_t i = std::size_t(y) * w + x;
                if (usable[i])
                    diff[i] = float(difference(pixel(image, i), paper.at((x + 0.5) / w, (y + 0.5) / h)));
            }
        smooth121(diff, w, h);
        // The part: the pixels unlike the paper by more than `limit`, specks
        // out and small gaps closed, the largest piece that doesn't run off
        // (or the tapped one). Its label in `labels` (0: none, `whyNot` says why).
        const double pxArea = mmpp * mmpp;
        auto pick = [&](double limit, std::vector<int>& labels, std::string& whyNot) -> int {
            const double threshold = limit;
            std::vector<std::uint8_t> mask(count, 0);
            for (std::size_t i = 0; i < count; ++i)
                mask[i] = usable[i] && diff[i] > threshold ? 1 : 0;
            // Specks out, small gaps closed.
            squareFilter(mask, w, h, 1, false);
            squareFilter(mask, w, h, 1, true);
            const int closeR = std::clamp(int(std::lround(0.25 / mmpp)), 1, 4);
            squareFilter(mask, w, h, closeR, true);
            squareFilter(mask, w, h, closeR, false);
            for (std::size_t i = 0; i < count; ++i)
                mask[i] = mask[i] && usable[i];

            const int pieces = labelPieces(mask, w, h, true, labels);
            std::vector<std::size_t> area(std::size_t(pieces) + 1, 0);
            std::vector<std::uint8_t> touches(std::size_t(pieces) + 1, 0);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const int l = labels[std::size_t(y) * w + x];
                    if (!l)
                        continue;
                    ++area[std::size_t(l)];
                    const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1 || !valid[std::size_t(y) * w + x - 1]
                                   || !valid[std::size_t(y) * w + x + 1] || !valid[std::size_t(y - 1) * w + x]
                                   || !valid[std::size_t(y + 1) * w + x];
                    if (edge)
                        touches[std::size_t(l)] = 1;
                }
            int target = 0;
            if (opt.seed) {
                const int sx = int(std::floor(opt.seed->x)), sy = int(std::floor(opt.seed->y));
                const int reach = int(std::ceil(5.0 / mmpp));
                double best = 1e18;
                for (int y = std::max(0, sy - reach); y <= std::min(h - 1, sy + reach); ++y)
                    for (int x = std::max(0, sx - reach); x <= std::min(w - 1, sx + reach); ++x) {
                        const int l = labels[std::size_t(y) * w + x];
                        const double d = double(x - sx) * (x - sx) + double(y - sy) * (y - sy);
                        if (l && d < best) {
                            best = d;
                            target = l;
                        }
                    }
                if (!target)
                    whyNot = "No part where you tapped: tap on the part itself.";
            } else {
                bool runsOff = false;
                for (int l = 1; l <= pieces; ++l) {
                    const double mm2 = double(area[std::size_t(l)]) * pxArea;
                    if (mm2 < opt.minObjectMm2)
                        continue;
                    if (touches[std::size_t(l)]) {
                        runsOff = runsOff || double(area[std::size_t(l)]) < 0.5 * double(usableCount);
                        continue;
                    }
                    if (double(area[std::size_t(l)]) > 0.7 * double(usableCount))
                        continue;
                    if (!target || area[std::size_t(l)] > area[std::size_t(target)])
                        target = l;
                }
                if (!target && runsOff)
                    whyNot = "The part runs off the photo or the paper: leave a margin all around it.";
            }
            return target;
        };
        // Otsu's threshold first; then at least half way to the part's own
        // difference (a large sheet of paper with a soft shadow pulls Otsu's
        // threshold down into the shadow, which would join the part).
        double threshold = std::max(0.18, otsu(diff, usable, 2.0));
        std::vector<int> labels;
        int target = pick(threshold, labels, why);
        if (!target)
            continue;
        {
            std::vector<float> inPart;
            for (std::size_t i = 0; i < count; ++i)
                if (labels[i] == target)
                    inPart.push_back(diff[i]);
            std::nth_element(inPart.begin(), inPart.begin() + std::ptrdiff_t(inPart.size() / 2), inPart.end());
            const double halfWay = 0.5 * double(inPart[inPart.size() / 2]);
            std::vector<int> again;
            std::string whyAgain;
            if (halfWay > threshold + 0.02) {
                if (const int t = pick(halfWay, again, whyAgain)) {
                    labels.swap(again);
                    target = t;
                    threshold = halfWay;
                }
            }
        }

        // The part, its small holes filled.
        std::vector<std::uint8_t> part(count, 0);
        int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (labels[std::size_t(y) * w + x] == target) {
                    part[std::size_t(y) * w + x] = 1;
                    bx0 = std::min(bx0, x);
                    by0 = std::min(by0, y);
                    bx1 = std::max(bx1, x);
                    by1 = std::max(by1, y);
                }
        // The window around it (2 px of paper all round, and room to refine).
        const int pad = int(std::ceil(std::max(4.0, 2.5 / mmpp))) + 6;
        const int ox = std::max(0, bx0 - pad), oy = std::max(0, by0 - pad);
        const int ww = std::min(w - 1, bx1 + pad) - ox + 1, wh = std::min(h - 1, by1 + pad) - oy + 1;
        std::vector<std::uint8_t> local(std::size_t(ww) * wh, 0), back(std::size_t(ww) * wh, 0);
        for (int y = 0; y < wh; ++y)
            for (int x = 0; x < ww; ++x) {
                local[std::size_t(y) * ww + x] = part[std::size_t(y + oy) * w + x + ox];
                back[std::size_t(y) * ww + x] = local[std::size_t(y) * ww + x] ? 0 : 1;
            }
        std::vector<int> holeLabels;
        const int holes = labelPieces(back, ww, wh, false, holeLabels);
        std::vector<std::size_t> holeArea(std::size_t(holes) + 1, 0);
        std::vector<std::uint8_t> outside(std::size_t(holes) + 1, 0);
        for (int y = 0; y < wh; ++y)
            for (int x = 0; x < ww; ++x) {
                const int l = holeLabels[std::size_t(y) * ww + x];
                if (!l)
                    continue;
                ++holeArea[std::size_t(l)];
                if (x == 0 || y == 0 || x == ww - 1 || y == wh - 1)
                    outside[std::size_t(l)] = 1;
            }
        for (std::size_t i = 0; i < local.size(); ++i) {
            const int l = holeLabels[i];
            if (l && !outside[std::size_t(l)] && double(holeArea[std::size_t(l)]) * pxArea < opt.minHoleMm2)
                local[i] = 1;
        }

        // The part's colour: the median inside it, away from its edge.
        std::vector<std::uint8_t> core = local;
        squareFilter(core, ww, wh, 2, false);
        std::array<std::vector<std::uint8_t>, 3> channels;
        for (int y = 0; y < wh; ++y)
            for (int x = 0; x < ww; ++x)
                if (core[std::size_t(y) * ww + x]) {
                    const std::size_t i = std::size_t(y + oy) * w + x + ox;
                    for (std::size_t k = 0; k < 3; ++k)
                        channels[k].push_back(image.rgb[3 * i + k]);
                }
        if (channels[0].size() < 20)
            for (int y = 0; y < wh; ++y)
                for (int x = 0; x < ww; ++x)
                    if (local[std::size_t(y) * ww + x]) {
                        const std::size_t i = std::size_t(y + oy) * w + x + ox;
                        for (std::size_t k = 0; k < 3; ++k)
                            channels[k].push_back(image.rgb[3 * i + k]);
                    }
        Rgb object{0, 0, 0};
        for (std::size_t k = 0; k < 3; ++k) {
            auto& c = channels[k];
            std::nth_element(c.begin(), c.begin() + std::ptrdiff_t(c.size() / 2), c.end());
            object[k] = c.empty() ? 0.0 : double(c[c.size() / 2]);
        }
        const Rgb paperHere = paper.at((0.5 * (bx0 + bx1) + 0.5) / w, (0.5 * (by0 + by1) + 0.5) / h);
        const Rgb towards{object[0] - paperHere[0], object[1] - paperHere[1], object[2] - paperHere[2]};
        const double t2 = towards[0] * towards[0] + towards[1] * towards[1] + towards[2] * towards[2];
        if (t2 < 20 * 20) {
            why = "The part and the paper are too alike: use paper of a contrasting colour.";
            continue;
        }

        // The part's share of each pixel's colour (0 paper, 1 part), smoothed:
        // the edge is where it is one half.
        Field share;
        share.ox = ox;
        share.oy = oy;
        share.w = ww;
        share.h = wh;
        share.v.assign(std::size_t(ww) * wh, 0.f);
        for (int y = 0; y < wh; ++y)
            for (int x = 0; x < ww; ++x) {
                const std::size_t i = std::size_t(y + oy) * w + x + ox;
                const Rgb c = pixel(image, i);
                const Rgb p = paper.at((x + ox + 0.5) / w, (y + oy + 0.5) / h);
                share.v[std::size_t(y) * ww + x]
                    = float(((c[0] - p[0]) * towards[0] + (c[1] - p[1]) * towards[1] + (c[2] - p[2]) * towards[2]) / t2);
            }
        smooth121(share.v, ww, wh);

        LoopFitOptions fit;
        fit.tolerance = tolPx;
        fit.minArcRadius = std::max(3 * tolPx, 1.5 / mmpp);
        fit.trim = std::max(3.0, 2 * tolPx);
        fit.minPiece = std::max(2 * tolPx, 1.5 / mmpp);
        std::vector<OutlineLoop> loops;
        for (const auto& crack : crackLoops(local, ww, wh, ox, oy)) {
            const double signedArea = shoelace(crack);
            const std::vector<Vec2> edge = refineEdge(crack, share, std::max(4.0, 2.5 / mmpp));
            if (edge.size() < 6)
                continue;
            OutlineLoop loop = fitClosedEdge(edge, fit);
            loop.hole = signedArea < 0;
            loop.maxErrorMm *= mmpp;
            loop.areaMm2 = std::abs(shoelace(edge)) * pxArea;
            if (loop.circle || loop.segments.size() >= 2)
                loops.push_back(std::move(loop));
        }
        std::sort(loops.begin(), loops.end(), [](const OutlineLoop& a, const OutlineLoop& b) {
            return a.hole != b.hole ? !a.hole : a.areaMm2 > b.areaMm2;
        });
        if (loops.empty() || loops.front().hole) {
            why = "The part's edge couldn't be traced.";
            continue;
        }
        // One outer edge (the biggest); holes inside it.
        loops.erase(std::remove_if(loops.begin() + 1, loops.end(), [](const OutlineLoop& l) { return !l.hole; }), loops.end());
        result.found = true;
        result.loops = std::move(loops);
        result.paper = paperHere;
        result.object = object;
        result.threshold = threshold;
        result.areaMm2 = result.loops.front().areaMm2;
        for (std::size_t k = 1; k < result.loops.size(); ++k)
            result.areaMm2 -= result.loops[k].areaMm2;
        return result;
    }
    result.message = why;
    return result;
}

} // namespace os::measure
