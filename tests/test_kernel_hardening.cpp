// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions of this file are adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE):
// the cases (a size search that is not monotonic, repairs refused for their
// tolerance) follow their findings.

// Kernel robustness, measured: time limits stop runaway algorithms with a
// plain message (and previews give up sooner), booleans treat faces within
// rounding of each other as touching, an invalid result gets one repair
// that is kept only under a tolerance cap, validity failures name the
// sub-shape and its parent, blends leave out edges they cannot use, the
// size search copes with sizes that fail below ones that work, and shells
// retry C0 faces. Checked by volumes, face and solid counts and exact
// messages.
#include "geometry/KernelSignals.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "interaction/PreviewWorker.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <sstream>

using namespace os;
using namespace os::geom;
using namespace std::chrono_literals;

namespace {

const std::string kTooLong = "This took too long and was stopped.";

Shape box(Vec3 origin, Vec3 size)
{
    auto r = makeBox(origin, size);
    EXPECT_TRUE(r.ok()) << r.developerMessage();
    return r.value();
}

int faceFacing(const Shape& s, const Vec3& n)
{
    for (int i = 0; i < s.faceCount(); ++i) {
        const auto info = faceInfo(s, i);
        if (info && info->isPlanar() && info->normal.dot(n) > 0.9999)
            return i;
    }
    return -1;
}

std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

std::string join(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& line : lines)
        out += line + "\n";
    return out;
}

// A 10 mm cube's BRep text with the vertex at (10, 10, 10) moved by `by`
// along x: the edges still end where the vertex was, so the solid is
// invalid until its tolerance covers the gap.
std::string cubeWithMovedCorner(double by)
{
    auto lines = linesOf(toBrepString(box({0, 0, 0}, {10, 10, 10}), false));
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (lines[i - 1].find("Ve") != 0)
            continue;
        // "Ve", tolerance, then the point.
        std::istringstream in(lines[i + 1]);
        double x = 0, y = 0, z = 0;
        in >> x >> y >> z;
        if (x == 10 && y == 10 && z == 10) {
            std::ostringstream moved;
            moved.precision(17);
            moved << 10 + by << " 10 10 ";
            lines[i + 1] = moved.str();
            return join(lines);
        }
    }
    ADD_FAILURE() << "no corner at (10, 10, 10)";
    return {};
}

} // namespace

// ---- Time limits ------------------------------------------------------------

TEST(KernelHardening, TimeLimitsNestAndDefault)
{
    EXPECT_EQ(kernelTimeLimit(), kDefaultKernelTimeLimit);
    EXPECT_EQ(kDefaultKernelTimeLimit, 60'000ms);
    {
        const ScopedKernelTimeLimit outer(5s);
        EXPECT_EQ(kernelTimeLimit(), 5'000ms);
        {
            const ScopedKernelTimeLimit inner(0ms);
            EXPECT_EQ(kernelTimeLimit(), 0ms);
        }
        EXPECT_EQ(kernelTimeLimit(), 5'000ms);
    }
    EXPECT_EQ(kernelTimeLimit(), kDefaultKernelTimeLimit);
    // Per thread: another thread keeps the default.
    const ScopedKernelTimeLimit here(1ms);
    auto other = std::async(std::launch::async, [] { return kernelTimeLimit(); });
    EXPECT_EQ(other.get(), kDefaultKernelTimeLimit);
}

TEST(KernelHardening, PreviewWorkerGivesUpSooner)
{
    std::promise<std::chrono::milliseconds> seen;
    interact::PreviewWorker worker;
    worker.submit([&seen] {
        seen.set_value(kernelTimeLimit());
        return interact::PreviewWorker::Delivery{};
    });
    auto future = seen.get_future();
    ASSERT_EQ(future.wait_for(10s), std::future_status::ready);
    EXPECT_EQ(future.get(), kPreviewKernelTimeLimit);
    EXPECT_EQ(kPreviewKernelTimeLimit, 25'000ms);
}

// A limit that has already passed stops each algorithm at its first
// progress check, as a runaway one is stopped when its limit passes.
TEST(KernelHardening, AnExpiredLimitStopsTheLongAlgorithms)
{
    const Shape block = box({0, 0, 0}, {30, 20, 10});
    const Shape pin = makeCylinder({15, 10, -5}, {0, 0, 1}, 3, 20).value();
    const int top = faceFacing(block, {0, 0, 1});
    const std::string text = toBrepString(block, false);
    {
        const ScopedKernelTimeLimit expired(0ms);
        for (BooleanKind kind : {BooleanKind::Union, BooleanKind::Subtract, BooleanKind::Intersect}) {
            const auto r = booleanOp(block, pin, kind);
            EXPECT_FALSE(r.ok());
            EXPECT_EQ(r.userMessage(), kTooLong) << r.developerMessage();
            EXPECT_EQ(r.error(), ErrorCode::KernelFailure);
        }
        const auto pulled = pushPullFace(block, top, 5);
        EXPECT_EQ(pulled.userMessage(), kTooLong) << pulled.developerMessage();
        const auto hollow = shell(block, {top}, 1, SizeAdvice{true, LengthUnit::Millimeter});
        EXPECT_EQ(hollow.userMessage(), kTooLong) << "no size search after a time limit: " << hollow.developerMessage();
        const auto repeated = repeatJoined(block, {RigidMotion{{0, 0, 0}, {0, 0, 1}, 0, {15, 0, 0}}});
        EXPECT_EQ(repeated.userMessage(), kTooLong) << repeated.developerMessage();
        // Meshing and reading stored geometry stop too.
        EXPECT_EQ(tessellate(block).triangleCount(), 0u);
        const auto read = fromBrepString(text);
        EXPECT_FALSE(read.ok());
        EXPECT_EQ(read.userMessage(), "The stored geometry could not be read.");
    }
    // The limit restored, the same calls work.
    EXPECT_TRUE(booleanOp(block, pin, BooleanKind::Subtract).ok());
    EXPECT_NEAR(volume(pushPullFace(block, top, 5).value()), 30 * 20 * 15, 1e-6);
    EXPECT_GT(tessellate(block).triangleCount(), 0u);
    EXPECT_TRUE(fromBrepString(text).ok());
}

// ---- Fuzzy booleans --------------------------------------------------------------

// Faces within rounding of each other (a body placed from a typed value,
// 1e-6 mm off) are treated as touching: the pocket opens instead of
// leaving a skin 1 micron thin over a closed void.
TEST(KernelHardening, NearlyTouchingFacesCountAsTouching)
{
    const Shape block = box({0, 0, 0}, {10, 10, 10});
    const Shape tool = box({2, 2, 5}, {6, 6, 5 - 1e-6}); // top 1e-6 below the block's
    const auto pocket = booleanOp(block, tool, BooleanKind::Subtract);
    ASSERT_TRUE(pocket.ok()) << pocket.developerMessage();
    // An open pocket: the cube's 6 faces (the top one with a hole) and the
    // pocket's floor and 4 walls. A skin would add a closed void: 12 faces.
    EXPECT_EQ(pocket.value().faceCount(), 6 + 5);
    EXPECT_NEAR(volume(pocket.value()), 1000 - 36 * 5, 1e-3);
    EXPECT_TRUE(isValid(pocket.value()));

    // A second block 2e-6 away from the first joins it into one piece.
    const auto joined = booleanOp(block, box({10 + 2e-6, 0, 0}, {10, 10, 10}), BooleanKind::Union);
    ASSERT_TRUE(joined.ok());
    EXPECT_EQ(solids(joined.value()).size(), 1u);
    EXPECT_TRUE(joined.warnings().empty());
    EXPECT_NEAR(volume(joined.value()), 2000, 1e-3);

    // Exactly touching bodies keep their exact volumes.
    const auto touching = booleanOp(block, box({10, 0, 0}, {10, 10, 10}), BooleanKind::Union);
    ASSERT_TRUE(touching.ok());
    EXPECT_EQ(solids(touching.value()).size(), 1u);
    EXPECT_EQ(touching.value().faceCount(), 6);
    EXPECT_NEAR(volume(touching.value()), 2000, 1e-9);
    // Clearly apart (0.01 mm) stays two pieces.
    const auto apart = booleanOp(block, box({10.01, 0, 0}, {10, 10, 10}), BooleanKind::Union);
    ASSERT_TRUE(apart.ok());
    EXPECT_EQ(solids(apart.value()).size(), 2u);
}

// ---- One repair, under a tolerance cap ------------------------------------------------

TEST(KernelHardening, SmallDamageIsRepairedOnce)
{
    auto damaged = fromBrepString(cubeWithMovedCorner(1e-4));
    ASSERT_TRUE(damaged.ok()) << damaged.developerMessage();
    EXPECT_FALSE(isValid(damaged.value()));
    const auto repaired = gatherSolids({damaged.value()});
    ASSERT_TRUE(repaired.ok()) << repaired.developerMessage();
    EXPECT_TRUE(isValid(repaired.value()));
    EXPECT_NEAR(volume(repaired.value()), 1000, 0.01);
    EXPECT_NE(repaired.developerMessage().find("repaired by ShapeFix_Shape"), std::string::npos) << repaired.developerMessage();
    // The damaged input itself is left as it was.
    EXPECT_FALSE(isValid(damaged.value()));
}

TEST(KernelHardening, RepairsThatLoosenTheToleranceToPartSizeAreRefused)
{
    // 2 mm off in a 17 mm part: a repair would need a tolerance of about
    // 2 mm, more than 1% of the diagonal and 10x the 1e-7 before.
    auto damaged = fromBrepString(cubeWithMovedCorner(2.0));
    ASSERT_TRUE(damaged.ok()) << damaged.developerMessage();
    const auto refused = gatherSolids({damaged.value()});
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ErrorCode::InvalidResultShape);
    EXPECT_EQ(refused.userMessage(), "Unable to separate the pieces of this body.");
    const std::string& dev = refused.developerMessage();
    EXPECT_NE(dev.find("too loose to build on"), std::string::npos) << dev;
    // The checker's findings name the sub-shape in its parent.
    EXPECT_NE(dev.find(" in EDGE: "), std::string::npos) << dev;
}

// ---- Fillets and chamfers ------------------------------------------------------------

// A cylinder's seam (where its round face closes on itself) is not a
// corner: blends leave it out, and a selection of only seams is refused.
TEST(KernelHardening, BlendsLeaveOutSeams)
{
    const Shape rod = makeCylinder({0, 0, 0}, {0, 0, 1}, 5, 20).value();
    int seam = -1, rim = -1;
    for (int i = 0; i < rod.edgeCount(); ++i) {
        const auto info = edgeInfo(rod, i);
        if (!info)
            continue;
        if (info->kind == CurveKind::Line)
            seam = i;
        else if (info->kind == CurveKind::Circle && info->center.z > 19)
            rim = i;
    }
    ASSERT_GE(seam, 0);
    ASSERT_GE(rim, 0);
    const double full = kPi * 25 * 20;
    const auto rounded = filletEdges(rod, {seam, rim}, 1.0);
    ASSERT_TRUE(rounded.ok()) << rounded.developerMessage();
    // Pappus: the rim's ring of cross-section (1 - pi/4) r^2, whose centroid
    // lies (10 - 3 pi) / (12 - 3 pi) r in from the wall, turned about the axis.
    const double r = 1, R = 5;
    const double filletRing = (1 - kPi / 4) * r * r * 2 * kPi * (R - (10 - 3 * kPi) / (12 - 3 * kPi) * r);
    EXPECT_NEAR(volume(rounded.value()), full - filletRing, 1e-4 * full);
    const auto chamfered = chamferEdges(rod, {seam, rim}, 1.0);
    ASSERT_TRUE(chamfered.ok()) << chamfered.developerMessage();
    const double chamferRing = 0.5 * r * r * 2 * kPi * (R - r / 3);
    EXPECT_NEAR(volume(chamfered.value()), full - chamferRing, 1e-6 * full);

    const auto onlySeam = filletEdges(rod, {seam}, 1.0);
    ASSERT_FALSE(onlySeam.ok());
    EXPECT_EQ(onlySeam.userMessage(), "This edge is not a corner between two faces, so it cannot be rounded.");
    const auto onlySeamChamfer = chamferEdges(rod, {seam}, 1.0);
    EXPECT_EQ(onlySeamChamfer.userMessage(), "This edge is not a corner between two faces, so it cannot be bevelled.");
}

// ---- Size search -----------------------------------------------------------------------

// Whether a blend builds is not quite monotonic in its size: here sizes
// below 0.2 fail and 0.2 to 5 work (a fillet chain OpenShape3D met failed
// at 0.05 and built from 0.1 to 5). The tiny size fails, yet 5 is found.
TEST(KernelHardening, SizeSearchLooksAboveAFailingTinySize)
{
    int calls = 0;
    const auto largest = detail::largestWorkingSize(40, 0ms, [&calls](double size) {
        ++calls;
        return size >= 0.2 && size <= 5;
    });
    ASSERT_TRUE(largest.has_value());
    EXPECT_GT(*largest, 4.5);
    EXPECT_LE(*largest, 5.0);
    EXPECT_LE(calls, detail::kMaxSizeAttempts);
    // Nothing works: still "at any size", after the halvings.
    calls = 0;
    const auto none = detail::largestWorkingSize(40, 0ms, [&calls](double) {
        ++calls;
        return false;
    });
    ASSERT_TRUE(none.has_value());
    EXPECT_EQ(*none, 0.0);
    EXPECT_EQ(calls, 1 + 8) << "the tiny size, then 20, 10, 5, 2.5, 1.25, 0.625, 0.3125, 0.15625";
}

// ---- Volume ------------------------------------------------------------------------------

// A round rod turned into B-splines (a STEP file from another program
// often holds its cylinders so): the volume comes out exact.
TEST(KernelHardening, VolumeIsExactOnRoundParts)
{
    const Shape rod = makeCylinder({0, 0, 0}, {0, 0, 1}, 5, 20).value();
    EXPECT_NEAR(volume(rod), kPi * 25 * 20, 1e-9 * kPi * 25 * 20);
    const auto signature = solidSignature(rod);
    EXPECT_NEAR(signature.volume, kPi * 25 * 20, 1e-9 * kPi * 25 * 20);
    EXPECT_NEAR(signature.centroid.z, 10, 1e-9);
    EXPECT_NEAR(signature.centroid.x, 0, 1e-9);
}
