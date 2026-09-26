// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Holes.h"

#include "core/Timer.h"
#include "geometry/Modeling.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <ElSLib.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <Precision.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>

namespace os::geom {

using namespace detail;

namespace {

// The tools start this far outside the surface so the cut opens cleanly.
constexpr double kLead = 0.05;

double frustumVolume(double r1, double r2, double height)
{
    return kPi * height / 3.0 * (r1 * r1 + r1 * r2 + r2 * r2);
}

// A tool solid and its volume (for the upper bound of what the cut may remove).
struct Tool {
    TopoDS_Shape shape;
    double volume = 0;
};

Result<Tool> cylinderTool(const Vec3& base, const Vec3& direction, double radius, double length)
{
    BRepPrimAPI_MakeCylinder maker(gp_Ax2(toPnt(base), toDir(direction)), radius, length);
    maker.Build();
    if (!maker.IsDone())
        return Result<Tool>::failure(ErrorCode::KernelFailure, "Unable to drill this hole.", "hole: cylinder not done");
    return Result<Tool>::success(Tool{maker.Shape(), kPi * radius * radius * length});
}

Result<Tool> coneTool(const Vec3& base, const Vec3& direction, double baseRadius, double endRadius, double length)
{
    BRepPrimAPI_MakeCone maker(gp_Ax2(toPnt(base), toDir(direction)), baseRadius, endRadius, length);
    maker.Build();
    if (!maker.IsDone())
        return Result<Tool>::failure(ErrorCode::KernelFailure, "Unable to drill this hole.", "hole: cone not done");
    return Result<Tool>::success(Tool{maker.Shape(), frustumVolume(baseRadius, endRadius, length)});
}

// Why a hole cannot be made at all (before any kernel call); "" = fine.
std::string checkHole(const HoleCut& h)
{
    const double d = h.diameter;
    if (!(d > kMinLength) || !std::isfinite(d))
        return "The hole diameter must be greater than zero.";
    if (h.drillShaft && !h.throughAll && (!(h.depth > kMinLength) || !std::isfinite(h.depth)))
        return "The hole depth must be greater than zero.";
    const bool blind = h.drillShaft && !h.throughAll;
    switch (h.head) {
    case HoleHead::None: break;
    case HoleHead::Counterbore:
        if (!(h.headDiameter > d + 1e-6) || !std::isfinite(h.headDiameter))
            return "The counterbore must be wider than the hole.";
        if (!(h.headDepth > kMinLength) || !std::isfinite(h.headDepth))
            return "The counterbore depth must be greater than zero.";
        if (blind && h.headDepth >= h.depth - 1e-6)
            return "The counterbore must be shallower than the hole.";
        break;
    case HoleHead::Countersink:
        if (!(h.headDiameter > d + 1e-6) || !std::isfinite(h.headDiameter))
            return "The countersink must be wider than the hole.";
        if (!(h.headAngle > 1e-3) || !(h.headAngle < kPi - 1e-3))
            return "The countersink angle must be between 0\xC2\xB0 and 180\xC2\xB0.";
        if (blind && countersinkDepth(h.headDiameter, d, h.headAngle) >= h.depth - 1e-6)
            return "The countersink must be shallower than the hole.";
        break;
    }
    return {};
}

// The tool solids of one hole.
Result<std::vector<Tool>> holeTools(const HoleCut& h, double throughLength)
{
    using R = Result<std::vector<Tool>>;
    std::vector<Tool> tools;
    const Vec3 dir = h.direction.normalized();
    const double r = h.diameter / 2;
    const Vec3 above = h.entry - dir * kLead;
    const double reach = headReach(h);
    const double shaftEnd = h.throughAll ? throughLength : h.depth;
    switch (h.head) {
    case HoleHead::None: break;
    case HoleHead::Counterbore: {
        auto tool = cylinderTool(above, dir, h.headDiameter / 2, h.headDepth + kLead);
        if (!tool)
            return R::failureFrom(tool);
        tools.push_back(tool.value());
        break;
    }
    case HoleHead::Countersink: {
        // The cone runs on past the hole's wall (into the shaft, which is
        // empty) so the two meet in a clean circle, not edge on face.
        const double t = std::tan(h.headAngle / 2);
        const double past = h.drillShaft && !h.throughAll ? std::min(0.5 * r / t, 0.5 * (shaftEnd - reach)) : 0.5 * r / t;
        const double end = reach + past;
        const double R0 = h.headDiameter / 2;
        auto tool = coneTool(above, dir, R0 + kLead * t, R0 - end * t, end + kLead);
        if (!tool)
            return R::failureFrom(tool);
        tools.push_back(tool.value());
        break;
    }
    }
    if (h.drillShaft) {
        // With a head, the shaft starts inside it (no coincident end faces).
        const double start = h.head == HoleHead::None ? -kLead : reach / 2;
        auto tool = cylinderTool(h.entry + dir * start, dir, r, shaftEnd - start);
        if (!tool)
            return R::failureFrom(tool);
        tools.push_back(tool.value());
    }
    return R::success(std::move(tools));
}

} // namespace

double countersinkDepth(double headDiameter, double holeDiameter, double angle)
{
    return (headDiameter - holeDiameter) / 2 / std::tan(angle / 2);
}

double headReach(const HoleCut& hole)
{
    switch (hole.head) {
    case HoleHead::None: return 0;
    case HoleHead::Counterbore: return hole.headDepth;
    case HoleHead::Countersink: return countersinkDepth(hole.headDiameter, hole.diameter, hole.headAngle);
    }
    return 0;
}

double headVolume(const HoleCut& hole)
{
    const double r = hole.diameter / 2, R0 = hole.headDiameter / 2, h = headReach(hole);
    switch (hole.head) {
    case HoleHead::None: return 0;
    case HoleHead::Counterbore: return kPi * (R0 * R0 - r * r) * h;
    case HoleHead::Countersink: return frustumVolume(R0, r, h) - kPi * r * r * h;
    }
    return 0;
}

std::optional<double> materialDepth(const Shape& shape, const Vec3& point, const Vec3& direction)
{
    if (shape.isNull() || direction.length() < 1e-12)
        return std::nullopt;
    try {
        // (BRepClass3d_SolidClassifier crashed inside Extrema on a plain
        // plate with a hole, so the side the line starts on is read from the
        // first face it meets instead: leaving the material means it started
        // in it.)
        const Vec3 d = direction.normalized();
        constexpr double kStep = 1e-4;
        IntCurvesFace_ShapeIntersector intersector;
        intersector.Load(occ(shape), Precision::Confusion());
        intersector.Perform(gp_Lin(toPnt(point), toDir(d)), kStep, Precision::Infinite());
        if (!intersector.IsDone() || intersector.NbPnt() == 0)
            return std::nullopt;
        intersector.SortResult();
        const TopoDS_Face& face = intersector.Face(1);
        BRepAdaptor_Surface surface(face);
        gp_Pnt p;
        gp_Vec du, dv;
        surface.D1(intersector.UParameter(1), intersector.VParameter(1), p, du, dv);
        gp_Vec normal = du.Crossed(dv);
        if (normal.Magnitude() < 1e-12)
            return std::nullopt;
        if (face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        if (normal.Normalized().Dot(toVec(d)) < 1e-6)
            return std::nullopt; // the line enters the material there: it started outside
        return intersector.WParameter(1);
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "materialDepth failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

FaceOutline faceOutline(const Shape& shape, int faceIndex, const Vec3& origin, const Vec3& xAxis, const Vec3& yAxis)
{
    FaceOutline out;
    if (shape.isNull() || faceIndex < 0 || faceIndex >= shape.faceCount())
        return out;
    try {
        const TopoDS_Face face = TopoDS::Face(shape.data()->faces.FindKey(faceIndex + 1));
        // The face in the frame's coordinates: its box there is the outline's.
        const Vec3 n = xAxis.cross(yAxis);
        gp_Trsf toFrame;
        toFrame.SetTransformation(gp_Ax3(toPnt(origin), toDir(n), toDir(xAxis)));
        const TopoDS_Shape local = BRepBuilderAPI_Transform(face, toFrame, false).Shape();
        Bnd_Box box;
        BRepBndLib::AddOptimal(local, box, false, false);
        if (box.IsVoid())
            return out;
        double zMin = 0, zMax = 0;
        box.Get(out.minU, out.minV, zMin, out.maxU, out.maxV, zMax);
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(face, TopAbs_EDGE, edges);
        for (int i = 1; i <= edges.Extent(); ++i) {
            const int index = shape.data()->edges.FindIndex(edges(i)) - 1;
            const auto info = edgeInfo(shape, index);
            if (!info)
                continue;
            if (info->kind == CurveKind::Line)
                out.edgeMidpoints.push_back(info->midpoint);
            else if (info->kind == CurveKind::Circle)
                out.circleCenters.push_back(info->center);
        }
        out.valid = true;
        return out;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "faceOutline(" << faceIndex << ") failed: " << describeFailure(failure);
        return FaceOutline{};
    }
}

bool faceContains(const Shape& shape, int faceIndex, const Vec3& point)
{
    if (shape.isNull() || faceIndex < 0 || faceIndex >= shape.faceCount())
        return false;
    try {
        const TopoDS_Face face = TopoDS::Face(shape.data()->faces.FindKey(faceIndex + 1));
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            return false;
        double u = 0, v = 0;
        ElSLib::Parameters(surface.Plane(), toPnt(point), u, v);
        BRepClass_FaceClassifier classifier(face, gp_Pnt2d(u, v), BRep_Tool::Tolerance(face) * 10);
        return classifier.State() == TopAbs_IN || classifier.State() == TopAbs_ON;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "faceContains(" << faceIndex << ") failed: " << describeFailure(failure);
        return false;
    }
}

Result<Shape> drillHoles(const Shape& shape, const std::vector<HoleCut>& holes)
{
    using R = Result<Shape>;
    if (shape.isNull())
        return R::failure(ErrorCode::InvalidArgument, "There is no body to drill.", "drillHoles: null shape");
    if (holes.empty())
        return R::failure(ErrorCode::InvalidArgument, "Place at least one hole.", "drillHoles: no holes");
    for (const HoleCut& h : holes)
        if (std::string why = checkHole(h); !why.empty())
            return R::failure(ErrorCode::InvalidArgument, why, "drillHoles: " + why);
    const char* userMessage = "Unable to drill this hole here.";
    return guarded("drillHoles", userMessage, [&]() -> R {
        ScopedTimer timer("drillHoles");
        const BoundingBox box = approximateBoundingBox(shape);
        TopTools_ListOfShape arguments, tools;
        arguments.Append(occ(shape));
        double toolVolume = 0;
        for (const HoleCut& h : holes) {
            // Through all: far enough to leave the body from anywhere.
            const double through = box.valid ? box.size().length() + (box.center() - h.entry).length() + 1.0 : 1000.0;
            auto pieces = holeTools(h, through);
            if (!pieces)
                return R::failureFrom(pieces);
            for (const Tool& t : pieces.value()) {
                tools.Append(t.shape);
                toolVolume += t.volume;
            }
        }
        BRepAlgoAPI_Cut cut;
        cut.SetArguments(arguments);
        cut.SetTools(tools);
        cut.Build();
        if (cut.HasErrors() || !cut.IsDone())
            return R::failure(ErrorCode::KernelFailure, userMessage, "drillHoles: cut failed: " + describeAlgoErrors(cut));
        ShapeUpgrade_UnifySameDomain unify(cut.Shape(), true, true, true);
        unify.Build();
        auto result = finishSolid(unify.Shape(), "drillHoles", userMessage);
        if (!result)
            return result;
        // What the cut must have done: take something away, and no more than
        // the tools hold.
        const double removed = volume(shape) - volume(result.value());
        if (removed <= 1e-9)
            return R::failure(ErrorCode::InvalidArgument, "The hole does not reach into the part here.",
                              "drillHoles: volume unchanged");
        if (removed > toolVolume * (1 + 1e-6) + 1e-6)
            return R::failure(ErrorCode::InvalidResultShape, userMessage,
                              "drillHoles: removed " + std::to_string(removed) + " > tools " + std::to_string(toolVolume));
        return result;
    });
}

} // namespace os::geom
