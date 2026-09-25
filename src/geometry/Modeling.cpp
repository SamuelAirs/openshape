#include "geometry/Modeling.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_Surface.hxx>
#include <Message_Report.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <sstream>

namespace os::geom {

namespace detail {

Result<Shape> finishSolid(const TopoDS_Shape& result, const char* operation, const char* userMessage)
{
    if (result.IsNull())
        return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, std::string(operation) + " returned a null shape");

    TopoDS_Shape out = result;
    int solidCount = 0;
    TopoDS_Shape lastSolid;
    for (TopExp_Explorer ex(result, TopAbs_SOLID); ex.More(); ex.Next()) {
        ++solidCount;
        lastSolid = ex.Current();
    }
    if (solidCount == 0)
        return Result<Shape>::failure(ErrorCode::EmptyResult, "This operation would remove the entire body.",
                                      std::string(operation) + " produced no solids");
    if (solidCount == 1)
        out = lastSolid;

    BRepCheck_Analyzer analyzer(out);
    if (!analyzer.IsValid()) {
        const std::string dev = std::string(operation) + " produced an invalid shape (BRepCheck_Analyzer failed)";
        OS_LOG(Error, Kernel) << dev;
        return Result<Shape>::failure(ErrorCode::InvalidResultShape, userMessage, dev);
    }

    std::vector<std::string> warnings;
    if (solidCount > 1)
        warnings.push_back("The result has " + std::to_string(solidCount) + " separate solids.");
    return Result<Shape>::success(makeShape(out), std::move(warnings));
}

} // namespace detail

using namespace detail;

namespace {

bool validIndex(const Shape& shape, int index, int count)
{
    return !shape.isNull() && index >= 0 && index < count;
}

TopoDS_Face faceAt(const Shape& shape, int index)
{
    return TopoDS::Face(shape.data()->faces.FindKey(index + 1));
}

TopoDS_Edge edgeAt(const Shape& shape, int index)
{
    return TopoDS::Edge(shape.data()->edges.FindKey(index + 1));
}

// Outward normal of a face at (u,v), honoring face orientation.
std::optional<gp_Dir> faceNormal(const TopoDS_Face& face, double u, double v)
{
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
    if (surface.IsNull())
        return std::nullopt;
    GeomLProp_SLProps props(surface, u, v, 1, 1e-7);
    if (!props.IsNormalDefined())
        return std::nullopt;
    gp_Dir n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    return n;
}

SurfaceKind surfaceKind(GeomAbs_SurfaceType type)
{
    switch (type) {
    case GeomAbs_Plane: return SurfaceKind::Plane;
    case GeomAbs_Cylinder: return SurfaceKind::Cylinder;
    case GeomAbs_Cone: return SurfaceKind::Cone;
    case GeomAbs_Sphere: return SurfaceKind::Sphere;
    case GeomAbs_Torus: return SurfaceKind::Torus;
    case GeomAbs_BSplineSurface: return SurfaceKind::BSpline;
    default: return SurfaceKind::Other;
    }
}

CurveKind curveKind(GeomAbs_CurveType type)
{
    switch (type) {
    case GeomAbs_Line: return CurveKind::Line;
    case GeomAbs_Circle: return CurveKind::Circle;
    case GeomAbs_Ellipse: return CurveKind::Ellipse;
    case GeomAbs_BSplineCurve: return CurveKind::BSpline;
    default: return CurveKind::Other;
    }
}

} // namespace

Result<Shape> makeBox(const Vec3& origin, const Vec3& size)
{
    if (size.x < kMinLength || size.y < kMinLength || size.z < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Box dimensions must be greater than zero.",
                                      "makeBox with non-positive size");
    return guarded("BRepPrimAPI_MakeBox", "Unable to create the box.", [&] {
        BRepPrimAPI_MakeBox maker(toPnt(origin), size.x, size.y, size.z);
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, "Unable to create the box.", "BRepPrimAPI_MakeBox not done");
        return finishSolid(maker.Shape(), "BRepPrimAPI_MakeBox", "Unable to create the box.");
    });
}

Result<Shape> makeCylinder(const Vec3& baseCenter, const Vec3& axis, double radius, double height)
{
    if (radius < kMinLength || height < kMinLength || axis.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Cylinder radius and height must be greater than zero.",
                                      "makeCylinder with invalid arguments");
    return guarded("BRepPrimAPI_MakeCylinder", "Unable to create the cylinder.", [&] {
        const Vec3 a = axis.normalized();
        BRepPrimAPI_MakeCylinder maker(gp_Ax2(toPnt(baseCenter), gp_Dir(a.x, a.y, a.z)), radius, height);
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, "Unable to create the cylinder.", "BRepPrimAPI_MakeCylinder not done");
        return finishSolid(maker.Shape(), "BRepPrimAPI_MakeCylinder", "Unable to create the cylinder.");
    });
}

Result<Shape> pushPullFace(const Shape& shape, int faceIndex, double distance)
{
    if (!validIndex(shape, faceIndex, shape.faceCount()))
        return Result<Shape>::failure(ErrorCode::InvalidReference, "The selected face no longer exists.",
                                      "pushPullFace: face index " + std::to_string(faceIndex) + " out of range");
    if (std::abs(distance) < kMinLength)
        return Result<Shape>::success(shape); // zero offset: no change

    const char* userMessage = "Unable to move this face by that distance.";
    return guarded("pushPullFace", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("pushPullFace");
        const TopoDS_Face face = faceAt(shape, faceIndex);
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            return Result<Shape>::failure(ErrorCode::NotPlanar, "Only flat faces can be pushed or pulled for now.",
                                          "pushPullFace: face " + std::to_string(faceIndex) + " is not planar");
        const auto normal = faceNormal(face, (surface.FirstUParameter() + surface.LastUParameter()) / 2,
                                       (surface.FirstVParameter() + surface.LastVParameter()) / 2);
        if (!normal)
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "pushPullFace: normal undefined");

        const gp_Vec sweep = gp_Vec(*normal) * distance;
        BRepPrimAPI_MakePrism prismMaker(face, sweep);
        prismMaker.Build();
        if (!prismMaker.IsDone())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "BRepPrimAPI_MakePrism not done");
        TopoDS_Shape prism = prismMaker.Shape();
        // A face swept against its own normal yields an inside-out solid; fix
        // orientation so the boolean sees a proper closed solid.
        for (TopExp_Explorer ex(prism, TopAbs_SOLID); ex.More(); ex.Next()) {
            TopoDS_Solid solid = TopoDS::Solid(ex.Current());
            BRepLib::OrientClosedSolid(solid);
            prism = solid;
            break;
        }

        TopoDS_Shape combined;
        if (distance > 0) {
            BRepAlgoAPI_Fuse op(occ(shape), prism);
            if (op.HasErrors())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Fuse failed: " + describeAlgoErrors(op));
            combined = op.Shape();
        } else {
            BRepAlgoAPI_Cut op(occ(shape), prism);
            if (op.HasErrors())
                return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Cut failed: " + describeAlgoErrors(op));
            combined = op.Shape();
        }

        ShapeUpgrade_UnifySameDomain unify(combined, true, true, true);
        unify.Build();
        return finishSolid(unify.Shape(), "pushPullFace", userMessage);
    });
}

Result<Shape> filletEdges(const Shape& shape, const std::vector<int>& edgeIndices, double radius)
{
    if (edgeIndices.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select at least one edge to fillet.", "filletEdges: no edges");
    if (radius < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The fillet radius must be greater than zero.",
                                      "filletEdges: radius " + std::to_string(radius));
    for (int e : edgeIndices)
        if (!validIndex(shape, e, shape.edgeCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected edge no longer exists.",
                                          "filletEdges: edge index " + std::to_string(e) + " out of range");

    const char* userMessage = "Unable to create this fillet. Try a smaller radius.";
    auto result = guarded("BRepFilletAPI_MakeFillet", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("filletEdges");
        BRepFilletAPI_MakeFillet maker(occ(shape));
        for (int e : edgeIndices)
            maker.Add(radius, edgeAt(shape, e));
        maker.Build();
        if (!maker.IsDone()) {
            std::ostringstream dev;
            dev << "BRepFilletAPI_MakeFillet not done: radius=" << radius << " mm, edges=" << edgeIndices.size()
                << ", faulty contours=" << maker.NbFaultyContours() << ", faulty vertices=" << maker.NbFaultyVertices();
            return Result<Shape>::failure(ErrorCode::FilletRadiusTooLarge, userMessage, dev.str());
        }
        return finishSolid(maker.Shape(), "BRepFilletAPI_MakeFillet", userMessage);
    });
    if (!result && result.error() != ErrorCode::InvalidArgument)
        return Result<Shape>::failure(ErrorCode::FilletRadiusTooLarge, userMessage, result.developerMessage());
    return result;
}

Result<Shape> chamferEdges(const Shape& shape, const std::vector<int>& edgeIndices, double distance)
{
    if (edgeIndices.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select at least one edge to chamfer.", "chamferEdges: no edges");
    if (distance < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The chamfer distance must be greater than zero.",
                                      "chamferEdges: distance " + std::to_string(distance));
    for (int e : edgeIndices)
        if (!validIndex(shape, e, shape.edgeCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected edge no longer exists.",
                                          "chamferEdges: edge index " + std::to_string(e) + " out of range");

    const char* userMessage = "Unable to create this chamfer. Try a smaller distance.";
    auto result = guarded("BRepFilletAPI_MakeChamfer", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("chamferEdges");
        BRepFilletAPI_MakeChamfer maker(occ(shape));
        for (int e : edgeIndices)
            maker.Add(distance, edgeAt(shape, e));
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::ChamferTooLarge, userMessage,
                                          "BRepFilletAPI_MakeChamfer not done: distance=" + std::to_string(distance));
        return finishSolid(maker.Shape(), "BRepFilletAPI_MakeChamfer", userMessage);
    });
    if (!result && result.error() != ErrorCode::InvalidArgument)
        return Result<Shape>::failure(ErrorCode::ChamferTooLarge, userMessage, result.developerMessage());
    return result;
}

Result<Shape> booleanOp(const Shape& a, const Shape& b, BooleanKind kind)
{
    if (a.isNull() || b.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select two bodies.", "booleanOp: null operand");
    const char* userMessage = "Unable to combine these bodies.";
    return guarded("booleanOp", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("booleanOp");
        TopoDS_Shape out;
        std::string errors;
        switch (kind) {
        case BooleanKind::Union: {
            BRepAlgoAPI_Fuse op(occ(a), occ(b));
            if (op.HasErrors())
                errors = describeAlgoErrors(op);
            else
                out = op.Shape();
            break;
        }
        case BooleanKind::Subtract: {
            BRepAlgoAPI_Cut op(occ(a), occ(b));
            if (op.HasErrors())
                errors = describeAlgoErrors(op);
            else
                out = op.Shape();
            break;
        }
        case BooleanKind::Intersect: {
            BRepAlgoAPI_Common op(occ(a), occ(b));
            if (op.HasErrors())
                errors = describeAlgoErrors(op);
            else
                out = op.Shape();
            break;
        }
        }
        if (!errors.empty())
            return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage, "Boolean failed: " + errors);
        if (kind != BooleanKind::Intersect) {
            ShapeUpgrade_UnifySameDomain unify(out, true, true, true);
            unify.Build();
            out = unify.Shape();
        }
        return finishSolid(out, "booleanOp", userMessage);
    });
}

Result<Shape> translated(const Shape& shape, const Vec3& offset)
{
    if (shape.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to move.", "translated: null shape");
    return guarded("translate", "Unable to move the body.", [&] {
        gp_Trsf trsf;
        trsf.SetTranslation(toVec(offset));
        BRepBuilderAPI_Transform op(occ(shape), trsf, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

Result<Shape> rotated(const Shape& shape, const Vec3& axisOrigin, const Vec3& axisDirection, double angleRadians)
{
    if (shape.isNull() || axisDirection.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to rotate.", "rotated: invalid input");
    return guarded("rotate", "Unable to rotate the body.", [&] {
        const Vec3 d = axisDirection.normalized();
        gp_Trsf trsf;
        trsf.SetRotation(gp_Ax1(toPnt(axisOrigin), gp_Dir(d.x, d.y, d.z)), angleRadians);
        BRepBuilderAPI_Transform op(occ(shape), trsf, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

double volume(const Shape& shape)
{
    if (shape.isNull())
        return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(occ(shape), props);
    return props.Mass();
}

double surfaceArea(const Shape& shape)
{
    if (shape.isNull())
        return 0.0;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(occ(shape), props);
    return props.Mass();
}

BoundingBox boundingBox(const Shape& shape)
{
    BoundingBox out;
    if (shape.isNull())
        return out;
    Bnd_Box box;
    BRepBndLib::AddOptimal(occ(shape), box, false, false);
    if (box.IsVoid())
        return out;
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    out.min = {x0, y0, z0};
    out.max = {x1, y1, z1};
    out.valid = true;
    return out;
}

bool isValid(const Shape& shape)
{
    if (shape.isNull())
        return false;
    BRepCheck_Analyzer analyzer(occ(shape));
    return analyzer.IsValid();
}

std::optional<FaceInfo> faceInfo(const Shape& shape, int faceIndex)
{
    if (!validIndex(shape, faceIndex, shape.faceCount()))
        return std::nullopt;
    try {
        const TopoDS_Face face = faceAt(shape, faceIndex);
        FaceInfo info;
        BRepAdaptor_Surface surface(face);
        info.kind = surfaceKind(surface.GetType());

        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        info.area = props.Mass();
        info.centroid = fromPnt(props.CentreOfMass());

        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        if (const auto n = faceNormal(face, (u0 + u1) / 2, (v0 + v1) / 2))
            info.normal = fromDir(*n);
        if (info.kind == SurfaceKind::Plane)
            info.planeOrigin = fromPnt(surface.Plane().Location());
        return info;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "faceInfo(" << faceIndex << ") failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::optional<EdgeInfo> edgeInfo(const Shape& shape, int edgeIndex)
{
    if (!validIndex(shape, edgeIndex, shape.edgeCount()))
        return std::nullopt;
    try {
        const TopoDS_Edge edge = edgeAt(shape, edgeIndex);
        if (BRep_Tool::Degenerated(edge))
            return std::nullopt;
        BRepAdaptor_Curve curve(edge);
        EdgeInfo info;
        info.kind = curveKind(curve.GetType());
        const double t0 = curve.FirstParameter();
        const double t1 = curve.LastParameter();
        info.start = fromPnt(curve.Value(t0));
        info.end = fromPnt(curve.Value(t1));
        gp_Pnt mid;
        gp_Vec tangent;
        curve.D1((t0 + t1) / 2, mid, tangent);
        info.midpoint = fromPnt(mid);
        info.tangent = fromVec(tangent).normalized();
        info.length = GCPnts_AbscissaPoint::Length(curve);
        if (info.kind == CurveKind::Circle)
            info.radius = curve.Circle().Radius();
        return info;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "edgeInfo(" << edgeIndex << ") failed: " << describeFailure(failure);
        return std::nullopt;
    }
}

std::vector<int> facesOfEdge(const Shape& shape, int edgeIndex)
{
    std::vector<int> result;
    if (!validIndex(shape, edgeIndex, shape.edgeCount()))
        return result;
    TopTools_IndexedDataMapOfShapeListOfShape map;
    TopExp::MapShapesAndAncestors(occ(shape), TopAbs_EDGE, TopAbs_FACE, map);
    const TopoDS_Shape& edge = shape.data()->edges.FindKey(edgeIndex + 1);
    if (!map.Contains(edge))
        return result;
    for (const TopoDS_Shape& face : map.FindFromKey(edge)) {
        const int index = shape.data()->faces.FindIndex(face);
        if (index > 0 && std::find(result.begin(), result.end(), index - 1) == result.end())
            result.push_back(index - 1);
    }
    return result;
}

std::string toBrepString(const Shape& shape)
{
    if (shape.isNull())
        return {};
    std::ostringstream out;
    BRepTools::Write(occ(shape), out);
    return out.str();
}

Result<Shape> fromBrepString(const std::string& text)
{
    return guarded("BRepTools::Read", "The stored geometry could not be read.", [&]() -> Result<Shape> {
        std::istringstream in(text);
        TopoDS_Shape shape;
        BRep_Builder builder;
        BRepTools::Read(shape, in, builder);
        if (shape.IsNull())
            return Result<Shape>::failure(ErrorCode::FileFormatError, "The stored geometry could not be read.",
                                          "BRepTools::Read produced a null shape");
        return Result<Shape>::success(makeShape(shape));
    });
}

} // namespace os::geom
