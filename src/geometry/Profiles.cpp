#include "geometry/Profiles.h"

#include "core/Timer.h"
#include "geometry/Tessellation.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <gp_Ax1.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <limits>

namespace os::geom {

using namespace detail;

namespace {

// Rebuilds a region face from its closed wires only, dropping dangling
// (INTERNAL) edges that the splitter leaves inside regions. Extruding a face
// with internal edges would create internal faces in the solid.
TopoDS_Face cleanFace(const TopoDS_Face& face)
{
    bool hasInternal = false;
    for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next())
        if (ex.Current().Orientation() == TopAbs_INTERNAL || ex.Current().Orientation() == TopAbs_EXTERNAL)
            hasInternal = true;
    if (!hasInternal)
        return face;

    const TopoDS_Wire outer = BRepTools::OuterWire(face);
    BRepBuilderAPI_MakeFace maker(BRep_Tool::Surface(face), outer, true);
    for (TopExp_Explorer ex(face, TopAbs_WIRE); ex.More(); ex.Next()) {
        const TopoDS_Wire wire = TopoDS::Wire(ex.Current());
        if (wire.IsSame(outer))
            continue;
        bool clean = wire.Orientation() != TopAbs_INTERNAL && wire.Orientation() != TopAbs_EXTERNAL;
        for (TopExp_Explorer ee(wire, TopAbs_EDGE); clean && ee.More(); ee.Next())
            if (ee.Current().Orientation() == TopAbs_INTERNAL || ee.Current().Orientation() == TopAbs_EXTERNAL)
                clean = false;
        if (clean && BRep_Tool::IsClosed(wire))
            maker.Add(wire);
    }
    if (!maker.IsDone())
        return face;
    TopoDS_Face rebuilt = maker.Face();
    // The outer wire itself may carry internal edges (a line poking into the region).
    bool outerHasInternal = false;
    for (TopExp_Explorer ee(outer, TopAbs_EDGE); ee.More(); ee.Next())
        if (ee.Current().Orientation() == TopAbs_INTERNAL || ee.Current().Orientation() == TopAbs_EXTERNAL)
            outerHasInternal = true;
    if (outerHasInternal) {
        // TODO(OpenShape-M3): rebuild the outer wire without its dangling edges.
        OS_LOG(Debug, Sketch) << "region outer wire contains dangling edges; keeping splitter face";
        return face;
    }
    return rebuilt;
}

// Fuses many solids in one General Fuse run (one argument, the rest as tools).
// Fusing pairwise is quadratic: 100 hole cylinders took ~0.7 s that way.
TopoDS_Shape fuseAll(const TopTools_ListOfShape& solids, std::string& error)
{
    if (solids.IsEmpty())
        return {};
    if (solids.Extent() == 1)
        return solids.First();
    TopTools_ListOfShape arguments, tools;
    arguments.Append(solids.First());
    bool first = true;
    for (const TopoDS_Shape& s : solids) {
        if (first) {
            first = false;
            continue;
        }
        tools.Append(s);
    }
    BRepAlgoAPI_Fuse fuse;
    fuse.SetArguments(arguments);
    fuse.SetTools(tools);
    fuse.Build();
    if (fuse.HasErrors()) {
        error = describeAlgoErrors(fuse);
        return {};
    }
    return fuse.Shape();
}

// A point guaranteed to be inside the face: centroid of its largest triangle.
Vec3 interiorPoint(const Shape& face, const Vec3& fallback)
{
    const Mesh mesh = tessellate(face);
    double bestArea = -1;
    Vec3 best = fallback;
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const Vec3 a = mesh.vertex(mesh.indices[3 * t]);
        const Vec3 b = mesh.vertex(mesh.indices[3 * t + 1]);
        const Vec3 c = mesh.vertex(mesh.indices[3 * t + 2]);
        const double area = (b - a).cross(c - a).length();
        if (area > bestArea) {
            bestArea = area;
            best = (a + b + c) / 3.0;
        }
    }
    return best;
}

} // namespace

Result<std::vector<Region>> findRegions(const PlaneFrame& plane, const std::vector<PlanarCurve>& curves)
{
    using R = Result<std::vector<Region>>;
    std::vector<Region> regions;
    if (curves.empty())
        return R::success(std::move(regions));

    ScopedTimer timer("findRegions");
    try {
        const Vec3 n = plane.normal().normalized();
        const gp_Ax3 axes(toPnt(plane.origin), toDir(n), toDir(plane.xAxis));

        // Plane-local extents of all curves.
        double umin = std::numeric_limits<double>::max(), vmin = umin;
        double umax = -umin, vmax = -umin;
        auto extend = [&](const Vec3& p, double pad) {
            const Vec3 d = p - plane.origin;
            const double u = d.dot(plane.xAxis), v = d.dot(plane.yAxis);
            umin = std::min(umin, u - pad);
            umax = std::max(umax, u + pad);
            vmin = std::min(vmin, v - pad);
            vmax = std::max(vmax, v + pad);
        };

        TopTools_ListOfShape tools;
        for (const auto& c : curves) {
            if (c.kind == PlanarCurve::Kind::Segment) {
                if ((c.end - c.start).length() < kMinLength)
                    continue;
                tools.Append(BRepBuilderAPI_MakeEdge(toPnt(c.start), toPnt(c.end)).Edge());
                extend(c.start, 0);
                extend(c.end, 0);
            } else if (c.kind == PlanarCurve::Kind::Arc) {
                if (c.radius < kMinLength || (c.end - c.start).length() < kMinLength)
                    continue;
                const gp_Circ circle(gp_Ax2(toPnt(c.center), toDir(n), toDir(plane.xAxis)), c.radius);
                tools.Append(BRepBuilderAPI_MakeEdge(circle, toPnt(c.start), toPnt(c.end)).Edge());
                extend(c.center, c.radius);
            } else {
                if (c.radius < kMinLength)
                    continue;
                const gp_Circ circle(gp_Ax2(toPnt(c.center), toDir(n), toDir(plane.xAxis)), c.radius);
                tools.Append(BRepBuilderAPI_MakeEdge(circle).Edge());
                extend(c.center, c.radius);
            }
        }
        if (tools.IsEmpty())
            return R::success(std::move(regions));

        const double margin = std::max({umax - umin, vmax - vmin, 1.0});
        const double u0 = umin - margin, u1 = umax + margin, v0 = vmin - margin, v1 = vmax + margin;
        const TopoDS_Face base = BRepBuilderAPI_MakeFace(gp_Pln(axes), u0, u1, v0, v1).Face();

        BRepAlgoAPI_Splitter splitter;
        TopTools_ListOfShape arguments;
        arguments.Append(base);
        splitter.SetArguments(arguments);
        splitter.SetTools(tools);
        splitter.Build();
        if (splitter.HasErrors())
            return R::failure(ErrorCode::KernelFailure, "Unable to find closed shapes in this sketch.",
                              "BRepAlgoAPI_Splitter failed: " + describeAlgoErrors(splitter));

        const double eps = margin * 1e-6 + 1e-7;
        for (TopExp_Explorer ex(splitter.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face face = TopoDS::Face(ex.Current());
            // The unbounded remainder touches the base face's border: not a profile.
            bool touchesBorder = false;
            for (TopExp_Explorer vx(face, TopAbs_VERTEX); vx.More() && !touchesBorder; vx.Next()) {
                const Vec3 p = fromPnt(BRep_Tool::Pnt(TopoDS::Vertex(vx.Current())));
                const Vec3 d = p - plane.origin;
                const double u = d.dot(plane.xAxis), v = d.dot(plane.yAxis);
                touchesBorder = std::abs(u - u0) < eps || std::abs(u - u1) < eps || std::abs(v - v0) < eps
                             || std::abs(v - v1) < eps;
            }
            if (touchesBorder)
                continue;

            Region region;
            region.face = makeShape(cleanFace(face));
            GProp_GProps props;
            BRepGProp::SurfaceProperties(occ(region.face), props);
            region.area = props.Mass();
            region.centroid = fromPnt(props.CentreOfMass());
            if (region.area < kMinLength * kMinLength)
                continue;
            region.interiorPoint = interiorPoint(region.face, region.centroid);
            regions.push_back(std::move(region));
        }
    } catch (const Standard_Failure& failure) {
        return R::failure(ErrorCode::KernelFailure, "Unable to find closed shapes in this sketch.",
                          "findRegions threw " + describeFailure(failure));
    }

    std::sort(regions.begin(), regions.end(), [](const Region& a, const Region& b) { return a.area > b.area; });
    return R::success(std::move(regions));
}

bool regionContains(const Shape& face, const Vec3& point)
{
    if (face.isNull())
        return false;
    try {
        TopExp_Explorer ex(occ(face), TopAbs_FACE);
        if (!ex.More())
            return false;
        BRepClass_FaceClassifier classifier(TopoDS::Face(ex.Current()), toPnt(point), 1e-6);
        return classifier.State() == TopAbs_IN;
    } catch (const Standard_Failure&) {
        return false;
    }
}

Result<Shape> extrudeFaces(const std::vector<Shape>& faces, const Vec3& vector)
{
    if (faces.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select a closed shape to extrude.", "extrudeFaces: no faces");
    if (vector.length() < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The extrusion distance must not be zero.",
                                      "extrudeFaces: zero vector");
    const char* userMessage = "Unable to extrude this shape.";
    return guarded("extrudeFaces", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("extrudeFaces");
        TopoDS_Shape result;
        TopTools_ListOfShape solids;
        for (const Shape& face : faces) {
            BRepPrimAPI_MakePrism prism(occ(face), toVec(vector));
            prism.Build();
            if (!prism.IsDone())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "BRepPrimAPI_MakePrism not done");
            TopoDS_Shape solid = prism.Shape();
            for (TopExp_Explorer ex(solid, TopAbs_SOLID); ex.More(); ex.Next()) {
                TopoDS_Solid s = TopoDS::Solid(ex.Current());
                BRepLib::OrientClosedSolid(s);
                solid = s;
                break;
            }
            solids.Append(solid);
        }
        std::string fuseError;
        result = fuseAll(solids, fuseError);
        if (result.IsNull())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Fuse failed: " + fuseError);
        if (faces.size() > 1) {
            ShapeUpgrade_UnifySameDomain unify(result, true, true, true);
            unify.Build();
            result = unify.Shape();
        }
        return finishSolid(result, "extrudeFaces", userMessage);
    });
}

Result<Shape> revolveFaces(const std::vector<Shape>& faces, const Vec3& axisOrigin, const Vec3& axisDirection,
                           double angle)
{
    if (faces.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select a closed shape to revolve.", "revolveFaces: no faces");
    if (!(angle > 1e-9) || angle > 2 * kPi + 1e-9)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The angle must be between 0\xC2\xB0 and 360\xC2\xB0.",
                                      "revolveFaces: angle " + std::to_string(angle));
    const Vec3 axis = axisDirection.normalized();
    // A profile on both sides of the axis would sweep through itself.
    for (const Shape& face : faces) {
        const Mesh mesh = tessellate(face);
        double lo = 0, hi = 0;
        bool first = true;
        Vec3 normal;
        for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
            const Vec3 d = mesh.vertex(i) - axisOrigin;
            const Vec3 c = axis.cross(d);
            if (first && c.length() > 1e-9) {
                normal = c.normalized();
                first = false;
            }
            const double side = first ? 0.0 : c.dot(normal);
            lo = std::min(lo, side);
            hi = std::max(hi, side);
        }
        if (lo < -1e-6 && hi > 1e-6)
            return Result<Shape>::failure(ErrorCode::InvalidArgument,
                                          "The shape crosses the revolve axis. Draw it on one side of the axis.",
                                          "revolveFaces: profile straddles the axis");
    }
    const char* userMessage = "Unable to revolve this shape.";
    return guarded("revolveFaces", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("revolveFaces");
        const gp_Ax1 ax(toPnt(axisOrigin), toDir(axis));
        TopoDS_Shape result;
        TopTools_ListOfShape solids;
        for (const Shape& face : faces) {
            BRepPrimAPI_MakeRevol revol(occ(face), ax, std::min(angle, 2 * kPi));
            revol.Build();
            if (!revol.IsDone())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "BRepPrimAPI_MakeRevol not done");
            TopoDS_Shape solid = revol.Shape();
            for (TopExp_Explorer ex(solid, TopAbs_SOLID); ex.More(); ex.Next()) {
                TopoDS_Solid s = TopoDS::Solid(ex.Current());
                BRepLib::OrientClosedSolid(s);
                solid = s;
                break;
            }
            solids.Append(solid);
        }
        std::string fuseError;
        result = fuseAll(solids, fuseError);
        if (result.IsNull())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Fuse failed: " + fuseError);
        return finishSolid(result, "revolveFaces", userMessage);
    });
}

} // namespace os::geom
