// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Shell's retries and the checks on what they build. OCCT 7.9.3 had never
// refused one of our bodies with C0 faces, so the retries (adapted from
// OpenShape3D) ran here first with the first attempt forced to count as
// refused (detail::setShellFirstAttemptRefusedForTesting): one body came
// back as a wrong shape reported as a success, another crashed inside
// OCCT. Now every success is measured and every failure is clean, a body
// with a real C0 face takes the split-at-C0 path, and two maker parts arc
// joins refuse shell through the newer retries.
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"

#include <gtest/gtest.h>

// OpenCASCADE, to build a body with a C0 face (as STEP files bring them)
// and to rebuild a wrong shell the old code returned.
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <ShapeFix_Shape.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace os;
using namespace os::geom;

namespace {

struct Candidate {
    std::string name;
    Shape body;
    Vec3 openNormal;
    double thickness = 1;
};

int faceFacing(const Shape& s, const Vec3& n)
{
    for (int i = 0; i < s.faceCount(); ++i) {
        const auto info = faceInfo(s, i);
        if (info && info->isPlanar() && info->normal.dot(n) > 0.9999)
            return i;
    }
    return -1;
}

// A 30 x 30 x 10 plate with a half sphere r 10 on top and a pipe r 2 at
// x = `px` standing out of the dome, the junction rounded with `r`.
std::optional<Shape> domeWithPipe(double px, double r)
{
    PlanarCurve c;
    c.kind = PlanarCurve::Kind::Circle;
    c.radius = 10;
    PlanarCurve axis;
    axis.kind = PlanarCurve::Kind::Segment;
    axis.start = {0, 0, -10};
    axis.end = {0, 0, 10};
    PlaneFrame xz;
    xz.xAxis = {1, 0, 0};
    xz.yAxis = {0, 0, 1};
    auto regions = findRegions(xz, {c, axis});
    if (!regions.ok())
        return std::nullopt;
    Shape half;
    for (const auto& region : regions.value())
        if (region.centroid.x > 0)
            half = region.face;
    const auto sphere = revolveFaces({half}, {0, 0, 0}, {0, 0, 1}, 2 * kPi);
    if (!sphere.ok())
        return std::nullopt;
    const Shape plate = makeBox({-15, -15, -10}, {30, 30, 10}).value();
    const Shape dome = booleanOp(plate, sphere.value(), BooleanKind::Union).value();
    const Shape pipe = makeCylinder({px, 0, 0}, {0, 0, 1}, 2, 15).value();
    const auto u = booleanOp(dome, pipe, BooleanKind::Union);
    if (!u.ok())
        return std::nullopt;
    std::vector<int> junction;
    for (int i = 0; i < u.value().edgeCount(); ++i) {
        const auto info = edgeInfo(u.value(), i);
        if (info && info->kind != CurveKind::Line && info->midpoint.z > 0.5 && info->midpoint.z < 14.5)
            junction.push_back(i);
    }
    const auto f = filletEdges(u.value(), junction, r);
    if (!f.ok())
        return std::nullopt;
    return f.value();
}

// A 40 x 30 x 20 box, one vertical edge rounded r 10, and a pipe r 4 along
// x entering it at height 10, y = `y` (where its side meets the rounding),
// the junction rounded with `r`.
std::optional<Shape> pipeAtRoundedCorner(double y, double r)
{
    const Shape b = makeBox({0, 0, 0}, {40, 30, 20}).value();
    int corner = -1;
    for (int i = 0; i < b.edgeCount(); ++i) {
        const auto info = edgeInfo(b, i);
        if (info && std::abs(info->midpoint.x - 40) < 1e-9 && std::abs(info->midpoint.y - 30) < 1e-9)
            corner = i;
    }
    const Shape rb = filletEdges(b, {corner}, 10).value();
    const Shape pipe = makeCylinder({30, y, 10}, {1, 0, 0}, 4, 25).value();
    const auto u = booleanOp(rb, pipe, BooleanKind::Union);
    if (!u.ok())
        return std::nullopt;
    std::vector<int> chain;
    for (int i = 0; i < u.value().edgeCount(); ++i) {
        const auto info = edgeInfo(u.value(), i);
        if (info && info->kind != CurveKind::Line && info->midpoint.x > 39 && info->midpoint.x < 43
            && std::abs(info->midpoint.z - 10) < 4.5)
            chain.push_back(i);
    }
    const auto f = filletEdges(u.value(), chain, r);
    if (!f.ok())
        return std::nullopt;
    return f.value();
}

// A T junction of two cylinders, the saddle rounded r 2.
std::optional<Shape> filletedT()
{
    const Shape v = makeCylinder({0, 0, 0}, {0, 0, 1}, 10, 30).value();
    const Shape h = makeCylinder({0, 0, 15}, {1, 0, 0}, 5, 25).value();
    const Shape t = booleanOp(v, h, BooleanKind::Union).value();
    std::vector<int> saddle;
    for (int i = 0; i < t.edgeCount(); ++i) {
        const auto info = edgeInfo(t, i);
        if (info && info->kind != CurveKind::Line && info->kind != CurveKind::Circle)
            saddle.push_back(i);
    }
    const auto f = filletEdges(t, saddle, 2.0);
    if (!f.ok())
        return std::nullopt;
    return f.value();
}

// A 40 x 30 x 20 box, its vertical edges rounded r 5, then the top loop r 2.
std::optional<Shape> roundedBox()
{
    const Shape b = makeBox({0, 0, 0}, {40, 30, 20}).value();
    std::vector<int> vertical;
    for (int i = 0; i < b.edgeCount(); ++i) {
        const auto info = edgeInfo(b, i);
        if (info && std::abs(std::abs(info->tangent.z) - 1) < 1e-9)
            vertical.push_back(i);
    }
    const Shape r1 = filletEdges(b, vertical, 5).value();
    std::vector<int> top;
    for (int i = 0; i < r1.edgeCount(); ++i) {
        const auto info = edgeInfo(r1, i);
        if (info && std::abs(info->midpoint.z - 20) < 1e-6)
            top.push_back(i);
    }
    const auto r2 = filletEdges(r1, top, 2);
    if (!r2.ok())
        return std::nullopt;
    return r2.value();
}

// A 40 x 30 x 20 box with a cylinder boss r 8 at x = 40 (half outside the
// box, 30 tall), the circle edges at z = 20 rounded r 2.
std::optional<Shape> bossBox()
{
    const Shape b = makeBox({0, 0, 0}, {40, 30, 20}).value();
    const Shape c = makeCylinder({40, 15, 0}, {0, 0, 1}, 8, 30).value();
    const Shape u = booleanOp(b, c, BooleanKind::Union).value();
    std::vector<int> junction;
    for (int i = 0; i < u.edgeCount(); ++i) {
        const auto info = edgeInfo(u, i);
        if (info && info->kind == CurveKind::Circle && std::abs(info->midpoint.z - 20) < 1e-6)
            junction.push_back(i);
    }
    const auto f = filletEdges(u, junction, 2);
    if (!f.ok())
        return std::nullopt;
    return f.value();
}

// A 40 x 30 x 20 box with a half-buried horizontal cylinder r 6 on top
// (axis along x), its edges at z = 20 rounded r 1.5.
std::optional<Shape> bumpBox()
{
    const Shape b = makeBox({0, 0, 0}, {40, 30, 20}).value();
    const Shape c = makeCylinder({5, 15, 20}, {1, 0, 0}, 6, 30).value();
    const Shape u = booleanOp(b, c, BooleanKind::Union).value();
    std::vector<int> all;
    for (int i = 0; i < u.edgeCount(); ++i) {
        const auto info = edgeInfo(u, i);
        if (info && info->midpoint.z > 19.9 && info->midpoint.z < 20.1)
            all.push_back(i);
    }
    const auto f = filletEdges(u, all, 1.5);
    if (!f.ok())
        return std::nullopt;
    return f.value();
}

std::vector<Candidate> candidates()
{
    std::vector<Candidate> out;
    char name[64];
    for (double px : {3.0, 5.0, 7.0})
        for (double r : {0.5, 1.0, 2.0})
            if (const auto s = domeWithPipe(px, r)) {
                std::snprintf(name, sizeof name, "dome %g r %g", px, r);
                out.push_back({name, *s, {0, 0, -1}, 0.8});
            }
    for (double y : {18.0, 20.0, 22.0})
        for (double r : {0.5, 1.0, 2.0})
            if (const auto s = pipeAtRoundedCorner(y, r)) {
                std::snprintf(name, sizeof name, "pipe %g r %g", y, r);
                out.push_back({name, *s, {0, 0, -1}, 1.0});
            }
    if (const auto s = filletedT())
        out.push_back({"T filleted", *s, {0, 0, 1}, 1.0});
    if (const auto s = roundedBox())
        out.push_back({"rounded box", *s, {0, 0, -1}, 1.0});
    if (const auto s = bossBox())
        out.push_back({"boss box", *s, {0, 0, -1}, 1.0});
    if (const auto s = bumpBox())
        out.push_back({"bump box", *s, {0, 0, -1}, 1.0});
    return out;
}

// A 40 x 20 profile whose top is a degree-2 B-spline through (40, 20),
// (20, `kinkY`) and (0, 20), extruded 15 along z. With `c0`, the top is one
// curve with a full-multiplicity knot at (20, kinkY): its extrusion is a C0
// surface, as STEP files bring them. Without, the same curve as two
// B-splines meeting there: two C1 faces with an edge between them. The
// middle poles are (30, 28) and (10, 28), so the top is smooth at the knot
// only when kinkY is 28.
Shape kinkedBlock(double kinkY, bool c0)
{
    const gp_Pnt p[5] = {{40, 20, 0}, {30, 28, 0}, {20, kinkY, 0}, {10, 28, 0}, {0, 20, 0}};
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), gp_Pnt(40, 0, 0)));
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(40, 0, 0), p[0]));
    const auto bspline = [](const gp_Pnt* first, int count, const std::vector<double>& knotValues, const std::vector<int>& multiplicities) {
        TColgp_Array1OfPnt poles(1, count);
        for (int i = 0; i < count; ++i)
            poles(i + 1) = first[i];
        TColStd_Array1OfReal knots(1, static_cast<int>(knotValues.size()));
        TColStd_Array1OfInteger mults(1, static_cast<int>(knotValues.size()));
        for (std::size_t i = 0; i < knotValues.size(); ++i) {
            knots(static_cast<int>(i) + 1) = knotValues[i];
            mults(static_cast<int>(i) + 1) = multiplicities[i];
        }
        return Handle(Geom_BSplineCurve)(new Geom_BSplineCurve(poles, knots, mults, 2));
    };
    if (c0) {
        wire.Add(BRepBuilderAPI_MakeEdge(bspline(p, 5, {0, 1, 2}, {3, 2, 3})));
    } else {
        wire.Add(BRepBuilderAPI_MakeEdge(bspline(p, 3, {0, 1}, {3, 3})));
        wire.Add(BRepBuilderAPI_MakeEdge(bspline(p + 2, 3, {1, 2}, {3, 3})));
    }
    wire.Add(BRepBuilderAPI_MakeEdge(p[4], gp_Pnt(0, 0, 0)));
    const TopoDS_Shape solid = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(), gp_Vec(0, 0, 15)).Shape();
    std::ostringstream text;
    BRepTools::Write(solid, text);
    return fromBrepString(text.str()).value();
}

// The bump box with only the chords rounded r 1.5 (the edges where the
// cylinder's end caps meet the box's top). OCCT refuses to shell it at
// every wall thickness tried; its intersection joins build a solid that
// keeps far too much material.
Shape chordBox()
{
    const Shape b = makeBox({0, 0, 0}, {40, 30, 20}).value();
    const Shape c = makeCylinder({5, 15, 20}, {1, 0, 0}, 6, 30).value();
    const Shape u = booleanOp(b, c, BooleanKind::Union).value();
    std::vector<int> chords;
    for (int i = 0; i < u.edgeCount(); ++i) {
        const auto info = edgeInfo(u, i);
        if (info && std::abs(info->midpoint.z - 20) < 0.1
            && (std::abs(info->midpoint.x - 5) < 1e-6 || std::abs(info->midpoint.x - 35) < 1e-6))
            chords.push_back(i);
    }
    return filletEdges(u, chords, 1.5).value();
}

// The shape as BRep text without its TShape flag lines (as in
// test_ops_inputs_untouched.cpp): the checker's "checked" flag is not
// geometry.
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

// Shell with the first attempt counted as refused for C0 faces (the
// retries run), on this thread only.
Result<Shape> forcedShell(const Shape& shape, const std::vector<int>& openFaces, double thickness)
{
    detail::setShellFirstAttemptRefusedForTesting(true);
    auto result = shell(shape, openFaces, thickness);
    detail::setShellFirstAttemptRefusedForTesting(false);
    return result;
}

// A failure that ends a shell cleanly: the shell's own refusal with a
// message, no kernel crash behind it.
void expectCleanFailure(const Result<Shape>& r, const std::string& name)
{
    EXPECT_FALSE(r.ok()) << name;
    EXPECT_EQ(r.error(), ErrorCode::ShellTooThick) << name << ": " << r.developerMessage();
    EXPECT_FALSE(r.userMessage().empty()) << name;
    EXPECT_EQ(r.developerMessage().find("ACCESS_VIOLATION"), std::string::npos) << name << ": " << r.developerMessage();
}

// A shell result with the walls asked for: smaller than the body, the same
// outside (bounding box), and the walls measured (detail::shellWallProblem).
void expectRightWalls(const Shape& body, int openFace, const Shape& hollow, double thickness, const std::string& name)
{
    EXPECT_LT(volume(hollow), volume(body)) << name;
    const auto a = boundingBox(body), b = boundingBox(hollow);
    EXPECT_NEAR(a.size().x, b.size().x, 1e-3) << name;
    EXPECT_NEAR(a.size().y, b.size().y, 1e-3) << name;
    EXPECT_NEAR(a.size().z, b.size().z, 1e-3) << name;
    EXPECT_EQ(detail::shellWallProblem(body, {openFace}, hollow, thickness), "") << name;
}

TopoDS_Shape toOcct(const Shape& shape)
{
    std::istringstream in(toBrepString(shape, false));
    TopoDS_Shape s;
    BRep_Builder builder;
    BRepTools::Read(s, in, builder);
    return s;
}

Shape fromOcct(const TopoDS_Shape& shape)
{
    std::ostringstream text;
    BRepTools::Write(shape, text);
    return fromBrepString(text.str()).value();
}

} // namespace

// Every candidate shells on the normal path with the walls asked for, and
// with the first attempt forced to count as refused the retries either
// build the same body (within 1.5%: intersection joins keep the inner
// corners sharp where arc joins round them) or fail cleanly, leaving the
// body untouched. Before the checks, a dome came back as 5 faces of inner
// skin (3314 mm^3 instead of 1881) and another crashed inside OCCT.
TEST(ShellRetries, CandidatesShellNormallyAndForced)
{
    const auto all = candidates();
    ASSERT_EQ(all.size(), 21u); // the dome 3 r 1 rounding fails; every other body builds
    int forcedOk = 0;
    for (const Candidate& c : all) {
        const int open = faceFacing(c.body, c.openNormal);
        ASSERT_GE(open, 0) << c.name;
        const std::string before = snapshot(c.body);
        const auto normal = shell(c.body, {open}, c.thickness);
        ASSERT_TRUE(normal.ok()) << c.name << ": " << normal.developerMessage();
        expectRightWalls(c.body, open, normal.value(), c.thickness, c.name);

        const auto forced = forcedShell(c.body, {open}, c.thickness);
        if (forced.ok()) {
            ++forcedOk;
            EXPECT_NE(forced.developerMessage().find("shell retried: arc joins: error 3 (for testing)"), std::string::npos)
                << c.name << ": " << forced.developerMessage();
            expectRightWalls(c.body, open, forced.value(), c.thickness, c.name + " forced");
            EXPECT_NEAR(volume(forced.value()), volume(normal.value()), 0.015 * volume(normal.value())) << c.name;
        } else {
            expectCleanFailure(forced, c.name + " forced");
        }
        EXPECT_EQ(snapshot(c.body), before) << c.name;
    }
    EXPECT_GE(forcedOk, 15);
}

// Opened at its top (which the boss rises through), the boss box comes
// back from arc joins as a solid of two shells: the outside closed and the
// inner skin closed on its own, nothing opened. Intersection joins on a
// dome with a rounded pipe built the same kind of solid (the outside left
// open), and BRepCheck_Analyzer's solid check, classifying one shell
// against the other, crashed inside OCCT on it (an access violation in
// Extrema_ExtCC; caught there, but on this body it once ended the whole
// test process). The shells are now counted before anything classifies
// against them: a clean refusal, the body as it was, and shell still works
// on it afterwards.
TEST(ShellRetries, UnjoinedWallsAreRefusedBeforeTheChecker)
{
    const auto boss = bossBox();
    ASSERT_TRUE(boss);
    const std::string before = snapshot(*boss);
    const auto top = shell(*boss, {faceFacing(*boss, {0, 0, 1})}, 1.0);
    expectCleanFailure(top, "boss box top");
    EXPECT_NE(top.developerMessage().find("arc joins: a solid of 2 shells"), std::string::npos) << top.developerMessage();
    EXPECT_EQ(snapshot(*boss), before);
    const int bottom = faceFacing(*boss, {0, 0, -1});
    const auto again = shell(*boss, {bottom}, 1.0);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    expectRightWalls(*boss, bottom, again.value(), 1.0, "boss box afterwards");
}

// The wall check itself, on the wrong body the old code returned for the
// dome with the first attempt forced: intersection joins (outside open,
// inner skin apart) repaired by ShapeFix_Shape into 5 faces of inner skin,
// 3314 mm^3 where the shell has 1881.
TEST(ShellRetries, WallCheckRejectsTheRepairedInnerSkin)
{
    const auto dome = domeWithPipe(7, 2);
    ASSERT_TRUE(dome);
    const int open = faceFacing(*dome, {0, 0, -1});
    // As tryShell and finishSolid did: the offset on a copy, the repair on
    // a copy of its result.
    BRepBuilderAPI_Copy copier(toOcct(*dome), Standard_False, Standard_False);
    TopTools_ListOfShape openFaces;
    for (TopExp_Explorer ex(copier.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
        GProp_GProps props;
        BRepGProp::SurfaceProperties(ex.Current(), props);
        if (std::abs(props.CentreOfMass().Z() + 10) < 1e-6)
            openFaces.Append(ex.Current());
    }
    ASSERT_EQ(openFaces.Extent(), 1);
    BRepOffsetAPI_MakeThickSolid maker;
    maker.MakeThickSolidByJoin(copier.Shape(), openFaces, -0.8, 1e-3, BRepOffset_Skin, Standard_False, Standard_False,
                               GeomAbs_Intersection);
    ASSERT_TRUE(maker.IsDone());
    Handle(ShapeFix_Shape) fixer = new ShapeFix_Shape(BRepBuilderAPI_Copy(maker.Shape(), Standard_False, Standard_False).Shape());
    fixer->Perform();
    TopoDS_Shape solid;
    int solids = 0;
    for (TopExp_Explorer ex(fixer->Shape(), TopAbs_SOLID); ex.More(); ex.Next(), ++solids)
        solid = ex.Current();
    ASSERT_EQ(solids, 1);
    const Shape wrong = fromOcct(solid);
    EXPECT_EQ(wrong.faceCount(), 5);
    ASSERT_NEAR(volume(wrong), 3314.2, 1.0);
    const std::string problem = detail::shellWallProblem(*dome, {open}, wrong, 0.8);
    EXPECT_NE(problem.find("the outside lost faces"), std::string::npos) << problem;

    const auto right = shell(*dome, {open}, 0.8);
    ASSERT_TRUE(right.ok());
    EXPECT_NEAR(volume(right.value()), 1881.3, 0.5);
    EXPECT_EQ(detail::shellWallProblem(*dome, {open}, right.value(), 0.8), "");
}

// A natural body (no forcing) whose last retry, intersection joins, builds
// a solid that keeps far too much material: 5492 mm^3 where walls of 1 mm
// hold about 3939. The volume against the band between outside and inside
// rejects it, and the refusal sends the user to another opening, not to
// ever thinner walls (0.1 mm fails too).
TEST(ShellRetries, WallCheckRejectsIntersectionJoinsThatKeepTooMuch)
{
    const Shape body = chordBox();
    const int open = faceFacing(body, {0, 0, -1});
    const auto r = shell(body, {open}, 1.0);
    expectCleanFailure(r, "chord box");
    EXPECT_NE(r.developerMessage().find("intersection joins: wrong walls: volume"), std::string::npos) << r.developerMessage();
    EXPECT_EQ(r.userMessage(), "Unable to hollow this body with this wall thickness.");
    EXPECT_FALSE(shell(body, {open}, 0.1).ok());
    SizeAdvice advice;
    advice.suggest = true;
    const auto advised = shell(body, {open}, 1.0, advice);
    // The size search has a time budget (kSizeBudget): on a fast machine it
    // reaches 0.1 mm, which fails too, and says to open another face; on a
    // slow one (GitHub's macOS runner, 2026-10-31) it runs out first and the
    // plain message stays. Either is true; "Try thinner walls" never is.
    EXPECT_TRUE(advised.userMessage() == "Unable to hollow this body with these faces open. Try opening a different face."
                || advised.userMessage() == "Unable to hollow this body with this wall thickness.")
        << advised.userMessage();
    EXPECT_EQ(advised.userMessage().find("thinner"), std::string::npos) << advised.userMessage();
}

// A face that is really C0, as STEP files bring them: the extrusion of a
// B-spline with a full-multiplicity knot. OCCT refuses it (BRepOffset_C0Geometry,
// error 3), the body is split at the knot and shelled: the same volume as
// the same block built of two C1 faces, walls as asked. Before the fix the
// split body's floor (the opening) was handed to the offset as the old face,
// not the one with the split edges, and the offset sealed the inside.
TEST(ShellRetries, C0FaceIsSplitAndShelled)
{
    for (double kinkY : {24.0, 28.0, 32.0}) {
        const Shape c0 = kinkedBlock(kinkY, true);
        const Shape c1 = kinkedBlock(kinkY, false);
        ASSERT_EQ(c0.faceCount(), 6);
        ASSERT_EQ(c1.faceCount(), 7);
        ASSERT_NEAR(volume(c0), volume(c1), 1e-6 * volume(c1));
        const int open0 = faceFacing(c0, {0, 0, -1}), open1 = faceFacing(c1, {0, 0, -1});
        const auto split = shell(c0, {open0}, 1.0);
        ASSERT_TRUE(split.ok()) << kinkY << ": " << split.developerMessage();
        EXPECT_EQ(split.developerMessage(), "shell retried: arc joins: error 3; split at C0: done");
        const auto plain = shell(c1, {open1}, 1.0);
        ASSERT_TRUE(plain.ok()) << plain.developerMessage();
        EXPECT_EQ(plain.developerMessage(), "");
        EXPECT_NEAR(volume(split.value()), volume(plain.value()), 1e-6 * volume(plain.value())) << kinkY;
        expectRightWalls(c0, open0, split.value(), 1.0, "kinked block");
    }
}

// Two maker parts OCCT's arc joins refuse (error 1) at 1 mm: a box with a
// cylinder boss half outside it, the boss's top edges rounded (it shells
// once faces on one surface are merged), and a box with a half-buried
// cylinder on top, all its edges at the top rounded (it shells in OCCT's
// complete intersection mode). Before, the user was told "Try thinner
// walls" for both, though the bump box failed at every thickness.
TEST(ShellRetries, BossAndBumpBoxesShell)
{
    struct Body {
        const char* name;
        Shape shape;
        const char* retry;
    };
    const Body bodies[] = {{"boss box", *bossBox(), "merged faces: done"}, {"bump box", *bumpBox(), "complete intersection: done"}};
    for (const Body& b : bodies) {
        const int open = faceFacing(b.shape, {0, 0, -1});
        double previous = 0;
        for (double t : {0.5, 1.0, 2.0}) {
            const auto r = shell(b.shape, {open}, t);
            ASSERT_TRUE(r.ok()) << b.name << " " << t << ": " << r.developerMessage();
            if (t >= 1.0) {
                EXPECT_NE(r.developerMessage().find(b.retry), std::string::npos) << b.name << ": " << r.developerMessage();
            }
            expectRightWalls(b.shape, open, r.value(), t, b.name);
            // Material grows about linearly with the walls.
            const double v = volume(r.value());
            if (previous > 0) {
                EXPECT_GT(v, 1.8 * previous) << b.name << " " << t;
                EXPECT_LT(v, 2.0 * previous) << b.name << " " << t;
            }
            previous = v;
        }
    }
}

// The rounded box with 12 mm walls: arc joins fail (error 1) and the
// complete intersection mode builds it. Its roundings (r 5 and r 2) are
// smaller than the walls, so the inside is a plain box: 16 x 6 x 8 mm.
TEST(ShellRetries, ThickWallsInsideRoundingsAreExact)
{
    const auto box = roundedBox();
    ASSERT_TRUE(box);
    const int open = faceFacing(*box, {0, 0, -1});
    const auto r = shell(*box, {open}, 12.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    EXPECT_NE(r.developerMessage().find("complete intersection: done"), std::string::npos) << r.developerMessage();
    EXPECT_NEAR(volume(r.value()), volume(*box) - 16 * 6 * 8, 1e-3);
    expectRightWalls(*box, open, r.value(), 12.0, "rounded box");
}

// Refusals say what is known. Walls thicker than half the body's narrowest
// side meet for sure: "Try thinner walls". A body OCCT refuses at every
// thickness (the bump box opened at its side, the boss box at its top)
// never hears that; with the size search, it is sent to another opening.
TEST(ShellRetries, RefusalsAreHonest)
{
    const Shape cube = makeBox({0, 0, 0}, {20, 20, 20}).value();
    const auto thick = shell(cube, {faceFacing(cube, {0, 0, 1})}, 12.0);
    expectCleanFailure(thick, "cube");
    EXPECT_EQ(thick.userMessage(), "Unable to shell with this wall thickness. Try thinner walls.");

    SizeAdvice advice;
    advice.suggest = true;
    struct Case {
        const char* name;
        Shape shape;
        Vec3 open;
    };
    const Case cases[] = {{"bump box side", *bumpBox(), {-1, 0, 0}}, {"boss box top", *bossBox(), {0, 0, 1}}};
    for (const Case& c : cases) {
        const int open = faceFacing(c.shape, c.open);
        ASSERT_GE(open, 0) << c.name;
        EXPECT_FALSE(shell(c.shape, {open}, 0.1).ok()) << c.name;
        const auto plain = shell(c.shape, {open}, 1.0);
        expectCleanFailure(plain, c.name);
        EXPECT_EQ(plain.userMessage().find("thinner"), std::string::npos) << c.name << ": " << plain.userMessage();
        // As in WallCheckRejectsIntersectionJoinsThatKeepTooMuch: a slow
        // machine's size search may run out of time before 0.1 mm.
        const auto advised = shell(c.shape, {open}, 1.0, advice);
        EXPECT_TRUE(advised.userMessage() == "Unable to hollow this body with these faces open. Try opening a different face."
                    || advised.userMessage() == "Unable to hollow this body with this wall thickness.")
            << c.name << ": " << advised.userMessage();
        EXPECT_EQ(advised.userMessage().find("thinner"), std::string::npos) << c.name << ": " << advised.userMessage();
    }
}
