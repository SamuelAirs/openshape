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
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
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
#include <TopTools_MapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <set>
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
        warnings.push_back("The body is now in " + std::to_string(solidCount) + " separate pieces.");
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

Result<Shape> shell(const Shape& shape, const std::vector<int>& openFaces, double thickness)
{
    if (openFaces.empty())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Select the face(s) to open.", "shell: no faces");
    if (thickness < kMinLength)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "The wall thickness must be greater than zero.",
                                      "shell: thickness " + std::to_string(thickness));
    for (int f : openFaces)
        if (!validIndex(shape, f, shape.faceCount()))
            return Result<Shape>::failure(ErrorCode::InvalidReference, "A selected face no longer exists.",
                                          "shell: face index " + std::to_string(f) + " out of range");

    const char* userMessage = "Unable to shell with this wall thickness. Try thinner walls.";
    auto result = guarded("BRepOffsetAPI_MakeThickSolid", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("shell");
        TopTools_ListOfShape faces;
        for (int f : openFaces)
            faces.Append(faceAt(shape, f));
        BRepOffsetAPI_MakeThickSolid maker;
        // Negative offset: walls grow inward, the outside stays where it is.
        maker.MakeThickSolidByJoin(occ(shape), faces, -thickness, 1e-3);
        maker.Build();
        if (!maker.IsDone())
            return Result<Shape>::failure(ErrorCode::ShellTooThick, userMessage,
                                          "MakeThickSolidByJoin not done: thickness=" + std::to_string(thickness));
        auto hollow = finishSolid(maker.Shape(), "BRepOffsetAPI_MakeThickSolid", userMessage);
        // When the walls would meet, OCCT can report success and hand back the
        // untouched solid. A shell always removes material: reject anything else.
        if (hollow && volume(hollow.value()) >= volume(shape) * (1.0 - 1e-9))
            return Result<Shape>::failure(ErrorCode::ShellTooThick, userMessage,
                                          "MakeThickSolidByJoin removed no material: thickness=" + std::to_string(thickness));
        return hollow;
    });
    if (!result && result.error() != ErrorCode::InvalidArgument)
        return Result<Shape>::failure(ErrorCode::ShellTooThick, userMessage, result.developerMessage());
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

namespace {
// Rodrigues rotation of v about unit axis k by angle a.
Vec3 rotateVector(const Vec3& v, const Vec3& k, double a)
{
    return v * std::cos(a) + k.cross(v) * std::sin(a) + k * (k.dot(v) * (1 - std::cos(a)));
}
} // namespace

Vec3 RigidMotion::apply(const Vec3& p) const
{
    const Vec3 k = axis.length() > 1e-12 ? axis.normalized() : Vec3{0, 0, 1};
    return center + rotateVector(p - center, k, angle) + translation;
}

Result<Shape> transformed(const Shape& shape, const RigidMotion& motion)
{
    if (shape.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to move.", "transformed: null shape");
    if (motion.isIdentity())
        return Result<Shape>::success(shape);
    if (std::abs(motion.angle) > 1e-12 && motion.axis.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Unable to rotate the body.", "transformed: zero axis");
    return guarded("transform", "Unable to move the body.", [&] {
        gp_Trsf rotation;
        if (std::abs(motion.angle) > 1e-12) {
            const Vec3 d = motion.axis.normalized();
            rotation.SetRotation(gp_Ax1(toPnt(motion.center), gp_Dir(d.x, d.y, d.z)), motion.angle);
        }
        gp_Trsf translation;
        translation.SetTranslation(toVec(motion.translation));
        BRepBuilderAPI_Transform op(occ(shape), translation * rotation, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

Result<Shape> mirrored(const Shape& shape, const Vec3& planeOrigin, const Vec3& planeNormal)
{
    if (shape.isNull() || planeNormal.length() < 1e-12)
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to mirror.", "mirrored: invalid input");
    return guarded("mirror", "Unable to mirror the body.", [&] {
        const Vec3 n = planeNormal.normalized();
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2(toPnt(planeOrigin), gp_Dir(n.x, n.y, n.z)));
        // BRepBuilderAPI_Transform keeps solids valid under a reflection.
        BRepBuilderAPI_Transform op(occ(shape), mirror, true);
        return Result<Shape>::success(makeShape(op.Shape()));
    });
}

namespace {
// Fuses `shapes` (at least one) in one General Fuse run, then merges faces
// that ended up on the same surface and validates the result.
Result<Shape> fuseInOnePass(const std::vector<TopoDS_Shape>& shapes, const char* operation, const char* userMessage)
{
    if (shapes.size() == 1)
        return finishSolid(shapes.front(), operation, userMessage);
    TopTools_ListOfShape arguments, tools;
    arguments.Append(shapes.front());
    for (std::size_t i = 1; i < shapes.size(); ++i)
        tools.Append(shapes[i]);
    BRepAlgoAPI_Fuse fuse;
    fuse.SetArguments(arguments);
    fuse.SetTools(tools);
    fuse.Build();
    if (fuse.HasErrors())
        return Result<Shape>::failure(ErrorCode::KernelFailure, userMessage,
                                      std::string(operation) + ": fuse failed: " + describeAlgoErrors(fuse));
    ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), true, true, true);
    unify.Build();
    return finishSolid(unify.Shape(), operation, userMessage);
}
} // namespace

Result<Shape> mirrorJoined(const Shape& shape, const Vec3& planeOrigin, const Vec3& planeNormal)
{
    auto image = mirrored(shape, planeOrigin, planeNormal);
    if (!image)
        return image;
    const char* userMessage = "Unable to mirror the body across this plane.";
    return guarded("mirrorJoined", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("mirrorJoined");
        return fuseInOnePass({occ(shape), occ(image.value())}, "mirrorJoined", userMessage);
    });
}

Result<Shape> repeatJoined(const Shape& shape, const std::vector<RigidMotion>& copies)
{
    if (shape.isNull())
        return Result<Shape>::failure(ErrorCode::InvalidArgument, "Nothing to repeat.", "repeatJoined: null shape");
    std::vector<TopoDS_Shape> parts{occ(shape)};
    for (const RigidMotion& motion : copies) {
        auto copy = transformed(shape, motion);
        if (!copy)
            return copy;
        parts.push_back(occ(copy.value()));
    }
    const char* userMessage = "Unable to repeat the body this way.";
    return guarded("repeatJoined", userMessage, [&]() -> Result<Shape> {
        ScopedTimer timer("repeatJoined");
        return fuseInOnePass(parts, "repeatJoined", userMessage);
    });
}

std::optional<AlignFrame> alignFrame(const Shape& shape, SubShapeKind kind, int index)
{
    if (kind == SubShapeKind::Face) {
        const auto info = faceInfo(shape, index);
        if (!info)
            return std::nullopt;
        if (info->isPlanar())
            return AlignFrame{info->centroid, info->normal.normalized(), true};
        if (info->hasAxis() && info->axisDirection.length() > 0.5)
            return AlignFrame{info->axisOrigin, info->axisDirection.normalized(), false};
        return std::nullopt;
    }
    if (kind == SubShapeKind::Edge) {
        const auto info = edgeInfo(shape, index);
        if (!info)
            return std::nullopt;
        if (info->kind == CurveKind::Line)
            return AlignFrame{info->midpoint, info->tangent.normalized(), false};
        if (info->kind == CurveKind::Circle && info->axis.length() > 0.5)
            return AlignFrame{info->center, info->axis.normalized(), false};
    }
    return std::nullopt;
}

RigidMotion alignMotion(const AlignFrame& source, const AlignFrame& target, bool flip, double offset)
{
    const Vec3 s = source.direction.normalized();
    Vec3 t = target.direction.normalized();
    if (source.sided && target.sided)
        t = t * -1.0; // flat faces meet face to face
    else if (s.dot(t) < 0)
        t = t * -1.0; // unsided: the smaller turn
    if (flip)
        t = t * -1.0;

    RigidMotion motion;
    motion.center = source.point;
    const Vec3 cross = s.cross(t);
    const double sine = cross.length(), cosine = std::clamp(s.dot(t), -1.0, 1.0);
    if (sine > 1e-9) {
        motion.axis = cross * (1.0 / sine);
        motion.angle = std::atan2(sine, cosine);
    } else if (cosine < 0) {
        // Opposite directions: half a turn about any axis perpendicular to s.
        const Vec3 helper = std::abs(s.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        motion.axis = s.cross(helper).normalized();
        motion.angle = kPi;
    }
    motion.translation = target.point + target.direction.normalized() * offset - source.point;
    return motion;
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

namespace {
BoundingBox toBoundingBox(const Bnd_Box& box)
{
    BoundingBox out;
    if (box.IsVoid())
        return out;
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    out.min = {x0, y0, z0};
    out.max = {x1, y1, z1};
    out.valid = true;
    return out;
}
} // namespace

BoundingBox boundingBox(const Shape& shape)
{
    if (shape.isNull())
        return {};
    // AddOptimal optimizes over every face and edge (~40 ms for a filleted
    // cube), so it runs once per shape; callers ask for the same shape often.
    const ShapeData& data = *shape.data();
    std::call_once(data.tightBoxOnce, [&data] {
        Bnd_Box box;
        BRepBndLib::AddOptimal(data.shape, box, false, false);
        data.tightBox = toBoundingBox(box);
    });
    return data.tightBox;
}

BoundingBox approximateBoundingBox(const Shape& shape)
{
    if (shape.isNull())
        return {};
    // Geometry bounds (control-point hulls for B-splines), independent of any
    // triangulation, so the result does not depend on what was meshed before.
    Bnd_Box box;
    BRepBndLib::Add(occ(shape), box, false);
    return toBoundingBox(box);
}

std::vector<int> facesChangedBy(const Shape& before, const Shape& after, const Shape& current)
{
    std::vector<int> out;
    if (after.isNull() || current.isNull())
        return out;
    // TopTools_MapOfShape compares with IsSame (same TShape and location), so a
    // moved face counts as changed while an untouched one does not.
    TopTools_MapOfShape old;
    if (!before.isNull())
        for (int i = 1; i <= before.data()->faces.Extent(); ++i)
            old.Add(before.data()->faces(i));
    TopTools_MapOfShape changed;
    for (int i = 1; i <= after.data()->faces.Extent(); ++i)
        if (!old.Contains(after.data()->faces(i)))
            changed.Add(after.data()->faces(i));
    for (int i = 1; i <= current.data()->faces.Extent(); ++i)
        if (changed.Contains(current.data()->faces(i)))
            out.push_back(i - 1);
    return out;
}

std::vector<int> facesCreatedBy(const Shape& before, const Shape& after, const Shape& current)
{
    std::vector<int> out;
    if (after.isNull() || current.isNull())
        return out;
    // Trimmed or split faces are rebuilt on the input face's surface object;
    // genuinely new faces get new surfaces.
    std::set<const Geom_Surface*> oldSurfaces;
    TopTools_MapOfShape oldFaces;
    if (!before.isNull())
        for (int i = 1; i <= before.data()->faces.Extent(); ++i) {
            const TopoDS_Face face = TopoDS::Face(before.data()->faces(i));
            oldFaces.Add(face);
            TopLoc_Location location;
            oldSurfaces.insert(BRep_Tool::Surface(face, location).get());
        }
    TopTools_MapOfShape created;
    for (int i = 1; i <= after.data()->faces.Extent(); ++i) {
        const TopoDS_Face face = TopoDS::Face(after.data()->faces(i));
        TopLoc_Location location;
        if (!oldFaces.Contains(face) && !oldSurfaces.count(BRep_Tool::Surface(face, location).get()))
            created.Add(face);
    }
    for (int i = 1; i <= current.data()->faces.Extent(); ++i)
        if (created.Contains(current.data()->faces(i)))
            out.push_back(i - 1);
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
        if (info.hasAxis()) {
            const gp_Ax1 axis = info.kind == SurfaceKind::Cylinder ? surface.Cylinder().Axis() : surface.Cone().Axis();
            const Vec3 origin = fromPnt(axis.Location());
            const Vec3 direction = fromDir(axis.Direction());
            info.axisDirection = direction;
            info.axisOrigin = origin + direction * (info.centroid - origin).dot(direction);
            info.radius = info.kind == SurfaceKind::Cylinder ? surface.Cylinder().Radius() : surface.Cone().RefRadius();
        }
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
        if (info.kind == CurveKind::Circle) {
            const gp_Circ circle = curve.Circle();
            info.radius = circle.Radius();
            info.center = fromPnt(circle.Location());
            info.axis = fromDir(circle.Axis().Direction());
        }
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

std::optional<Measurement> measure(const SubShapeRef& a, const SubShapeRef& b)
{
    auto resolve = [](const SubShapeRef& r) -> TopoDS_Shape {
        if (!r.shape || r.shape->isNull())
            return {};
        const ShapeData* data = r.shape->data();
        switch (r.kind) {
        case SubShapeKind::Face:
            return r.index >= 0 && r.index < data->faces.Extent() ? data->faces.FindKey(r.index + 1) : TopoDS_Shape();
        case SubShapeKind::Edge:
            return r.index >= 0 && r.index < data->edges.Extent() ? data->edges.FindKey(r.index + 1) : TopoDS_Shape();
        case SubShapeKind::Vertex:
            return r.index >= 0 && r.index < data->vertices.Extent() ? data->vertices.FindKey(r.index + 1) : TopoDS_Shape();
        case SubShapeKind::Whole:
            return data->shape;
        }
        return {};
    };
    const TopoDS_Shape sa = resolve(a), sb = resolve(b);
    if (sa.IsNull() || sb.IsNull())
        return std::nullopt;
    try {
        BRepExtrema_DistShapeShape extrema(sa, sb);
        if (!extrema.IsDone() || extrema.NbSolution() < 1)
            return std::nullopt;
        Measurement m;
        m.distance = extrema.Value();
        m.pointA = fromPnt(extrema.PointOnShape1(1));
        m.pointB = fromPnt(extrema.PointOnShape2(1));
        // Angles and gaps for the common maker questions: wall thickness
        // (parallel faces) and whether two faces/edges are square.
        if (a.kind == SubShapeKind::Face && b.kind == SubShapeKind::Face) {
            const auto fa = faceInfo(*a.shape, a.index);
            const auto fb = faceInfo(*b.shape, b.index);
            if (fa && fb && fa->isPlanar() && fb->isPlanar()) {
                const double c = std::clamp(std::abs(fa->normal.dot(fb->normal)), 0.0, 1.0);
                m.angle = std::acos(c);
                if (c > 1.0 - 1e-9)
                    m.parallelGap = std::abs((fb->centroid - fa->centroid).dot(fa->normal));
            }
        } else if (a.kind == SubShapeKind::Edge && b.kind == SubShapeKind::Edge) {
            const auto ea = edgeInfo(*a.shape, a.index);
            const auto eb = edgeInfo(*b.shape, b.index);
            if (ea && eb && ea->kind == CurveKind::Line && eb->kind == CurveKind::Line)
                m.angle = std::acos(std::clamp(std::abs(ea->tangent.dot(eb->tangent)), 0.0, 1.0));
        }
        return m;
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "measure failed: " << describeFailure(failure);
        return std::nullopt;
    }
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
