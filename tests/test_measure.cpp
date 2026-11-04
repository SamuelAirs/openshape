// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The measuring tools' math (src/measure, docs/MEASURE.md): the screen's
// true scale, a photo straightened onto a known rectangle, a traced stroke
// fitted to lines, arcs and circles, the fold's angle and caliper, the level,
// outlines, and the list of measurements.

#include "measure/Fold.h"
#include "measure/Homography.h"
#include "measure/Level.h"
#include "measure/Measurement.h"
#include "measure/Outline.h"
#include "measure/ScreenScale.h"
#include "measure/StrokeFit.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace os;
using namespace os::measure;

namespace {

constexpr double kDeg = kPi / 180.0;

// A pinhole camera looking at the ground plane (z = 0, mm) from `eye`
// toward `target`: the image of a ground point in pixels.
struct TestCamera {
    Vec3 eye{0, -300, 400};
    Vec3 target{40, 30, 0};
    double focalPx = 3000;
    Vec2 principal{2016, 1512};

    Vec2 project(Vec2 ground) const
    {
        const Vec3 f = (target - eye).normalized();
        const Vec3 r = f.cross(Vec3{0, 0, 1}).normalized();
        const Vec3 u = r.cross(f);
        const Vec3 d = Vec3{ground.x, ground.y, 0} - eye;
        const double z = d.dot(f);
        return {principal.x + focalPx * d.dot(r) / z, principal.y - focalPx * d.dot(u) / z};
    }
};

} // namespace

// ---------------------------------------------------------------- screen scale

TEST(ScreenScale, KnownDevices)
{
    const ScaleEstimate phone = estimateScale({402, 874, 3.0, DeviceKind::Phone});
    EXPECT_EQ(phone.source, ScaleEstimate::Source::DeviceTable);
    EXPECT_NEAR(phone.pointsPerMm * 25.4, 460.0 / 3.0, 1e-9); // iPhone 16 Pro

    const ScaleEstimate inner = estimateScale({951, 669, 3.0, DeviceKind::Phone});
    EXPECT_NE(inner.note.find("inner"), std::string::npos);
    EXPECT_NEAR(inner.pointsPerMm, 458.0 / 3.0 / 25.4, 1e-9);
    const ScaleEstimate outer = estimateScale({678, 466, 3.0, DeviceKind::Phone}); // turned
    EXPECT_NE(outer.note.find("outer"), std::string::npos);

    EXPECT_NEAR(estimateScale({820, 1180, 2.0, DeviceKind::Tablet}).pointsPerMm * 25.4, 132.0, 1e-9); // iPad Air
    EXPECT_NEAR(estimateScale({744, 1133, 2.0, DeviceKind::Tablet}).pointsPerMm * 25.4, 163.0, 1e-9); // iPad mini
}

TEST(ScreenScale, DesktopUsesTheMonitorsSizeWhenBelievable)
{
    // A 27-inch 2560 x 1440 monitor at 150 %: 1707 x 960 logical px, 597 x 336 mm.
    const ScaleEstimate s = estimateScale({1707, 960, 1.5, DeviceKind::Desktop, 597, 336});
    EXPECT_EQ(s.source, ScaleEstimate::Source::System);
    EXPECT_NEAR(s.pointsPerMm, 1707.0 / 597.0, 1e-9);
    // A projector reporting 0 x 0, or nonsense: the 96 dpi guess.
    EXPECT_EQ(estimateScale({1920, 1080, 1.0, DeviceKind::Desktop, 0, 0}).source, ScaleEstimate::Source::Fallback);
    EXPECT_EQ(estimateScale({1920, 1080, 1.0, DeviceKind::Desktop, 16, 9}).source, ScaleEstimate::Source::Fallback);
}

TEST(ScreenScale, KeysAndCalibration)
{
    EXPECT_EQ(screenKey({951, 669, 3.0, DeviceKind::Phone}), "951x669@3");
    EXPECT_EQ(screenKey({669, 951, 3.0, DeviceKind::Phone}), "951x669@3"); // turned: same screen
    EXPECT_EQ(screenKey({1707.4, 960, 1.5, DeviceKind::Desktop}), "1707x960@1.5");
    // A card's long side matched 515 points.
    const double ppm = pointsPerMmFromSpan(515.0, kBankCard.longMm);
    EXPECT_NEAR(ppm, 515.0 / 85.6, 1e-12);
    EXPECT_TRUE(plausiblePointsPerMm(ppm));
    EXPECT_FALSE(plausiblePointsPerMm(pointsPerMmFromSpan(30, kBankCard.longMm))); // a card 30 points wide
    EXPECT_EQ(pointsPerMmFromSpan(-1, 85.6), 0.0);
    EXPECT_FALSE(plausiblePointsPerMm(0));
}

// ---------------------------------------------------------------- homography

TEST(Homography, MapsTheFourPointsAndInverts)
{
    const std::array<Vec2, 4> from{Vec2{100, 120}, Vec2{900, 80}, Vec2{1000, 700}, Vec2{60, 650}};
    const std::array<Vec2, 4> to{Vec2{0, 0}, Vec2{85.6, 0}, Vec2{85.6, 53.98}, Vec2{0, 53.98}};
    const auto h = Homography::fromQuads(from, to);
    ASSERT_TRUE(h);
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(h->map(from[i]).x, to[i].x, 1e-9);
        EXPECT_NEAR(h->map(from[i]).y, to[i].y, 1e-9);
    }
    const auto inv = h->inverse();
    ASSERT_TRUE(inv);
    const Vec2 back = inv->map(h->map({432, 321}));
    EXPECT_NEAR(back.x, 432, 1e-7);
    EXPECT_NEAR(back.y, 321, 1e-7);
    // Three in a line: no map.
    EXPECT_FALSE(Homography::fromQuads({Vec2{0, 0}, Vec2{1, 1}, Vec2{2, 2}, Vec2{0, 5}}, to));
}

TEST(Homography, CardInATiltedPhotoMeasuresThingsBesideIt)
{
    const TestCamera cam;
    // A card lying on the table at (0,0)..(85.6, 53.98), photographed at a slant.
    const std::array<Vec2, 4> card{cam.project({0, 0}), cam.project({85.6, 0}), cam.project({85.6, 53.98}),
                                   cam.project({0, 53.98})};
    const auto r = PlaneRectifier::create(card, cardReference(kBankCard));
    ASSERT_TRUE(r);
    // A 120 mm pencil lying beside it, diagonally.
    const Vec2 a = cam.project({100, 10}), b = cam.project({100 + 120 * std::cos(0.5), 10 + 120 * std::sin(0.5)});
    EXPECT_NEAR(r->distanceMm(a, b), 120.0, 1e-6);
    // A 50 x 30 mm label's area.
    const std::vector<Vec2> label{cam.project({-70, 0}), cam.project({-20, 0}), cam.project({-20, 30}), cam.project({-70, 30})};
    EXPECT_NEAR(r->areaMm2(label), 1500.0, 1e-5);
    EXPECT_NEAR(r->referenceWidthMm(), 85.6, 1e-12);
    EXPECT_TRUE(r->onPlane(cam.project({500, 500})));
    EXPECT_GT(r->mmPerImagePixelAtReference(), 0.0);
}

TEST(Homography, CornersFromAnyStartAndDirectionGiveTheSameMeasure)
{
    const TestCamera cam;
    std::array<Vec2, 4> card{cam.project({0, 0}), cam.project({85.6, 0}), cam.project({85.6, 53.98}), cam.project({0, 53.98})};
    const Vec2 a = cam.project({10, 70}), b = cam.project({70, 75});
    const double truth = (Vec2{10, 70} - Vec2{70, 75}).length();
    for (int start = 0; start < 4; ++start) {
        for (int dir : {1, -1}) {
            std::array<Vec2, 4> q{};
            for (int i = 0; i < 4; ++i)
                q[i] = card[(start + dir * i + 8) % 4];
            const auto r = PlaneRectifier::create(q, cardReference(kBankCard));
            ASSERT_TRUE(r) << start << " " << dir;
            EXPECT_NEAR(r->distanceMm(a, b), truth, 1e-6) << start << " " << dir;
            // Never mirrored: the plane's corners go round as the photo's do (y down).
            const auto& c = r->orderedCorners();
            double s = 0;
            for (int i = 0; i < 4; ++i)
                s += c[i].x * c[(i + 1) % 4].y - c[(i + 1) % 4].x * c[i].y;
            EXPECT_GT(s, 0) << start << " " << dir;
        }
    }
    // A bow tie (corners out of order) is refused.
    std::swap(card[1], card[2]);
    EXPECT_FALSE(PlaneRectifier::create(card, cardReference(kBankCard)));
}

TEST(Homography, ReferencesAndTheStraightenedView)
{
    // The card is a poker-size playing card unless another is named.
    EXPECT_NEAR(cardReference().widthMm, 88.9, 1e-12);
    EXPECT_NEAR(cardReference().heightMm, 63.5, 1e-12);
    EXPECT_EQ(cardReference().name, "Playing card");
    EXPECT_NEAR(cardReference(kBankCard).widthMm, 85.6, 1e-12);
    EXPECT_EQ(cardReference(kBankCard).name, "Bank card");
    EXPECT_NEAR(a4Reference().widthMm, 297.0, 1e-12);
    EXPECT_NEAR(letterReference().heightMm, 215.9, 1e-12);
    const auto rect = rectangleReference(40, 100);
    ASSERT_TRUE(rect);
    EXPECT_EQ(rect->widthMm, 100);
    EXPECT_EQ(rect->heightMm, 40);
    EXPECT_FALSE(rectangleReference(0, 10));

    const TestCamera cam;
    const std::array<Vec2, 4> card{cam.project({0, 0}), cam.project({85.6, 0}), cam.project({85.6, 53.98}),
                                   cam.project({0, 53.98})};
    const auto r = PlaneRectifier::create(card, cardReference(kBankCard));
    ASSERT_TRUE(r);
    const RectifiedView view = planRectifiedView(*r, 4032, 3024, 2000);
    EXPECT_LE(std::max(view.width, view.height), 2001);
    EXPECT_GE(view.mmPerPixel, r->mmPerImagePixelAtReference() - 1e-12);
    // The card is inside the view.
    EXPECT_LE(view.originMm.x, 0.0);
    EXPECT_LE(view.originMm.y, 0.0);
    EXPECT_GE(view.originMm.x + view.width * view.mmPerPixel, 85.6);
    // The view stops at 1.5 card sizes around the card even where the photo reaches further.
    EXPECT_GE(view.originMm.x, -1.5 * 85.6 - 1e-9);

    // Warping: a photo whose pixels encode their own coordinates; a view
    // pixel samples the photo where its surface point is.
    const int w = 4032, h = 3024;
    std::vector<std::uint8_t> photo(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &photo[(std::size_t(y) * w + x) * 4];
            p[0] = std::uint8_t(x % 256);
            p[1] = std::uint8_t(y % 256);
            p[2] = 0;
            p[3] = 255;
        }
    RectifiedView small = view;
    small.width = 64;
    small.height = 48;
    std::vector<std::uint8_t> out(std::size_t(small.width) * small.height * 4);
    warpToView({w, h, w * 4, photo.data()}, *r, small, out.data(), small.width * 4);
    int opaque = 0;
    for (int y = 0; y < small.height; ++y)
        for (int x = 0; x < small.width; ++x) {
            const std::uint8_t* px = &out[(std::size_t(y) * small.width + x) * 4];
            if (px[3] == 0)
                continue;
            ++opaque;
            const Vec2 img = r->toImage(small.toPlane({x + 0.5, y + 0.5}));
            const int ix = int(std::floor(img.x));
            if (ix % 256 > 2 && ix % 256 < 253) { // away from the wrap of x % 256
                EXPECT_NEAR(px[0], img.x - 0.5 - 256 * std::floor((img.x - 0.5) / 256), 1.01) << x << "," << y;
            }
        }
    EXPECT_GT(opaque, 100);
}

// ---------------------------------------------------------------- stroke fitting

namespace {

std::vector<Vec2> wobble(std::vector<Vec2> pts, double amplitude, unsigned seed = 7)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> d(-amplitude, amplitude);
    for (Vec2& p : pts)
        p = p + Vec2{d(rng), d(rng)};
    return pts;
}

std::vector<Vec2> along(Vec2 a, Vec2 b, int n)
{
    std::vector<Vec2> out;
    for (int i = 0; i < n; ++i)
        out.push_back(a + (b - a) * (double(i) / n));
    return out;
}

} // namespace

TEST(StrokeFit, CircleHelpers)
{
    const auto c = circleThrough({10, 0}, {0, 10}, {-10, 0});
    ASSERT_TRUE(c);
    EXPECT_NEAR(c->center.x, 0, 1e-12);
    EXPECT_NEAR(c->radius, 10, 1e-12);
    EXPECT_FALSE(circleThrough({0, 0}, {1, 1}, {2, 2}));
    std::vector<Vec2> arc;
    for (int i = 0; i <= 40; ++i)
        arc.push_back(Vec2{5, -3} + Vec2{std::cos(i * 2.0 * kDeg), std::sin(i * 2.0 * kDeg)} * 25.0); // an 80 degree arc
    const auto fit = fitCircle(arc);
    ASSERT_TRUE(fit);
    EXPECT_NEAR(fit->center.x, 5, 1e-6);
    EXPECT_NEAR(fit->center.y, -3, 1e-6);
    EXPECT_NEAR(fit->radius, 25, 1e-6);
    EXPECT_EQ(simplifyPolyline(along({0, 0}, {50, 0}, 30), 0.1).size(), 2u);
}

TEST(StrokeFit, ACoinTracedRoundIsOneCircle)
{
    std::vector<Vec2> pts;
    for (int i = 0; i <= 200; ++i) // a bit past full: hands overshoot
        pts.push_back(Vec2{12, 7} + Vec2{std::cos(i * 1.85 * kDeg), std::sin(i * 1.85 * kDeg)} * 11.6);
    const FittedStroke f = fitStroke(wobble(pts, 0.15), {});
    ASSERT_EQ(f.kind, FittedStroke::Kind::Circle);
    EXPECT_TRUE(f.closed);
    EXPECT_NEAR(f.circle.radius, 11.6, 0.1);
    EXPECT_NEAR(f.circle.center.x, 12, 0.1);
    EXPECT_LE(f.maxError, 0.5);
}

TEST(StrokeFit, ARectangleTracedIsFourLinesClosed)
{
    std::vector<Vec2> pts;
    for (auto [a, b] : {std::pair{Vec2{0, 0}, Vec2{60, 0}}, {Vec2{60, 0}, Vec2{60, 30}}, {Vec2{60, 30}, Vec2{0, 30}}, {Vec2{0, 30}, Vec2{0, 0}}}) {
        const auto side = along(a, b, 60);
        pts.insert(pts.end(), side.begin(), side.end());
    }
    pts.push_back({0.6, 0.4}); // ends near the start
    const FittedStroke f = fitStroke(wobble(pts, 0.12), {});
    ASSERT_EQ(f.kind, FittedStroke::Kind::Path);
    EXPECT_TRUE(f.closed);
    ASSERT_EQ(f.segments.size(), 4u);
    for (const FittedSegment& s : f.segments)
        EXPECT_FALSE(s.arc);
    EXPECT_NEAR(f.segments.back().end.x, f.segments.front().start.x, 1e-12);
    EXPECT_LE(f.maxError, 0.5 + 1e-9);
    double perimeter = 0;
    for (const FittedSegment& s : f.segments)
        perimeter += (s.end - s.start).length();
    EXPECT_NEAR(perimeter, 180.0, 2.0);
}

TEST(StrokeFit, ASlotTracedIsLinesAndArcs)
{
    // A 40 x 16 slot: two straight sides and two half-circle ends.
    std::vector<Vec2> pts = along({0, -8}, {40, -8}, 50);
    for (int i = 0; i <= 45; ++i)
        pts.push_back(Vec2{40, 0} + Vec2{std::sin(i * 4.0 * kDeg), -std::cos(i * 4.0 * kDeg)} * 8.0);
    const auto top = along({40, 8}, {0, 8}, 50);
    pts.insert(pts.end(), top.begin(), top.end());
    for (int i = 0; i <= 45; ++i)
        pts.push_back(Vec2{0, 0} + Vec2{-std::sin(i * 4.0 * kDeg), std::cos(i * 4.0 * kDeg)} * 8.0);
    const FittedStroke f = fitStroke(wobble(pts, 0.08), {0.4, 2.0});
    ASSERT_EQ(f.kind, FittedStroke::Kind::Path);
    EXPECT_TRUE(f.closed);
    int arcs = 0, lines = 0;
    for (const FittedSegment& s : f.segments) {
        if (s.arc) {
            ++arcs;
            EXPECT_NEAR(s.radius, 8.0, 0.4);
        } else {
            ++lines;
        }
    }
    EXPECT_EQ(arcs, 2);
    EXPECT_EQ(lines, 2);
    EXPECT_LE(f.maxError, 0.4 + 1e-9);
}

TEST(StrokeFit, AnOpenWiggleAndNothing)
{
    const FittedStroke line = fitStroke(wobble(along({0, 0}, {80, 20}, 100), 0.1), {});
    ASSERT_EQ(line.kind, FittedStroke::Kind::Path);
    EXPECT_FALSE(line.closed);
    ASSERT_EQ(line.segments.size(), 1u);
    EXPECT_FALSE(line.segments[0].arc);
    EXPECT_EQ(fitStroke({}, {}).kind, FittedStroke::Kind::Empty);
    EXPECT_EQ(fitStroke({{1, 1}, {1.1, 1}}, {}).kind, FittedStroke::Kind::Empty); // a dot
}

// ---------------------------------------------------------------- the fold

TEST(Fold, AnglesAndCaliper)
{
    const FoldAngles a = foldAngles(2.0 * kPi / 3.0);
    EXPECT_NEAR(a.angleDeg, 120.0, 1e-9);
    EXPECT_NEAR(a.bendDeg, 60.0, 1e-9);
    EXPECT_EQ(hingeDegrees(-0.1), 0.0);
    EXPECT_NEAR(caliperGapMm(kPi, 79.0), 158.0, 1e-9); // flat: both halves end to end
    EXPECT_NEAR(caliperGapMm(kPi / 3.0, 79.0), 79.0, 1e-9); // 60 degrees: an equilateral triangle
    // Calibrating with a card's long side pinched at 65.6 degrees.
    const auto arm = armFromKnownGap(65.6 * kDeg, kBankCard.longMm);
    ASSERT_TRUE(arm);
    EXPECT_NEAR(caliperGapMm(65.6 * kDeg, *arm), kBankCard.longMm, 1e-9);
    EXPECT_FALSE(armFromKnownGap(1.0 * kDeg, 10.0));
    EXPECT_NEAR(caliperMmPerDegree(0, 79.0), 79.0 * kDeg, 1e-12);
}

TEST(Fold, SteadyReadingHoldsOnceAndRearmsAfterAMove)
{
    SteadyReading s(0.4, 0.8, 0.1);
    double t = 0;
    SteadyReading::State st;
    // Moving: no hold.
    for (int i = 0; i < 20; ++i, t += 0.05)
        st = s.feed(90.0 + i * 2.0, t);
    EXPECT_FALSE(st.steady);
    EXPECT_FALSE(st.held);
    // Resting at 131.2 with tremor: held once.
    int holds = 0;
    for (int i = 0; i < 40; ++i, t += 0.05) {
        st = s.feed(131.2 + ((i % 2) ? 0.05 : -0.05), t);
        holds += st.newHold ? 1 : 0;
    }
    EXPECT_TRUE(st.steady);
    EXPECT_EQ(holds, 1);
    ASSERT_TRUE(st.held);
    EXPECT_NEAR(*st.held, 131.2, 0.2);
    // Lifted (a move) and set down at 45: a new hold.
    for (int i = 0; i < 10; ++i, t += 0.05)
        st = s.feed(131.2 - i * 9.0, t);
    holds = 0;
    for (int i = 0; i < 40; ++i, t += 0.05) {
        st = s.feed(45.0, t);
        holds += st.newHold ? 1 : 0;
    }
    EXPECT_EQ(holds, 1);
    EXPECT_NEAR(*st.held, 45.0, 0.05);
}

TEST(Fold, SteadyReadingSaysHowFarAHoldHasCome)
{
    SteadyReading s(0.4, 0.8, 0.1);
    double t = 0;
    SteadyReading::State st;
    for (int i = 0; i < 20; ++i, t += 0.05)
        st = s.feed(90.0 + i * 2.0, t);
    EXPECT_LT(st.progress, 0.15);
    EXPECT_TRUE(st.armed);
    // Still at 120: the bar fills (while the smoothed value still settles it
    // may step back by a sample, no more) and is full with the hold.
    double last = st.progress;
    bool rose = false;
    for (int i = 0; i < 40 && !st.held; ++i, t += 0.05) {
        st = s.feed(120.0, t);
        EXPECT_GE(st.progress, last - 0.07) << i;
        rose = rose || (st.progress > 0.3 && st.progress < 1.0);
        last = st.progress;
    }
    EXPECT_TRUE(rose);
    ASSERT_TRUE(st.held);
    EXPECT_EQ(st.progress, 1.0);
    EXPECT_FALSE(st.armed);
    // Measure again while it lies still: no hold at once, the whole wait again.
    s.clearHold();
    st = s.feed(120.0, t);
    t += 0.05;
    EXPECT_FALSE(st.held);
    EXPECT_LT(st.progress, 0.2);
    int feeds = 0;
    while (!st.held && feeds < 40) {
        st = s.feed(120.0, t);
        t += 0.05;
        ++feeds;
    }
    ASSERT_TRUE(st.held);
    EXPECT_GE(feeds * 0.05, 0.75);
    // A move away arms the next hold; the held value stays until then.
    for (int i = 0; i < 6; ++i, t += 0.05)
        st = s.feed(120.0 + i * 3.0, t);
    EXPECT_TRUE(st.armed);
    EXPECT_NEAR(*st.held, 120.0, 0.05);
}

// ---------------------------------------------------------------- the level

TEST(Level, LyingAndStanding)
{
    const Tilt flat = tiltFrom({0, 0, -1});
    EXPECT_TRUE(flat.lying);
    EXPECT_NEAR(flat.slopeDeg, 0, 1e-9);
    EXPECT_NEAR(flat.bubble.x, 0, 1e-9);
    // On a 5 degree slope falling to the right: the bubble drifts left.
    const Tilt slope = tiltFrom({std::sin(5 * kDeg), 0, -std::cos(5 * kDeg)});
    EXPECT_NEAR(slope.slopeDeg, 5, 1e-9);
    EXPECT_NEAR(slope.bubble.x, -0.5, 1e-9);
    // Standing on its bottom edge with the right end 12 degrees up.
    const Tilt edge = tiltFrom({-std::sin(12 * kDeg), std::cos(12 * kDeg), 0});
    EXPECT_FALSE(edge.lying);
    EXPECT_NEAR(edge.edgeDeg, 12, 1e-9);
    EXPECT_NEAR(relativeEdgeAngle(10, 350), 20, 1e-9);
    EXPECT_NEAR(relativeEdgeAngle(-170, 170), 20, 1e-9);
}

TEST(Level, AngleBetweenTwoRestingPositions)
{
    EXPECT_NEAR(angleBetween({0, 0, -1}, {0, std::sin(30 * kDeg), -std::cos(30 * kDeg)}), 30.0, 1e-9);
    EXPECT_NEAR(angleBetween({0, 0, -1}, {0, 0, -1}), 0.0, 1e-9);
    EXPECT_NEAR(angleBetween({0, 1, 0}, {0, 0, -1}), 90.0, 1e-9);
}

// ---------------------------------------------------------------- outlines

TEST(Outline, SidesCornersAreaAndCentering)
{
    const std::vector<Vec2> l{{0, 0}, {40, 0}, {40, 10}, {10, 10}, {10, 30}, {0, 30}};
    const OutlineReport r = describeOutline(l, true);
    ASSERT_EQ(r.sideLengths.size(), 6u);
    EXPECT_NEAR(r.perimeter, 140, 1e-9);
    EXPECT_NEAR(r.area, 40 * 10 + 10 * 20, 1e-9);
    ASSERT_EQ(r.cornerAngles.size(), 6u);
    EXPECT_NEAR(r.cornerAngles[0], 90, 1e-9);
    EXPECT_NEAR(r.cornerAngles[3], 270, 1e-9); // the inside corner
    // The same outline in a photo's y-down coordinates: the same angles.
    const std::vector<Vec2> flipped = centeredOnOrigin(l, true);
    const OutlineReport rf = describeOutline(flipped, true);
    EXPECT_NEAR(rf.cornerAngles[3], 270, 1e-9);
    EXPECT_NEAR(rf.boundsMin.x, -20, 1e-9);
    EXPECT_NEAR(rf.boundsMax.y, 15, 1e-9);
    EXPECT_NEAR(flipped[0].y, 15, 1e-9); // y mirrored
    const OutlineReport path = describeOutline({{0, 0}, {3, 4}}, false);
    EXPECT_NEAR(path.perimeter, 5, 1e-12);
    EXPECT_TRUE(path.cornerAngles.empty());
    EXPECT_EQ(path.area, 0);
}

// ---------------------------------------------------------------- measurements

TEST(Measurements, NamesFormatsAndJson)
{
    MeasurementLog log;
    const std::string a = log.add(Quantity::Length, 42.34, "Ruler", 1000);
    log.add(Quantity::Length, 10, "Photo", 2000);
    const std::string c = log.add(Quantity::Angle, 37.52, "Fold", 3000);
    ASSERT_EQ(log.items().size(), 3u);
    EXPECT_EQ(log.items()[0].name, "Angle 1"); // newest first
    EXPECT_EQ(log.items()[1].name, "Length 2");
    EXPECT_TRUE(log.rename(a, "Shelf depth"));
    EXPECT_FALSE(log.rename(a, ""));
    EXPECT_EQ(log.add(Quantity::Length, 5, "Ruler", 4000).empty(), false);
    EXPECT_EQ(log.items()[0].name, "Length 3");

    EXPECT_EQ(formatMeasurement(Quantity::Length, 42.34, LengthUnit::Millimeter), "42.3 mm");
    EXPECT_EQ(formatMeasurement(Quantity::Length, 25.4, LengthUnit::Inch), "1.000 in");
    EXPECT_EQ(formatMeasurement(Quantity::Angle, 37.52, LengthUnit::Inch), "37.5\xC2\xB0");
    EXPECT_EQ(formatMeasurement(Quantity::Area, 1234.4, LengthUnit::Millimeter), "1234 mm\xC2\xB2");
    EXPECT_EQ(formatMeasurement(Quantity::Area, 25000, LengthUnit::Millimeter), "250.0 cm\xC2\xB2");
    EXPECT_EQ(valueText(Quantity::Length, 42.34, LengthUnit::Millimeter), "42.3mm");
    EXPECT_EQ(valueText(Quantity::Angle, 37.52, LengthUnit::Millimeter), "37.5\xC2\xB0");
    // What valueText writes, the value box's parsers read.
    EXPECT_NEAR(*parseLength(valueText(Quantity::Length, 42.34, LengthUnit::Inch), LengthUnit::Millimeter).millimeters,
                42.34, 0.02);
    EXPECT_NEAR(*parseAngle(valueText(Quantity::Angle, 37.52, LengthUnit::Millimeter)).millimeters, 37.5 * kDeg, 1e-12);

    MeasurementLog copy;
    ASSERT_TRUE(copy.fromJson(log.toJson()));
    ASSERT_EQ(copy.items().size(), 4u);
    EXPECT_EQ(copy.items()[3].name, "Shelf depth");
    EXPECT_NEAR(copy.items()[3].value, 42.34, 1e-12);
    EXPECT_EQ(copy.items()[1].quantity, Quantity::Angle);
    // New ids don't repeat old ones.
    const std::string fresh = copy.add(Quantity::Area, 1, "Photo", 5000);
    for (std::size_t i = 1; i < copy.items().size(); ++i)
        EXPECT_NE(copy.items()[i].id, fresh);
    EXPECT_TRUE(copy.remove(c));
    EXPECT_FALSE(copy.remove(c));
    EXPECT_FALSE(copy.fromJson("{\"not\": \"a list\"}"));
    EXPECT_FALSE(copy.fromJson("garbage"));
    ASSERT_TRUE(copy.fromJson(R"([{"name": "x"}, {"name": "ok", "quantity": "Angle", "value": 3}])"));
    ASSERT_EQ(copy.items().size(), 1u);
    EXPECT_EQ(copy.items()[0].name, "ok");

    for (int i = 0; i < 100; ++i)
        log.add(Quantity::Length, i, "Ruler", i);
    EXPECT_EQ(log.items().size(), MeasurementLog::kMaxItems);
}
