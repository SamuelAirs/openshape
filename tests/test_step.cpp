// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// STEP import: names, units, several solids, surfaces, damaged files.

#include "core/Uuid.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace os;
using namespace os::geom;

namespace {

// Unique per run: several builds may run these tests at the same time.
std::filesystem::path stepFile(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_step_" + Uuid::generate().toString() + "_" + name);
}

Shape box(Vec3 origin, Vec3 size)
{
    auto r = makeBox(origin, size);
    EXPECT_TRUE(r.ok()) << r.developerMessage();
    return r.value();
}

std::string readText(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

void expectSameBox(const BoundingBox& a, const Vec3& min, const Vec3& max, double tol)
{
    EXPECT_NEAR(a.min.x, min.x, tol);
    EXPECT_NEAR(a.min.y, min.y, tol);
    EXPECT_NEAR(a.min.z, min.z, tol);
    EXPECT_NEAR(a.max.x, max.x, tol);
    EXPECT_NEAR(a.max.y, max.y, tol);
    EXPECT_NEAR(a.max.z, max.z, tol);
}

} // namespace

TEST(StepImport, RoundTripKeepsVolumeBoxAndName)
{
    // A filleted plate away from the origin: shape, size and place survive.
    Shape plate = box({5, -10, 2}, {60, 40, 8});
    std::vector<int> vertical;
    for (int i = 0; i < plate.edgeCount(); ++i) {
        const auto info = edgeInfo(plate, i);
        if (info && info->kind == CurveKind::Line && std::abs(std::abs(info->tangent.z) - 1.0) < 1e-9)
            vertical.push_back(i);
    }
    ASSERT_EQ(vertical.size(), 4u);
    auto filleted = filletEdges(plate, vertical, 4.0);
    ASSERT_TRUE(filleted.ok());
    const double expectedVolume = volume(filleted.value());
    const BoundingBox expectedBox = boundingBox(filleted.value());

    const auto path = stepFile("plate.step");
    ASSERT_TRUE(exportStep({{"Mounting plate", filleted.value()}}, path).ok());
    auto imported = importStep(path);
    ASSERT_TRUE(imported.ok()) << imported.developerMessage();
    ASSERT_EQ(imported.value().size(), 1u);
    const NamedShape& part = imported.value().front();
    EXPECT_EQ(part.name, "Mounting plate");
    EXPECT_NEAR(volume(part.shape), expectedVolume, 1e-6 * expectedVolume);
    expectSameBox(boundingBox(part.shape), expectedBox.min, expectedBox.max, 1e-4);
    EXPECT_TRUE(isValid(part.shape));
    EXPECT_EQ(part.shape.faceCount(), filleted.value().faceCount());
    EXPECT_TRUE(imported.warnings().empty());
    std::filesystem::remove(path);
}

TEST(StepImport, ImportedGeometryCanBeEdited)
{
    // Rounded plate, through STEP; then push the top up and round an edge.
    Shape plate = box({0, 0, 0}, {60, 40, 20});
    std::vector<int> vertical;
    for (int i = 0; i < plate.edgeCount(); ++i) {
        const auto info = edgeInfo(plate, i);
        if (info && info->kind == CurveKind::Line && std::abs(std::abs(info->tangent.z) - 1.0) < 1e-9)
            vertical.push_back(i);
    }
    auto filleted = filletEdges(plate, vertical, 3.0);
    ASSERT_TRUE(filleted.ok());
    const auto path = stepFile("editable.step");
    ASSERT_TRUE(exportStep({{"Plate", filleted.value()}}, path).ok());
    auto imported = importStep(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(imported.ok());
    const Shape& s = imported.value()[0].shape;
    int top = -1;
    for (int i = 0; i < s.faceCount(); ++i) {
        const auto info = faceInfo(s, i);
        if (info && info->isPlanar() && info->normal.z > 0.9999)
            top = i;
    }
    ASSERT_GE(top, 0);
    ASSERT_TRUE(isValid(s));
    // Modeling leaves its input usable (kernel booleans could change it in place).
    const double volumeBefore = volume(s);
    const int facesBefore = s.faceCount();
    const double topArea = 60.0 * 40.0 - 4 * (9.0 - kPi * 9.0 / 4.0);
    for (double d : {5.0, -5.0}) {
        auto pushed = pushPullFace(s, top, d);
        ASSERT_TRUE(pushed.ok()) << d << ": " << pushed.developerMessage();
        EXPECT_NEAR(volume(pushed.value()), volume(s) + topArea * d, 1e-3);
        // (Not applicable here: the top face has no rounded edges of its own.)
        auto kept = pushPullFaceKeepingEdges(s, top, d);
        EXPECT_EQ(kept.error(), ErrorCode::Unsupported);
        EXPECT_TRUE(isValid(s)) << "the imported shape itself is still valid";
        EXPECT_EQ(s.faceCount(), facesBefore);
        EXPECT_NEAR(volume(s), volumeBefore, 1e-9 * volumeBefore);
    }
    // Round the top edges of the pushed part: an edge of the imported geometry.
    auto pushed = pushPullFace(s, top, 5.0);
    ASSERT_TRUE(pushed.ok());
    std::vector<int> topEdges;
    for (int i = 0; i < pushed.value().edgeCount(); ++i) {
        const auto info = edgeInfo(pushed.value(), i);
        if (info && info->kind == CurveKind::Line && std::abs(info->midpoint.z - 25.0) < 1e-6)
            topEdges.push_back(i);
    }
    ASSERT_EQ(topEdges.size(), 4u);
    auto rounded = filletEdges(pushed.value(), {topEdges[0]}, 1.0);
    ASSERT_TRUE(rounded.ok()) << rounded.developerMessage();
    EXPECT_LT(volume(rounded.value()), volume(pushed.value()));
}

TEST(StepImport, InchesAndMetersComeInAtTheRightSize)
{
    // A one-inch cube written in inches, and a 1 m x 20 mm x 30 mm bar in meters.
    const Shape cube = box({0, 0, 0}, {25.4, 25.4, 25.4});
    const Shape bar = box({-500, 0, 0}, {1000, 20, 30});
    const auto inches = stepFile("cube_in.step");
    const auto meters = stepFile("bar_m.step");
    ASSERT_TRUE(exportStep({{"Cube", cube}}, inches, {StepUnit::Inch, false}).ok());
    ASSERT_TRUE(exportStep({{"Bar", bar}}, meters, {StepUnit::Meter, false}).ok());

    // The files really are in those units (not millimeters).
    EXPECT_NE(readText(inches).find("CONVERSION_BASED_UNIT('INCH'"), std::string::npos);
    EXPECT_NE(readText(meters).find("SI_UNIT($,.METRE.)"), std::string::npos) << "meters without a prefix";

    auto a = importStep(inches);
    ASSERT_TRUE(a.ok()) << a.developerMessage();
    ASSERT_EQ(a.value().size(), 1u);
    EXPECT_NEAR(volume(a.value()[0].shape), 25.4 * 25.4 * 25.4, 1e-6 * 16387.064);
    expectSameBox(boundingBox(a.value()[0].shape), {0, 0, 0}, {25.4, 25.4, 25.4}, 1e-5);

    auto b = importStep(meters);
    ASSERT_TRUE(b.ok()) << b.developerMessage();
    ASSERT_EQ(b.value().size(), 1u);
    EXPECT_NEAR(volume(b.value()[0].shape), 1000.0 * 20 * 30, 1e-3);
    expectSameBox(boundingBox(b.value()[0].shape), {-500, 0, 0}, {500, 20, 30}, 1e-5);
    std::filesystem::remove(inches);
    std::filesystem::remove(meters);
}

TEST(StepImport, SeveralSolidsBecomeSeveralNamedShapes)
{
    const Shape left = box({0, 0, 0}, {10, 10, 10});
    const Shape right = box({30, 0, 0}, {20, 10, 5});
    const Shape unnamed = box({0, 40, 0}, {5, 5, 5});
    // A body in two pieces is one product with two solids.
    auto pieces = gatherSolids({box({0, -40, 0}, {4, 4, 4}), box({10, -40, 0}, {4, 4, 4})});
    ASSERT_TRUE(pieces.ok());
    // A moved body (the kernel keeps the motion as a location) and two bodies
    // sharing one kernel shape (a duplicate not moved yet) stay separate.
    auto moved = translated(box({0, 0, 0}, {6, 6, 6}), {100, 50, 25});
    ASSERT_TRUE(moved.ok());
    const Shape twin = box({-50, 0, 0}, {3, 3, 3});
    const auto path = stepFile("multi.step");
    ASSERT_TRUE(exportStep({{"Left", left},
                            {"Right", right},
                            {"", unnamed},
                            {"Feet", pieces.value()},
                            {"Moved", moved.value()},
                            {"Twin A", twin},
                            {"Twin B", twin}},
                           path)
                    .ok());

    auto imported = importStep(path);
    ASSERT_TRUE(imported.ok()) << imported.developerMessage();
    ASSERT_EQ(imported.value().size(), 8u);
    std::map<std::string, double> volumes;
    std::map<std::string, BoundingBox> boxes;
    for (const NamedShape& s : imported.value()) {
        volumes[s.name] += volume(s.shape);
        boxes[s.name] = boundingBox(s.shape);
    }
    EXPECT_NEAR(volumes["Left"], 1000.0, 1e-6);
    EXPECT_NEAR(volumes["Right"], 1000.0, 1e-6);
    EXPECT_NEAR(volumes[""], 125.0, 1e-6) << "no name: OCCT's placeholder product name is not a name";
    EXPECT_NEAR(volumes["Feet 1"], 64.0, 1e-6);
    EXPECT_NEAR(volumes["Feet 2"], 64.0, 1e-6);
    EXPECT_NEAR(volumes["Moved"], 216.0, 1e-6);
    expectSameBox(boxes["Moved"], {100, 50, 25}, {106, 56, 31}, 1e-5);
    EXPECT_NEAR(volumes["Twin A"], 27.0, 1e-6);
    EXPECT_NEAR(volumes["Twin B"], 27.0, 1e-6);
    expectSameBox(boxes["Right"], {30, 0, 0}, {50, 10, 5}, 1e-5);
    std::filesystem::remove(path);
}

TEST(StepImport, ClosedSurfacesBecomeSolidsOpenOnesAreSkipped)
{
    // A solid written as a surface model (closed shell) is closed again.
    const Shape cube = box({0, 0, 0}, {10, 20, 30});
    const auto surfaces = stepFile("surfaces.step");
    ASSERT_TRUE(exportStep({{"Shell cube", cube}}, surfaces, {StepUnit::Millimeter, true}).ok());
    EXPECT_NE(readText(surfaces).find("SHELL_BASED_SURFACE_MODEL"), std::string::npos);
    auto closed = importStep(surfaces);
    ASSERT_TRUE(closed.ok()) << closed.developerMessage();
    ASSERT_EQ(closed.value().size(), 1u);
    EXPECT_NEAR(volume(closed.value()[0].shape), 6000.0, 1e-6);
    EXPECT_TRUE(closed.warnings().empty());

    // A flat face next to a solid: the solid comes in, the face is reported.
    PlaneFrame plane;
    plane.origin = {0, 0, 50};
    auto regions = findRegions(plane, {{PlanarCurve::Kind::Circle, {}, {}, {0, 0, 50}, 5.0}});
    ASSERT_TRUE(regions.ok());
    ASSERT_EQ(regions.value().size(), 1u);
    const auto mixed = stepFile("mixed.step");
    ASSERT_TRUE(exportStep({{"Block", cube}, {"Disc", regions.value()[0].face}}, mixed).ok());
    auto some = importStep(mixed);
    ASSERT_TRUE(some.ok()) << some.developerMessage();
    ASSERT_EQ(some.value().size(), 1u);
    EXPECT_EQ(some.value()[0].name, "Block");
    ASSERT_EQ(some.warnings().size(), 1u);
    EXPECT_NE(some.warnings()[0].find("1 open surface"), std::string::npos) << some.warnings()[0];

    // Only the face: nothing to import, said plainly.
    const auto faceOnly = stepFile("face.step");
    ASSERT_TRUE(exportStep({{"Disc", regions.value()[0].face}}, faceOnly).ok());
    auto none = importStep(faceOnly);
    ASSERT_FALSE(none.ok());
    EXPECT_EQ(none.error(), ErrorCode::FileFormatError);
    EXPECT_NE(none.userMessage().find("no closed solids"), std::string::npos) << none.userMessage();
    EXPECT_NE(none.userMessage().find("1 open surface"), std::string::npos) << none.userMessage();
    for (const auto& p : {surfaces, mixed, faceOnly})
        std::filesystem::remove(p);
}

TEST(StepImport, EmptyGarbageAndTruncatedFilesFailPlainly)
{
    const auto empty = stepFile("empty.step");
    { std::ofstream out(empty, std::ios::binary); }
    auto a = importStep(empty);
    ASSERT_FALSE(a.ok());
    EXPECT_EQ(a.userMessage(), "This STEP file is empty.");

    const auto garbage = stepFile("garbage.step");
    {
        std::ofstream out(garbage, std::ios::binary);
        out << "ISO-10303-21;\nthis is garbage\n";
    }
    auto b = importStep(garbage);
    ASSERT_FALSE(b.ok());
    EXPECT_FALSE(b.userMessage().empty());

    // The first half of a real file: its geometry section is cut off.
    const auto good = stepFile("good.step");
    ASSERT_TRUE(exportStep({{"Box", box({0, 0, 0}, {10, 10, 10})}}, good).ok());
    const std::string text = readText(good);
    const auto truncated = stepFile("truncated.step");
    {
        std::ofstream out(truncated, std::ios::binary);
        out << text.substr(0, text.size() / 2);
    }
    auto c = importStep(truncated);
    ASSERT_FALSE(c.ok());
    EXPECT_FALSE(c.userMessage().empty());
    EXPECT_EQ(c.userMessage().find("Standard_"), std::string::npos) << "no kernel jargon: " << c.userMessage();

    EXPECT_EQ(importStep(stepFile("missing.step")).error(), ErrorCode::FileNotFound);
    for (const auto& p : {empty, garbage, good, truncated})
        std::filesystem::remove(p);
}

TEST(StepImport, TooManySolidsAreRefusedBeforeAnyIsRepaired)
{
    // One product holding five solids.
    std::vector<Shape> boxes;
    for (int i = 0; i < 5; ++i)
        boxes.push_back(box({i * 20.0, 0, 0}, {10, 10, 10}));
    auto five = gatherSolids(boxes);
    ASSERT_TRUE(five.ok());
    const auto rack = stepFile("rack.step");
    ASSERT_TRUE(exportStep({{"Rack", five.value()}}, rack).ok());
    StepReadOptions options;
    options.maxSolids = 4;
    auto refused = importStep(rack, options);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ErrorCode::Unsupported);
    EXPECT_EQ(refused.userMessage(), "This STEP file has more than 4 solids, too many to import as bodies.");
    options.maxSolids = 5;
    auto taken = importStep(rack, options);
    ASSERT_TRUE(taken.ok()) << taken.developerMessage();
    EXPECT_EQ(taken.value().size(), 5u);

    // Closed shells count (they become solids), open surfaces do not.
    const auto shells = stepFile("shells.step");
    ASSERT_TRUE(exportStep({{"A", box({0, 0, 0}, {10, 10, 10})}, {"B", box({20, 0, 0}, {10, 10, 10})}}, shells,
                           {StepUnit::Millimeter, true})
                    .ok());
    options.maxSolids = 1;
    EXPECT_FALSE(importStep(shells, options).ok());
    options.maxSolids = 2;
    auto closed = importStep(shells, options);
    ASSERT_TRUE(closed.ok()) << closed.developerMessage();
    EXPECT_EQ(closed.value().size(), 2u);

    PlaneFrame plane;
    plane.origin = {0, 0, 50};
    auto regions = findRegions(plane, {{PlanarCurve::Kind::Circle, {}, {}, {0, 0, 50}, 5.0}});
    ASSERT_TRUE(regions.ok());
    ASSERT_EQ(regions.value().size(), 1u);
    const auto mixed = stepFile("mixed_limit.step");
    ASSERT_TRUE(exportStep({{"Block", box({0, 0, 0}, {10, 10, 10})}, {"Disc", regions.value()[0].face}}, mixed).ok());
    options.maxSolids = 1;
    auto one = importStep(mixed, options);
    ASSERT_TRUE(one.ok()) << one.developerMessage();
    EXPECT_EQ(one.value().size(), 1u);
    EXPECT_EQ(one.warnings().size(), 1u);
    for (const auto& p : {rack, shells, mixed})
        std::filesystem::remove(p);
}
