// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Scan (src/measure/Scan*, docs/SCAN.md): cameras from the paper, the
// corners' labelling, masks, carving and the surface, on synthetic photos
// (measure/ScanSynthetic) of known objects.

#include "measure/MeshTools.h"
#include "measure/PaperDetect.h"
#include "measure/Scan.h"
#include "measure/ScanMask.h"
#include "measure/ScanPose.h"
#include "measure/ScanSynthetic.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

using namespace os;
using namespace os::measure;
using namespace os::measure::synthetic;

namespace {

struct Outcome {
    bool ok = false;
    std::string error;
    ScanResult result;
    bool flipped = false;          // the engine's frame is the truth's turned half a turn
    bool labellingConsistent = false;
    Vec3 min, max;                 // the mesh's box in the truth's frame
};

// Runs the engine and brings its result into the truth's frame (the paper
// alone cannot tell the sheet from itself turned half a turn; the engine
// picks one for all photos, which must agree).
Outcome scan(const std::vector<Photo>& photos, bool withMask, bool withFocal, const ScanOptions& options = {})
{
    Outcome o;
    for (const Photo& p : photos)
        EXPECT_TRUE(p.paperInView);
    auto r = runScan(asScanPhotos(photos, withMask, withFocal), options);
    if (!r) {
        o.error = r.userMessage() + " / " + r.developerMessage();
        return o;
    }
    o.ok = true;
    o.result = r.value();
    int same = 0, turned = 0, used = 0;
    for (std::size_t i = 0; i < photos.size(); ++i) {
        const ScanPhotoReport& rep = o.result.photos[i];
        if (!rep.used)
            continue;
        ++used;
        EXPECT_EQ(rep.reversed, photos[i].trueReversed) << "photo " << i;
        if (rep.firstCorner == photos[i].trueFirstCorner)
            ++same;
        else if (rep.firstCorner == (photos[i].trueFirstCorner + 2) % 4)
            ++turned;
    }
    o.flipped = turned > same;
    o.labellingConsistent = used > 0 && (same == used || turned == used);
    const Vec3 a = o.result.surface.boundsMin, b = o.result.surface.boundsMax;
    o.min = o.flipped ? Vec3{-b.x, -b.y, a.z} : a;
    o.max = o.flipped ? Vec3{-a.x, -a.y, b.z} : b;
    return o;
}

void expectClosed(const ScanMesh& mesh)
{
    const MeshReport m = inspectMesh(mesh);
    EXPECT_TRUE(m.closed()) << m.boundaryEdges << " boundary, " << m.nonManifoldEdges << " non-manifold edges";
    EXPECT_TRUE(m.oriented()) << m.misorientedEdges << " misoriented edges";
    EXPECT_GT(m.volume, 0.0);
    EXPECT_EQ(m.degenerateTriangles, 0);
    EXPECT_EQ(selfIntersections(mesh), 0);
    // The bottom lies on the paper.
    EXPECT_NEAR(m.min.z, 0.0, 1e-9);
}

void printSummary(const char* name, const Outcome& o, const Object& truth)
{
    const auto& s = o.result.surface;
    std::printf("[scan] %s: volume %.0f (true %.0f, %+.1f %%), box %.1f..%.1f x %.1f..%.1f x %.1f..%.1f "
                "(true %.1f..%.1f x %.1f..%.1f x 0..%.1f), %d triangles, focal %.2f mm%s, %.0f + %.0f + %.0f ms\n",
                name, s.volumeMm3, truth.volume, 100.0 * (s.volumeMm3 / truth.volume - 1), o.min.x, o.max.x, o.min.y, o.max.y,
                o.min.z, o.max.z, truth.min.x, truth.max.x, truth.min.y, truth.max.y, truth.max.z, s.triangleCount,
                o.result.focal35mm, o.result.focalEstimated ? " (estimated)" : "", o.result.calibrateMs, o.result.carveMs,
                o.result.surfaceMs);
    for (const std::string& w : o.result.warnings)
        std::printf("[scan]   warning: %s\n", w.c_str());
}

void expectFootprint(const Outcome& o, const Object& truth, double tolerance)
{
    EXPECT_NEAR(o.min.x, truth.min.x, tolerance);
    EXPECT_NEAR(o.max.x, truth.max.x, tolerance);
    EXPECT_NEAR(o.min.y, truth.min.y, tolerance);
    EXPECT_NEAR(o.max.y, truth.max.y, tolerance);
}

} // namespace

// ------------------------------------------------------------------ cameras

TEST(ScanPose, CameraFromThePaperInEveryLabelling)
{
    const Reference paper = a4Reference();
    CameraSpec spec;
    spec.azimuthDeg = 130;
    spec.elevationDeg = 40;
    const ScanCamera truth = cameraFor(spec);
    const auto world = paperWorldCorners(paper);
    std::array<Vec2, 4> image{};
    for (int m = 0; m < 4; ++m)
        image[m] = truth.project(world[m]);
    for (int start = 0; start < 4; ++start)
        for (int dir = 0; dir < 2; ++dir) {
            PaperCorners pc;
            pc.width = spec.width;
            pc.height = spec.height;
            for (int m = 0; m < 4; ++m)
                pc.corners[m] = image[dir ? (start - m + 8) % 4 : (start + m) % 4];
            const auto poses = plausiblePoses(pc, paper, truth.fx);
            // A4 is not square: the long side is known, only the half turn is not.
            ASSERT_EQ(poses.size(), 2u) << start << dir;
            const Vec3 c0 = poses[0].camera.center(), c1 = poses[1].camera.center();
            EXPECT_NEAR(c0.x, -c1.x, 1e-6);
            EXPECT_NEAR(c0.y, -c1.y, 1e-6);
            EXPECT_NEAR(c0.z, c1.z, 1e-6);
            bool found = false;
            for (const PaperPose& p : poses) {
                EXPECT_LT(p.reprojectionErrorPx, 1e-6);
                EXPECT_EQ(p.reversed, dir == 1);
                if ((p.camera.center() - truth.center()).length() < 1e-6) {
                    found = true;
                    const int expectFirst = dir ? start : (4 - start) % 4;
                    EXPECT_EQ(p.firstCorner, expectFirst);
                }
            }
            EXPECT_TRUE(found);
        }
    const CameraPlacement place = cameraPlacement(truth);
    EXPECT_NEAR(place.azimuthDeg, 130, 3); // aimed 15 mm above the paper: nearly the spec's
    EXPECT_NEAR(place.elevationDeg, 40, 3);
}

TEST(ScanPose, FocalLengthFromThePapersAlone)
{
    const Reference paper = a4Reference();
    for (double focal : {24.0, 26.0, 52.0}) {
        std::vector<PaperCorners> photos;
        std::mt19937 rng(5);
        std::normal_distribution<double> noise(0.0, 0.4);
        for (const CameraSpec& specIn : ring(10, 32, 58, 5, 360, focal > 40 ? 900 : 480, focal)) {
            CameraSpec spec = specIn;
            if (photos.size() % 3 == 1)
                std::swap(spec.width, spec.height); // some taken upright
            const ScanCamera cam = cameraFor(spec);
            PaperCorners pc;
            pc.width = spec.width;
            pc.height = spec.height;
            const auto world = paperWorldCorners(paper);
            for (int m = 0; m < 4; ++m)
                pc.corners[m] = cam.project(world[m]) + Vec2{noise(rng), noise(rng)};
            photos.push_back(pc);
        }
        int used = 0;
        const auto f = estimateFocal35mm(photos, paper, &used);
        ASSERT_TRUE(f) << focal;
        EXPECT_NEAR(*f, focal, 0.03 * focal);
        EXPECT_GE(used, 6);
        std::printf("[scan] focal %.1f mm estimated %.2f mm (%+.2f %%) from %d photos\n", focal, *f, 100 * (*f / focal - 1), used);
    }
    // Straight from above, the papers say nothing about the focal length.
    std::vector<PaperCorners> above;
    for (const CameraSpec& specIn : ring(4, 89.5, 89.5)) {
        const ScanCamera cam = cameraFor(specIn);
        PaperCorners pc;
        pc.width = specIn.width;
        pc.height = specIn.height;
        const auto world = paperWorldCorners(paper);
        for (int m = 0; m < 4; ++m)
            pc.corners[m] = cam.project(world[m]);
        above.push_back(pc);
    }
    EXPECT_FALSE(estimateFocal35mm(above, paper));
}

// -------------------------------------------------------------------- meshes

TEST(ScanMeshTools, SurfaceOfASphereIsClosedAndDecimatesClosed)
{
    ScanGrid g;
    g.voxel = 1;
    g.nx = g.ny = g.nz = 44;
    g.origin = {-22, -22, 0};
    const Vec3 c{0, 0, 22};
    const double r = 18;
    g.field.resize(std::size_t(g.nx) * g.ny * g.nz);
    for (int k = 0; k < g.nz; ++k)
        for (int j = 0; j < g.ny; ++j)
            for (int i = 0; i < g.nx; ++i)
                g.field[g.index(i, j, k)] = float(r - (g.sampleCenter(i, j, k) - c).length());
    std::vector<char> bottom;
    ScanMesh mesh = surfaceNets(g, true, &bottom);
    MeshReport m = inspectMesh(mesh);
    EXPECT_TRUE(m.oriented());
    const double sphere = 4.0 / 3.0 * kPi * r * r * r;
    EXPECT_NEAR(m.volume, sphere, 0.02 * sphere);
    taubinSmooth(mesh, 6, &bottom);
    decimate(mesh, 1000, &bottom);
    m = inspectMesh(mesh);
    EXPECT_TRUE(m.oriented()) << m.boundaryEdges << " " << m.nonManifoldEdges << " " << m.misorientedEdges;
    EXPECT_LE(m.triangles, 1000);
    EXPECT_GT(m.triangles, 900);
    EXPECT_NEAR(m.volume, sphere, 0.03 * sphere);
    EXPECT_NEAR(m.max.z - m.min.z, 2 * r, 0.8);
    EXPECT_EQ(selfIntersections(mesh), 0);
    // Two tetrahedra pushed into each other cross.
    ScanMesh two;
    two.vertices = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 10}, {2, 2, 2}, {12, 2, 2}, {2, 12, 2}, {2, 2, 12}};
    two.triangles = {{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}, {4, 6, 5}, {4, 5, 7}, {5, 6, 7}, {4, 7, 6}};
    EXPECT_GT(selfIntersections(two), 0);
}

TEST(ScanMeshTools, ThinAndDiagonalVoxelsStillMakeAManifold)
{
    // Voxels touching only along edges or at corners, single-voxel sheets
    // and rods: the clean-up must still give a closed 2-manifold.
    ScanGrid g;
    g.voxel = 1;
    g.nx = g.ny = g.nz = 12;
    g.field.assign(std::size_t(12) * 12 * 12, -0.5f);
    std::mt19937 rng(3);
    for (int n = 0; n < 300; ++n)
        g.field[rng() % g.field.size()] = 0.5f;
    for (int i = 0; i < 12; ++i)
        g.field[g.index(i, i, i)] = 0.5f; // a diagonal chain of corner contacts
    std::vector<char> bottom;
    ScanMesh mesh = surfaceNets(g, true, &bottom);
    MeshReport m = inspectMesh(mesh);
    EXPECT_TRUE(m.oriented()) << m.boundaryEdges << " " << m.nonManifoldEdges << " " << m.misorientedEdges;
    decimate(mesh, 200, &bottom);
    m = inspectMesh(mesh);
    EXPECT_TRUE(m.oriented()) << m.boundaryEdges << " " << m.nonManifoldEdges << " " << m.misorientedEdges;
}

// --------------------------------------------------------------------- masks

TEST(ScanMask, PaperShadowsAndTheObjectApart)
{
    Scene scene;
    scene.object = boxObject({40, 30, 20});
    scene.ambient = 0.38; // deep shadows: about 40 % of the lit paper
    CameraSpec spec;
    spec.azimuthDeg = 200;
    spec.elevationDeg = 40;
    const auto photos = render(scene, {spec});
    const Photo& p = photos[0];
    RgbImage img;
    img.width = p.width;
    img.height = p.height;
    for (std::size_t i = 0; i < p.rgba.size(); i += 4)
        img.rgb.insert(img.rgb.end(), {p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]});
    const auto mask = segmentPhoto(img, p.camera, scene.paper, nullptr);
    long truePositive = 0, falsePositive = 0, falseNegative = 0, carvedObject = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const bool object = p.mask[i] == 255;
        truePositive += object && mask[i] == kMaskObject;
        falsePositive += !object && mask[i] == kMaskObject;
        falseNegative += object && mask[i] != kMaskObject;
        carvedObject += object && mask[i] == kMaskBackground;
    }
    const double iou = double(truePositive) / double(truePositive + falsePositive + falseNegative);
    std::printf("[scan] colour mask IoU %.3f (%ld object pixels called background)\n", iou, carvedObject);
    EXPECT_GT(iou, 0.95);
    // Calling the object background bores into it: only edge pixels may.
    EXPECT_LT(double(carvedObject) / truePositive, 0.01);
}

TEST(ScanMask, FindsThePaperWithoutVision)
{
    // The desktop's stand-in for Apple Vision's rectangle: the corners
    // within a couple of pixels, also with an object hiding part of an edge.
    Scene scene;
    scene.object = boxObject({30, 20, 60}, {0, 95}); // tall, at the sheet's far edge
    std::vector<CameraSpec> specs;
    for (double az : {-90.0, 20.0, 150.0}) {
        CameraSpec c;
        c.azimuthDeg = az;
        c.elevationDeg = 42;
        specs.push_back(c);
    }
    const auto photos = render(scene, specs, 23, 0.0);
    for (const Photo& p : photos) {
        RgbImage img;
        img.width = p.width;
        img.height = p.height;
        for (std::size_t i = 0; i < p.rgba.size(); i += 4)
            img.rgb.insert(img.rgb.end(), {p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]});
        const auto found = detectPaper(img);
        ASSERT_TRUE(found);
        double worst = 0;
        for (const Vec2& truth : p.corners) {
            double nearest = 1e9;
            for (const Vec2& c : *found)
                nearest = std::min(nearest, (c - truth).length());
            worst = std::max(worst, nearest);
        }
        std::printf("[scan] paper found, worst corner %.2f px\n", worst);
        EXPECT_LT(worst, 2.5);
        EXPECT_TRUE(PlaneRectifier::create(*found, scene.paper).has_value());
    }
}

// ------------------------------------------------------------- whole scans

TEST(Scan, BoxWithSubjectMasks)
{
    Scene scene;
    scene.object = boxObject({40, 30, 20}, {25, -15}, 20);
    scene.albedo = {0.5, 0.5, 0.5}; // grey: only the mask tells it from shadows
    const auto photos = render(scene, ring(12, 35, 55), 11);
    const Outcome o = scan(photos, true, true);
    ASSERT_TRUE(o.ok) << o.error;
    printSummary("box, masks", o, scene.object);
    expectClosed(o.result.surface.mesh);
    EXPECT_TRUE(o.labellingConsistent);
    EXPECT_LE(o.result.surface.triangleCount, 3000);
    expectFootprint(o, scene.object, 2.0);
    // The visual hull never cuts into the object, and from 35-55 degrees a
    // flat top grows a low roof (no outline sees past it: docs/SCAN.md).
    EXPECT_GT(o.max.z, scene.object.max.z - 1.0);
    EXPECT_LT(o.max.z, scene.object.max.z + 12.0);
    EXPECT_GT(o.result.surface.volumeMm3, 0.97 * scene.object.volume);
    EXPECT_LT(o.result.surface.volumeMm3, 1.30 * scene.object.volume);
    for (const ScanPhotoReport& r : o.result.photos) {
        EXPECT_TRUE(r.used) << r.problem;
        EXPECT_GT(r.silhouetteAgreement, 0.97);
        EXPECT_LT(r.reprojectionErrorPx, 1.0);
    }
    EXPECT_TRUE(o.result.warnings.empty());
}

TEST(Scan, BoxWithLowPhotosToo)
{
    // Photos from low down see over the top: the roof all but goes.
    Scene scene;
    scene.object = boxObject({40, 30, 20}, {-20, 10}, -10);
    auto cameras = ring(8, 40, 50, 0);
    for (const CameraSpec& low : ring(6, 9, 12, 30, 360, 560))
        cameras.push_back(low);
    const auto photos = render(scene, cameras, 12);
    const Outcome o = scan(photos, true, true);
    ASSERT_TRUE(o.ok) << o.error;
    printSummary("box, low photos", o, scene.object);
    expectClosed(o.result.surface.mesh);
    EXPECT_TRUE(o.labellingConsistent);
    expectFootprint(o, scene.object, 2.0);
    EXPECT_NEAR(o.max.z, scene.object.max.z, 3.0);
    EXPECT_NEAR(o.result.surface.volumeMm3, scene.object.volume, 0.08 * scene.object.volume);
}

TEST(Scan, CylinderByColourWithShadows)
{
    Scene scene;
    scene.object = cylinderObject(40, 50, {-30, 20});
    scene.albedo = {0.25, 0.45, 0.7}; // blue
    scene.ambient = 0.4;
    auto cameras = ring(10, 30, 50, 15);
    for (const CameraSpec& low : ring(5, 12, 12, 50, 360, 560))
        cameras.push_back(low);
    const auto photos = render(scene, cameras, 13);
    const Outcome o = scan(photos, false, true);
    ASSERT_TRUE(o.ok) << o.error;
    printSummary("cylinder, colour", o, scene.object);
    expectClosed(o.result.surface.mesh);
    EXPECT_TRUE(o.labellingConsistent);
    expectFootprint(o, scene.object, 2.0);
    EXPECT_GT(o.max.z, scene.object.max.z - 1.0);
    EXPECT_LT(o.max.z, scene.object.max.z + 5.0);
    EXPECT_NEAR(o.result.surface.volumeMm3, scene.object.volume, 0.08 * scene.object.volume);
}

TEST(Scan, LBlockWithAnUnknownFocalLength)
{
    Scene scene;
    scene.object = lBlockObject({10, 5});
    scene.textureStrength = 0.25; // a textured material, by colour
    auto cameras = ring(12, 35, 55, 0, 360, 480, 28);
    const auto photos = render(scene, cameras, 14, 0.5);
    const Outcome o = scan(photos, false, false);
    ASSERT_TRUE(o.ok) << o.error;
    printSummary("L-block, focal unknown", o, scene.object);
    expectClosed(o.result.surface.mesh);
    EXPECT_TRUE(o.result.focalEstimated);
    EXPECT_NEAR(o.result.focal35mm, 28.0, 0.03 * 28.0);
    EXPECT_TRUE(o.labellingConsistent);
    expectFootprint(o, scene.object, 2.0);
    EXPECT_GT(o.result.surface.volumeMm3, 0.97 * scene.object.volume);
}

TEST(Scan, OffCenterObjectsNeedTheRightHalfTurn)
{
    // Far from the paper's center a wrong half turn puts the object on the
    // other side: the outlines decide even with the photos shuffled.
    Scene scene;
    scene.object = boxObject({30, 20, 25}, {90, 50}, 0);
    auto cameras = ring(10, 35, 55, 0);
    std::mt19937 rng(9);
    std::shuffle(cameras.begin(), cameras.end(), rng);
    const auto photos = render(scene, cameras, 15);
    const Outcome o = scan(photos, true, true);
    ASSERT_TRUE(o.ok) << o.error;
    printSummary("off-center box, shuffled", o, scene.object);
    EXPECT_TRUE(o.labellingConsistent);
    expectFootprint(o, scene.object, 2.0);
    for (const ScanPhotoReport& r : o.result.photos)
        EXPECT_GT(r.labellingMargin, 0.05);
}

TEST(Scan, UChannelHullFillsTheSlot)
{
    // Silhouettes cannot see into a concavity open toward the cameras: the
    // hull fills the U's slot (the stereo stage is what recovers it).
    Scene scene;
    scene.object = uChannelObject({0, 10});
    scene.textureStrength = 0.3;
    auto cameras = ring(10, 30, 55, 5);
    const auto photos = render(scene, cameras, 16);
    const Outcome o = scan(photos, true, true);
    ASSERT_TRUE(o.ok) << o.error;
    printSummary("U-channel, masks", o, scene.object);
    expectClosed(o.result.surface.mesh);
    expectFootprint(o, scene.object, 2.0);
    const double outer = 50.0 * 30 * 30;
    EXPECT_GT(o.result.surface.volumeMm3, 0.95 * outer);
    EXPECT_GT(o.result.surface.volumeMm3, 1.8 * scene.object.volume);
}

TEST(Scan, PhotosFromOneSideAreWarnedAbout)
{
    Scene scene;
    scene.object = boxObject({40, 30, 20});
    const auto photos = render(scene, ring(6, 35, 50, -40, 110), 17);
    const Outcome o = scan(photos, true, true);
    ASSERT_TRUE(o.ok) << o.error;
    bool warned = false;
    for (const std::string& w : o.result.warnings)
        warned = warned || w.find("gap of") != std::string::npos;
    EXPECT_TRUE(warned);
}

TEST(Scan, SmallerWorkingSizeAndProgress)
{
    // Photos shrunk to 800 px: still within 2 mm; progress reported, and a
    // cancel stops it.
    Scene scene;
    scene.object = boxObject({40, 30, 20}, {0, 0}, 35);
    auto cameras = ring(8, 35, 50, 0);
    for (const CameraSpec& low : ring(4, 12, 12, 45, 360, 560))
        cameras.push_back(low);
    const auto photos = render(scene, cameras, 18);
    ScanOptions options;
    options.maxImageSide = 800;
    double last = -1;
    bool monotonic = true;
    auto r = runScan(asScanPhotos(photos, true, true), options, [&](double f) {
        monotonic = monotonic && f >= last - 1e-12;
        last = f;
        return true;
    });
    ASSERT_TRUE(r) << r.userMessage();
    EXPECT_TRUE(monotonic);
    EXPECT_NEAR(last, 1.0, 1e-9);
    EXPECT_NEAR(r->surface.boundsMax.z, 20.0, 3.0);
    EXPECT_NEAR(r->surface.boundsMax.x - r->surface.boundsMin.x, scene.object.max.x - scene.object.min.x, 2.5);
    auto cancelledRun = runScan(asScanPhotos(photos, true, true), options, [](double f) { return f < 0.6; });
    EXPECT_FALSE(cancelledRun);
    EXPECT_EQ(cancelledRun.userMessage(), "Cancelled.");
}

TEST(Scan, RefusesWhatItCannotUse)
{
    Scene scene;
    scene.object = boxObject({40, 30, 20});
    auto photos = render(scene, ring(4, 40, 50), 19);
    auto input = asScanPhotos(photos, true, true);
    // Too few photos.
    EXPECT_FALSE(runScan({input[0], input[1]}));
    // Corners not marked in two of four: fewer than three usable.
    input[0].paperCorners = {};
    input[1].paperCorners = {Vec2{0, 0}, Vec2{10, 0}, Vec2{20, 0}, Vec2{30, 0}};
    auto r = runScan(input);
    EXPECT_FALSE(r);
    EXPECT_NE(r.userMessage().find("fewer than 3"), std::string::npos) << r.userMessage();
    // No object: the paper alone.
    Scene empty;
    empty.object = boxObject({0.001, 0.001, 0.001}, {500, 500});
    auto blank = render(empty, ring(6, 40, 50), 20);
    EXPECT_FALSE(runScan(asScanPhotos(blank, false, true)));
}

TEST(Scan, QuarterResolutionSubjectMasks)
{
    // Vision's foreground masks are coarser than the photo: a quarter here.
    Scene scene;
    scene.object = cylinderObject(40, 50, {20, -10});
    scene.albedo = {0.85, 0.85, 0.85}; // white-ish: the mask alone tells it from the paper's shadows
    auto cameras = ring(10, 30, 50, 15);
    for (const CameraSpec& low : ring(5, 12, 12, 50, 360, 560))
        cameras.push_back(low);
    const auto photos = render(scene, cameras, 22);
    std::vector<std::vector<std::uint8_t>> small(photos.size());
    auto input = asScanPhotos(photos, false, true);
    for (std::size_t i = 0; i < photos.size(); ++i) {
        const int w = photos[i].width / 4, h = photos[i].height / 4;
        small[i].resize(std::size_t(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                small[i][std::size_t(y) * w + x] = photos[i].mask[std::size_t(4 * y + 2) * photos[i].width + 4 * x + 2];
        input[i].subjectMask = small[i].data();
        input[i].maskWidth = w;
        input[i].maskHeight = h;
        input[i].maskStride = w;
    }
    auto r = runScan(input);
    ASSERT_TRUE(r) << r.userMessage();
    std::printf("[scan] cylinder, quarter masks: volume %.0f (true %.0f, %+.1f %%), height %.1f\n", r->surface.volumeMm3,
                scene.object.volume, 100.0 * (r->surface.volumeMm3 / scene.object.volume - 1), r->surface.boundsMax.z);
    expectClosed(r->surface.mesh);
    EXPECT_NEAR(r->surface.volumeMm3, scene.object.volume, 0.12 * scene.object.volume);
    EXPECT_NEAR(r->surface.boundsMax.x - r->surface.boundsMin.x, 40.0, 3.0);
}

TEST(Scan, ShrinkingATwelveMegapixelPhoto)
{
    // Photos are shrunk to the working size first: a 4032 x 3024 photo.
    const int w = 4032, h = 3024;
    std::vector<std::uint8_t> big(std::size_t(w) * h * 4);
    for (std::size_t i = 0; i < big.size(); ++i)
        big[i] = std::uint8_t((i * 2654435761u) >> 24);
    const auto start = std::chrono::steady_clock::now();
    const RgbImage small = downsampleRgb(big.data(), w, h, w * 4, 4, true, 1600);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(small.width, 1600);
    EXPECT_EQ(small.height, 1200);
    std::printf("[scan] a 12 MP photo shrunk to 1600 x 1200 in %.0f ms\n", ms);
}
