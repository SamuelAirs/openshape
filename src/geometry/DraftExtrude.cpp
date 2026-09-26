// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrusions with drafted (tapered) side walls: a straight prism whose side
// faces are tilted about the profile's plane by BRepOffsetAPI_DraftAngle.

#include "core/Timer.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <cmath>

namespace os::geom {

using namespace detail;

namespace {

constexpr const char* kCollapsed =
    "The draft closes the shape before the full height. Use a smaller angle or a shorter extrusion.";

// One edge of a profile wire, walked in the wire's order and orientation
// (material on the left, seen from the face's normal).
struct WireEdge {
    bool line = false;
    double length = 0;       // lines
    double radius = 0;       // circles and arcs
    bool convex = false;     // circles and arcs: the center is on the material side
    Vec3 startTangent, endTangent;
};

std::vector<WireEdge> walk(const TopoDS_Wire& wire, const TopoDS_Face& face, const Vec3& normal)
{
    std::vector<WireEdge> out;
    for (BRepTools_WireExplorer ex(wire, face); ex.More(); ex.Next()) {
        const TopoDS_Edge& edge = ex.Current();
        BRepAdaptor_Curve curve(edge);
        const bool reversed = edge.Orientation() == TopAbs_REVERSED;
        const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
        auto tangentAt = [&](double t) {
            gp_Pnt p;
            gp_Vec d;
            curve.D1(t, p, d);
            Vec3 v = fromVec(d).normalized();
            return reversed ? v * -1.0 : v;
        };
        WireEdge e;
        e.startTangent = tangentAt(reversed ? t1 : t0);
        e.endTangent = tangentAt(reversed ? t0 : t1);
        if (curve.GetType() == GeomAbs_Line) {
            e.line = true;
            e.length = std::abs(t1 - t0);
        } else if (curve.GetType() == GeomAbs_Circle) {
            const gp_Circ c = curve.Circle();
            e.radius = c.Radius();
            gp_Pnt mid;
            gp_Vec d;
            curve.D1((t0 + t1) / 2, mid, d);
            Vec3 tangent = fromVec(d);
            if (reversed)
                tangent = tangent * -1.0;
            // Center on the left of the direction of travel = on the material's side.
            e.convex = tangent.cross(fromPnt(c.Location()) - fromPnt(mid)).dot(normal) > 0;
        } else {
            e.radius = -1; // other curves: left to the kernel's checks
        }
        out.push_back(e);
    }
    return out;
}

// Whether the profile survives an inward offset of `inset` (negative:
// outward) with every edge still there: the far end of a drafted extrusion
// is exactly that offset, with sharp corners. Lines shorten by
// inset * tan(turn / 2) at each corner (a square's sides by 2 x inset);
// circles and arcs around the material shrink by the inset, around holes
// they grow. (BRepOffsetAPI_MakeOffset would answer this too, but it crashed
// inside its medial-axis code on a square with a small round hole.)
Status survivesInset(const TopoDS_Face& face, double inset)
{
    BRepAdaptor_Surface surface(face);
    Vec3 normal = fromDir(surface.Plane().Axis().Direction());
    if (face.Orientation() == TopAbs_REVERSED)
        normal = normal * -1.0;
    auto collapsed = [](const std::string& why) {
        return Status::failure(ErrorCode::InvalidArgument, kCollapsed, "draft: " + why);
    };
    for (TopExp_Explorer wires(face, TopAbs_WIRE); wires.More(); wires.Next()) {
        const std::vector<WireEdge> edges = walk(TopoDS::Wire(wires.Current()), face, normal);
        const std::size_t n = edges.size();
        for (std::size_t i = 0; i < n; ++i) {
            const WireEdge& e = edges[i];
            if (!e.line) {
                if (e.radius > 0 && e.radius + (e.convex ? -inset : inset) <= 1e-6)
                    return collapsed("a round edge closes (radius " + std::to_string(e.radius) + ")");
                continue;
            }
            // The turns at this line's two corners (left turns are convex).
            auto turn = [&](const Vec3& a, const Vec3& b) { return std::atan2(a.cross(b).dot(normal), a.dot(b)); };
            const WireEdge& before = edges[(i + n - 1) % n];
            const WireEdge& after = edges[(i + 1) % n];
            const double atStart = n > 1 ? turn(before.endTangent, e.startTangent) : 0.0;
            const double atEnd = n > 1 ? turn(e.endTangent, after.startTangent) : 0.0;
            const double length = e.length - inset * (std::tan(atStart / 2) + std::tan(atEnd / 2));
            if (!(length > 1e-6))
                return collapsed("a straight edge closes (length " + std::to_string(length) + ")");
        }
    }
    return okStatus();
}

Result<TopoDS_Shape> draftedPrism(const TopoDS_Face& face, const Vec3& vector, double angle)
{
    using R = Result<TopoDS_Shape>;
    const char* userMessage = "Unable to extrude this shape with a draft.";
    BRepPrimAPI_MakePrism prism(face, toVec(vector));
    prism.Build();
    if (!prism.IsDone())
        return R::failure(ErrorCode::KernelFailure, userMessage, "draft: prism not done");
    const TopoDS_Shape bottom = prism.FirstShape();
    const TopoDS_Shape top = prism.LastShape();
    BRepAdaptor_Surface base(face);
    if (base.GetType() != GeomAbs_Plane)
        return R::failure(ErrorCode::NotPlanar, userMessage, "draft: profile not planar");
    const gp_Pln neutral = base.Plane();
    const gp_Dir pull = toDir(vector.normalized());
    BRepOffsetAPI_DraftAngle draft(prism.Shape());
    for (TopExp_Explorer ex(prism.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face side = TopoDS::Face(ex.Current());
        if (side.IsSame(bottom) || side.IsSame(top))
            continue;
        draft.Add(side, pull, angle, neutral);
        if (!draft.AddDone())
            return R::failure(ErrorCode::KernelFailure, userMessage, "draft: a side face cannot be drafted");
    }
    draft.Build();
    if (!draft.IsDone())
        return R::failure(ErrorCode::KernelFailure, userMessage, "draft: BRepOffsetAPI_DraftAngle not done");
    TopoDS_Shape solid = draft.Shape();
    for (TopExp_Explorer ex(solid, TopAbs_SOLID); ex.More(); ex.Next()) {
        TopoDS_Solid s = TopoDS::Solid(ex.Current());
        BRepLib::OrientClosedSolid(s);
        return R::success(s);
    }
    return R::failure(ErrorCode::KernelFailure, userMessage, "draft: no solid");
}

} // namespace

Result<Shape> extrudeFacesDrafted(const std::vector<Shape>& faces, const Vec3& vector, double draftAngle)
{
    if (std::abs(draftAngle) < 1e-12)
        return extrudeFaces(faces, vector);
    using R = Result<Shape>;
    if (!std::isfinite(draftAngle) || std::abs(draftAngle) > 89.0 * kPi / 180.0)
        return R::failure(ErrorCode::InvalidArgument, "The draft must be between -89\xC2\xB0 and 89\xC2\xB0.",
                          "extrudeFacesDrafted: angle out of range");
    if (faces.empty())
        return R::failure(ErrorCode::InvalidArgument, "Select a closed shape to extrude.", "extrudeFacesDrafted: no faces");
    const double height = vector.length();
    if (height < kMinLength)
        return R::failure(ErrorCode::InvalidArgument, "The extrusion distance must not be zero.", "extrudeFacesDrafted: zero vector");
    const char* userMessage = "Unable to extrude this shape with a draft.";
    return guarded("extrudeFacesDrafted", userMessage, [&]() -> R {
        ScopedTimer timer("extrudeFacesDrafted");
        const double inset = height * std::tan(draftAngle);
        TopTools_ListOfShape solids;
        double straightVolume = 0;
        for (const Shape& f : faces) {
            const TopoDS_Face face = TopoDS::Face(occ(f));
            if (Status s = survivesInset(face, inset); !s)
                return R::failure(s.error(), s.userMessage(), s.developerMessage());
            auto solid = draftedPrism(face, vector, draftAngle);
            if (!solid)
                return R::failureFrom(solid);
            solids.Append(solid.value());
            auto straight = extrudeFaces({f}, vector);
            if (straight)
                straightVolume += volume(straight.value());
        }
        TopoDS_Shape result;
        if (solids.Extent() == 1) {
            result = solids.First();
        } else {
            BRepAlgoAPI_Fuse fuse;
            TopTools_ListOfShape arguments, tools;
            arguments.Append(solids.First());
            for (auto it = std::next(solids.begin()); it != solids.end(); ++it)
                tools.Append(*it);
            fuse.SetArguments(arguments);
            fuse.SetTools(tools);
            fuse.Build();
            if (fuse.HasErrors() || !fuse.IsDone())
                return R::failure(ErrorCode::KernelFailure, userMessage, "draft: fuse failed: " + describeAlgoErrors(fuse));
            ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), true, true, true);
            unify.Build();
            result = unify.Shape();
        }
        auto finished = finishSolid(result, "extrudeFacesDrafted", userMessage);
        if (!finished)
            return finished;
        // What a draft must do: a positive one takes material away from the
        // straight extrusion, a negative one adds some; the length stays.
        const double v = volume(finished.value());
        if ((draftAngle > 0 && !(v < straightVolume)) || (draftAngle < 0 && !(v > straightVolume)) || !(v > 0))
            return R::failure(ErrorCode::InvalidResultShape, userMessage,
                              "draft: volume " + std::to_string(v) + " vs straight " + std::to_string(straightVolume));
        return finished;
    });
}

} // namespace os::geom
