// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The idea of this test (serialize every input before and after an
// operation, ignoring the TShape flag lines) is adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE).

// A modeling operation must leave the shapes it is given exactly as they
// were: an input is a step's cached output, the previous state undo keeps,
// or an imported body, and later steps and undo read it again. OCCT can
// change sub-shapes of its arguments in place (tolerances raised by a
// boolean, edges rewritten by the same-domain face merge, pcurves added by
// an offset), so every operation runs here on primitives and on a plate
// read back from STEP, and each input must serialize to the same text
// afterwards.
#include "TestFonts.h"

#include "core/Uuid.h"
#include "geometry/Exchange.h"
#include "geometry/Holes.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"
#include "geometry/Text.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <sstream>

using namespace os;
using namespace os::geom;

namespace {

// The shape as BRep text without its TShape flag lines (seven 0/1 digits:
// free, modified, checked, orientable, closed, infinite, convex). The
// checker sets "checked" and placing a shape in a compound clears "free";
// neither is geometry. Everything else (points, curves, pcurves,
// tolerances, sub-shape structure) is compared.
std::string snapshot(const Shape& shape)
{
    std::istringstream in(toBrepString(shape, false));
    std::string out, line;
    while (std::getline(in, line)) {
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' '))
            trimmed.pop_back();
        while (!trimmed.empty() && trimmed.front() == ' ')
            trimmed.erase(trimmed.begin());
        if (trimmed.size() == 7 && trimmed.find_first_not_of("01") == std::string::npos)
            continue;
        out += line;
        out += '\n';
    }
    return out;
}

// The first line where two snapshots differ, for the failure message.
std::string firstDifference(const std::string& a, const std::string& b)
{
    std::istringstream ia(a), ib(b);
    std::string la, lb;
    for (int line = 1;; ++line) {
        const bool moreA = static_cast<bool>(std::getline(ia, la));
        const bool moreB = static_cast<bool>(std::getline(ib, lb));
        if (!moreA && !moreB)
            return "no difference";
        if (la != lb || moreA != moreB)
            return "line " + std::to_string(line) + ": '" + (moreA ? la : "<end>") + "' became '" + (moreB ? lb : "<end>") + "'";
    }
}

enum class Expect { Success, Either };

// Runs `op` and checks that every input serializes as before (and, unless
// `Either`, that the operation succeeded).
template <typename Op>
void expectUntouched(const std::string& what, const std::vector<Shape>& inputs, Op&& op, Expect expect = Expect::Success)
{
    std::vector<std::string> before;
    for (const Shape& s : inputs)
        before.push_back(snapshot(s));
    const auto result = op();
    if (expect == Expect::Success) {
        EXPECT_TRUE(result.ok()) << what << ": " << result.developerMessage();
    }
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const std::string after = snapshot(inputs[i]);
        EXPECT_TRUE(after == before[i]) << what << ": input " << i << " was changed by the operation, "
                                        << firstDifference(before[i], after);
    }
}

// A fresh copy through BRep text: no other shape shares its sub-shapes.
Shape fresh(const Shape& shape)
{
    auto r = fromBrepString(toBrepString(shape, false));
    EXPECT_TRUE(r.ok()) << r.developerMessage();
    return r.value();
}

Shape box(Vec3 origin, Vec3 size)
{
    return makeBox(origin, size).value();
}

std::vector<int> edgesAlongZ(const Shape& s)
{
    std::vector<int> out;
    for (int i = 0; i < s.edgeCount(); ++i) {
        const auto info = edgeInfo(s, i);
        if (info && info->kind == CurveKind::Line && std::abs(std::abs(info->tangent.z) - 1.0) < 1e-9)
            out.push_back(i);
    }
    return out;
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

int firstFaceOfKind(const Shape& s, SurfaceKind kind)
{
    for (int i = 0; i < s.faceCount(); ++i)
        if (faceInfo(s, i)->kind == kind)
            return i;
    return -1;
}

// A 60 x 40 x 8 plate with rounded vertical corners, written to STEP and
// read back (imported geometry: its own tolerances and B-spline pcurves).
Shape stepPlate()
{
    const Shape plate = box({0, 0, 0}, {60, 40, 8});
    const Shape rounded = filletEdges(plate, edgesAlongZ(plate), 4.0).value();
    const auto path = std::filesystem::temp_directory_path() / ("openshape_untouched_" + Uuid::generate().toString() + ".step");
    EXPECT_TRUE(exportStep({{"Plate", rounded}}, path).ok());
    auto imported = importStep(path);
    std::filesystem::remove(path);
    EXPECT_TRUE(imported.ok()) << imported.developerMessage();
    return imported.value().front().shape;
}

struct Inputs {
    std::string name;
    Shape body;
};

std::vector<Inputs> bodies()
{
    const Shape block = box({0, 0, 0}, {30, 20, 10});
    const Shape holed = booleanOp(fresh(block), makeCylinder({15, 10, -5}, {0, 0, 1}, 3, 20).value(), BooleanKind::Subtract).value();
    return {{"box", fresh(block)}, {"holed box", fresh(holed)}, {"STEP plate", stepPlate()}};
}

} // namespace

TEST(OpsInputsUntouched, Booleans)
{
    for (const Inputs& in : bodies()) {
        const std::string& name = in.name;
        const Shape& body = in.body;
        const auto b = approximateBoundingBox(body);
        // A pin through the middle and a block over one side.
        const Shape pin = fresh(makeCylinder({b.center().x + 4, b.center().y, b.min.z - 5}, {0, 0, 1}, 2, b.size().z + 10).value());
        const Shape side = fresh(box({b.max.x - 5, b.min.y - 2, b.min.z + 2}, {10, b.size().y + 4, 3}));
        for (const Shape& tool : {pin, side}) {
            expectUntouched(name + " union", {body, tool}, [&] { return booleanOp(body, tool, BooleanKind::Union); });
            expectUntouched(name + " subtract", {body, tool}, [&] { return booleanOp(body, tool, BooleanKind::Subtract); });
            expectUntouched(name + " intersect", {body, tool}, [&] { return booleanOp(body, tool, BooleanKind::Intersect); });
        }
    }
}

TEST(OpsInputsUntouched, FaceOperations)
{
    for (const Inputs& in : bodies()) {
        const std::string& name = in.name;
        const Shape& body = in.body;
        const int top = faceFacing(body, {0, 0, 1});
        ASSERT_GE(top, 0) << name;
        expectUntouched(name + " pull", {body}, [&] { return pushPullFace(body, top, 3.0); });
        expectUntouched(name + " push", {body}, [&] { return pushPullFace(body, top, -2.0); });
        const int side = faceFacing(body, {1, 0, 0});
        ASSERT_GE(side, 0) << name;
        // (Refused on the plate: its side meets the rounded corners tangentially.)
        expectUntouched(name + " offset", {body}, [&] { return offsetFace(body, side, 2.0); }, Expect::Either);
        expectUntouched(name + " shell", {body}, [&] { return shell(body, {top}, 1.0); });
    }
    // Removing faces: the hole of the holed box, a rounded corner of the plate.
    const auto all = bodies();
    const Shape& holed = all[1].body;
    expectUntouched("holed box delete hole", {holed},
                    [&] { return deleteFaces(holed, {firstFaceOfKind(holed, SurfaceKind::Cylinder)}); });
    const Shape& plate = all[2].body;
    expectUntouched("STEP plate delete round", {plate},
                    [&] { return deleteFaces(plate, {firstFaceOfKind(plate, SurfaceKind::Cylinder)}); });
}

TEST(OpsInputsUntouched, EdgeOperations)
{
    for (const Inputs& in : bodies()) {
        const std::string& name = in.name;
        const Shape& body = in.body;
        // A top edge: every body here has straight edges around its top face.
        std::vector<int> edges;
        for (int i = 0; i < body.edgeCount() && edges.empty(); ++i) {
            const auto info = edgeInfo(body, i);
            if (info && info->kind == CurveKind::Line && std::abs(info->tangent.z) < 1e-9
                && std::abs(info->midpoint.z - approximateBoundingBox(body).max.z) < 1e-4)
                edges.push_back(i);
        }
        ASSERT_FALSE(edges.empty()) << name;
        expectUntouched(name + " fillet", {body}, [&] { return filletEdges(body, edges, 1.0); });
        expectUntouched(name + " chamfer", {body}, [&] { return chamferEdges(body, edges, 1.0); });
    }
}

TEST(OpsInputsUntouched, CopiesMirrorsHolesAndText)
{
    OS_REQUIRE_TEST_FONT("untouched-font");
    for (const Inputs& in : bodies()) {
        const std::string& name = in.name;
        const Shape& body = in.body;
        const auto b = approximateBoundingBox(body);
        // Joined mirror and repeats (the one-pass fuse).
        expectUntouched(name + " mirror", {body}, [&] { return mirrorJoined(body, {b.max.x, 0, 0}, {1, 0, 0}); });
        RigidMotion step;
        step.translation = {b.size().x * 0.5, 0, 0};
        expectUntouched(name + " repeat", {body}, [&] { return repeatJoined(body, {step}); });
        // A counterbored hole from the top, off the middle.
        HoleCut hole;
        hole.entry = {b.min.x + 6, b.center().y, b.max.z};
        hole.diameter = 3;
        hole.throughAll = true;
        hole.head = HoleHead::Counterbore;
        hole.headDiameter = 6;
        hole.headDepth = 1;
        expectUntouched(name + " hole", {body}, [&] { return drillHoles(body, {hole}); });
        // Letters raised from and cut into the top face.
        const TextSpec text{"Ab", "untouched-font", 5};
        const TextFrame frame{{b.center().x, b.center().y, b.max.z}, {1, 0, 0}, {0, 0, 1}};
        expectUntouched(name + " emboss", {body}, [&] { return embossText(body, text, frame, 0.5); });
        expectUntouched(name + " deboss", {body}, [&] { return embossText(body, text, frame, -0.5); });
    }
}

TEST(OpsInputsUntouched, DraftedExtrude)
{
    PlanarCurve c;
    c.kind = PlanarCurve::Kind::Circle;
    c.radius = 5;
    PlanarCurve s1, s2, s3, s4;
    s1.kind = s2.kind = s3.kind = s4.kind = PlanarCurve::Kind::Segment;
    s1.start = {-10, -10, 0}, s1.end = {10, -10, 0};
    s2.start = {10, -10, 0}, s2.end = {10, 10, 0};
    s3.start = {10, 10, 0}, s3.end = {-10, 10, 0};
    s4.start = {-10, 10, 0}, s4.end = {-10, -10, 0};
    auto regions = findRegions(PlaneFrame{}, {s1, s2, s3, s4, c});
    ASSERT_TRUE(regions.ok());
    std::vector<Shape> faces, ring;
    for (const auto& r : regions.value()) {
        faces.push_back(r.face);
        if (r.area > 300)
            ring.push_back(r.face);
    }
    ASSERT_EQ(faces.size(), 2u);
    ASSERT_EQ(ring.size(), 1u);
    expectUntouched("drafted extrude", ring, [&] { return extrudeFacesDrafted(ring, {0, 0, 8}, 5.0 * kPi / 180); });
    expectUntouched("straight extrude", faces, [&] { return extrudeFaces(faces, {0, 0, 8}); });
}
