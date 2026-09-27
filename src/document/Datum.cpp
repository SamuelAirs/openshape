// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/Datum.h"

#include "document/Body.h"
#include "document/Document.h"
#include "document/JsonHelpers.h"
#include "geometry/Modeling.h"
#include "geometry/TopoSignature.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace os::doc {

using nlohmann::json;

namespace {

constexpr std::array<std::pair<DatumMethod, std::string_view>, 7> kMethodNames{{
    {DatumMethod::AxisThrough, "AxisThrough"},
    {DatumMethod::AxisAlongEdge, "AxisAlongEdge"},
    {DatumMethod::AxisTwoPoints, "AxisTwoPoints"},
    {DatumMethod::AxisParallel, "AxisParallel"},
    {DatumMethod::PlaneOffset, "PlaneOffset"},
    {DatumMethod::PlaneAngle, "PlaneAngle"},
    {DatumMethod::PlaneMidway, "PlaneMidway"},
}};

constexpr std::array<std::pair<GeometryRef::Kind, std::string_view>, 4> kRefKindNames{{
    {GeometryRef::Kind::Face, "Face"},
    {GeometryRef::Kind::Edge, "Edge"},
    {GeometryRef::Kind::Vertex, "Vertex"},
    {GeometryRef::Kind::Center, "Center"},
}};

Vec3 axisVector(int axis)
{
    return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

// The index of the step whose output the body shows: its last step that has
// one (a failed step leaves the last good shape on screen).
int shownStep(const Body& body)
{
    for (int i = static_cast<int>(body.features().size()) - 1; i >= 0; --i) {
        const auto status = body.state(i).status;
        if (status == FeatureStatus::Ok || status == FeatureStatus::Suppressed)
            return i;
    }
    return -1;
}

Result<DatumGeometry> refused(const std::string& user, const std::string& developer)
{
    return Result<DatumGeometry>::failure(ErrorCode::InvalidReference, user, "datum: " + developer);
}

// A reference resolved: the step output it lives on and its index there.
struct Resolved {
    geom::Shape shape;
    int index = -1;
};

Result<Resolved> resolveRef(const GeometryRef& ref, const EvalContext& context)
{
    using R = Result<Resolved>;
    const Body* body = context.body && context.body->id() == ref.body
                           ? context.body
                           : (context.document ? context.document->body(ref.body) : nullptr);
    if (!body)
        return R::failure(ErrorCode::InvalidReference, "The body it was made from is gone.", "datum: body missing");
    const int step = body->featureIndex(ref.feature);
    if (step < 0)
        return R::failure(ErrorCode::InvalidReference, "The step it was made from is gone.", "datum: step missing");
    if (body == context.body && step >= context.featureIndex)
        return R::failure(ErrorCode::InvalidReference, "It is made from a later step.", "datum: step not computed yet");
    const FeatureState& state = body->state(step);
    if (state.status != FeatureStatus::Ok && state.status != FeatureStatus::Suppressed)
        return R::failure(ErrorCode::InvalidReference, "The step it was made from failed.", "datum: step failed");
    std::optional<int> index;
    if (ref.kind == GeometryRef::Kind::Face)
        index = geom::resolveFace(state.output, ref.face.signature, ref.face.indexHint);
    else
        index = geom::resolveEdge(state.output, ref.edge.signature, ref.edge.indexHint);
    if (!index)
        return R::failure(ErrorCode::InvalidReference,
                          ref.kind == GeometryRef::Kind::Face ? "The face it was made from no longer exists."
                          : ref.kind == GeometryRef::Kind::Edge ? "The edge it was made from no longer exists."
                                                                : "The point it was made from no longer exists.",
                          "datum: sub-shape not found");
    return R::success(Resolved{state.output, *index});
}

// One reference resolved to the facts datumGeometry works from.
Result<ResolvedRef> resolveOne(const GeometryRef& ref, const EvalContext& context)
{
    using R = Result<ResolvedRef>;
    auto resolved = resolveRef(ref, context);
    if (!resolved)
        return R::failureFrom(resolved);
    ResolvedRef out;
    out.kind = ref.kind;
    if (ref.kind == GeometryRef::Kind::Face) {
        const auto info = geom::faceInfo(resolved.value().shape, resolved.value().index);
        if (!info)
            return R::failure(ErrorCode::InvalidReference, "The face it was made from no longer exists.", "datum: face info");
        out.face = *info;
        return R::success(out);
    }
    const auto info = geom::edgeInfo(resolved.value().shape, resolved.value().index);
    if (!info)
        return R::failure(ErrorCode::InvalidReference, "The edge it was made from no longer exists.", "datum: edge info");
    out.edge = *info;
    if (ref.kind == GeometryRef::Kind::Center) {
        if (info->kind != geom::CurveKind::Circle)
            return R::failure(ErrorCode::InvalidReference, "The circle it was made from is no longer round.",
                              "datum: center of a non-circle");
        out.point = info->center;
    } else if (ref.kind == GeometryRef::Kind::Vertex) {
        // The end of the edge nearest where the corner was when picked.
        out.point = (info->start - ref.point).length() <= (info->end - ref.point).length() ? info->start : info->end;
    }
    return R::success(out);
}

Result<geom::FaceInfo> flatFace(const ResolvedRef& ref)
{
    using R = Result<geom::FaceInfo>;
    if (ref.kind != GeometryRef::Kind::Face || !ref.face.isPlanar() || ref.face.normal.length() < 0.5)
        return R::failure(ErrorCode::NotPlanar, "The face it was made from is no longer flat.", "datum: face not planar");
    return R::success(ref.face);
}

double halfSizeForArea(double area)
{
    return std::max(std::sqrt(std::max(area, 0.0)) * 0.75, 5.0);
}

} // namespace

std::string_view toString(DatumMethod method)
{
    for (const auto& [m, name] : kMethodNames)
        if (m == method)
            return name;
    return "PlaneOffset";
}

std::optional<DatumMethod> datumMethodFromString(std::string_view text)
{
    for (const auto& [m, name] : kMethodNames)
        if (name == text)
            return m;
    return std::nullopt;
}

DatumKind kindOf(DatumMethod method)
{
    switch (method) {
    case DatumMethod::AxisThrough:
    case DatumMethod::AxisAlongEdge:
    case DatumMethod::AxisTwoPoints:
    case DatumMethod::AxisParallel: return DatumKind::Axis;
    case DatumMethod::PlaneOffset:
    case DatumMethod::PlaneAngle:
    case DatumMethod::PlaneMidway: return DatumKind::Plane;
    }
    return DatumKind::Plane;
}

std::optional<GeometryRef> makeGeometryRef(const Body& body, GeometryRef::Kind kind, int index, const Vec3& near)
{
    const int step = shownStep(body);
    if (step < 0)
        return std::nullopt;
    const geom::Shape& output = body.state(step).output;
    GeometryRef ref;
    ref.kind = kind;
    ref.body = body.id();
    ref.feature = body.features()[std::size_t(step)]->id();
    if (kind == GeometryRef::Kind::Face) {
        const auto signature = geom::captureFaceSignature(output, index);
        if (!signature)
            return std::nullopt;
        ref.face = {index, *signature};
        return ref;
    }
    const auto signature = geom::captureEdgeSignature(output, index);
    const auto info = geom::edgeInfo(output, index);
    if (!signature || !info)
        return std::nullopt;
    ref.edge = {index, *signature};
    if (kind == GeometryRef::Kind::Center) {
        if (info->kind != geom::CurveKind::Circle)
            return std::nullopt;
        ref.point = info->center;
    } else if (kind == GeometryRef::Kind::Vertex) {
        if ((info->start - info->end).length() < 1e-9)
            return std::nullopt; // a closed curve has no corner
        ref.point = (info->start - near).length() <= (info->end - near).length() ? info->start : info->end;
    }
    return ref;
}

sketch::Plane sketchPlaneOn(const DatumGeometry& plane)
{
    const Vec3 n = plane.direction.normalized();
    return sketch::Plane::fromNormal(n * n.dot(plane.origin), n);
}

Result<DatumGeometry> resolveDatum(const Datum& datum, const EvalContext& context)
{
    auto refs = resolveDatumRefs(datum, context);
    if (!refs)
        return Result<DatumGeometry>::failureFrom(refs);
    return datumGeometry(datum, refs.value());
}

Result<std::vector<ResolvedRef>> resolveDatumRefs(const Datum& datum, const EvalContext& context)
{
    using R = Result<std::vector<ResolvedRef>>;
    std::vector<ResolvedRef> out;
    out.reserve(datum.refs.size());
    for (const GeometryRef& ref : datum.refs) {
        auto one = resolveOne(ref, context);
        if (!one)
            return R::failureFrom(one);
        out.push_back(one.value());
    }
    return R::success(std::move(out));
}

Result<DatumGeometry> datumGeometry(const Datum& datum, const std::vector<ResolvedRef>& refs)
{
    using R = Result<DatumGeometry>;
    // The references must be the datum's, resolved: the same count and kinds.
    bool matches = refs.size() == datum.refs.size();
    for (std::size_t i = 0; matches && i < refs.size(); ++i)
        matches = refs[i].kind == datum.refs[i].kind;
    if (!matches)
        return refused("This construction axis or plane is damaged.", "datumGeometry: references do not match");
    auto needRefs = [&](std::size_t count) { return refs.size() == count; };
    auto isEdge = [](const ResolvedRef& r) { return r.kind != GeometryRef::Kind::Face; };
    auto isPoint = [](const ResolvedRef& r) { return r.kind == GeometryRef::Kind::Vertex || r.kind == GeometryRef::Kind::Center; };
    DatumGeometry g;
    switch (datum.method) {
    case DatumMethod::AxisThrough: {
        if (!needRefs(1))
            return refused("This axis is damaged.", "AxisThrough needs one reference");
        const ResolvedRef& ref = refs[0];
        if (ref.kind == GeometryRef::Kind::Face) {
            if (!ref.face.hasAxis() || ref.face.axisDirection.length() < 0.5)
                return refused("The hole or shaft it was made from is no longer round.", "AxisThrough face has no axis");
            g.origin = ref.face.axisOrigin;
            g.direction = ref.face.axisDirection.normalized();
        } else {
            if (ref.edge.kind != geom::CurveKind::Circle || ref.edge.axis.length() < 0.5)
                return refused("The circle it was made from is no longer round.", "AxisThrough edge is not a circle");
            g.origin = ref.edge.center;
            g.direction = ref.edge.axis.normalized();
        }
        break;
    }
    case DatumMethod::AxisAlongEdge: {
        if (!needRefs(1) || !isEdge(refs[0]))
            return refused("This axis is damaged.", "AxisAlongEdge needs one edge");
        if (refs[0].edge.kind != geom::CurveKind::Line)
            return refused("The edge it was made from is no longer straight.", "AxisAlongEdge edge is not a line");
        g.origin = refs[0].edge.midpoint;
        g.direction = refs[0].edge.tangent.normalized();
        break;
    }
    case DatumMethod::AxisTwoPoints: {
        if (!needRefs(2) || !isPoint(refs[0]) || !isPoint(refs[1]))
            return refused("This axis is damaged.", "AxisTwoPoints needs two points");
        const Vec3 a = refs[0].point, b = refs[1].point;
        if ((b - a).length() < 1e-6)
            return refused("The two points are at the same place.", "AxisTwoPoints: coincident points");
        g.origin = (a + b) * 0.5;
        g.direction = (b - a).normalized();
        break;
    }
    case DatumMethod::AxisParallel: {
        if (!needRefs(1) || !isPoint(refs[0]) || datum.originIndex < 0 || datum.originIndex > 2)
            return refused("This axis is damaged.", "AxisParallel needs a point and an origin axis");
        g.origin = refs[0].point;
        g.direction = axisVector(datum.originIndex);
        break;
    }
    case DatumMethod::PlaneOffset: {
        if (refs.empty()) {
            if (datum.originIndex < 0 || datum.originIndex > 2)
                return refused("This plane is damaged.", "PlaneOffset without a face needs an origin plane");
            g.direction = axisVector(datum.originIndex);
            g.origin = g.direction * datum.distance;
            g.center = g.origin;
            g.xAxis = sketchPlaneOn(g).xAxis;
            return R::success(g);
        }
        if (!needRefs(1))
            return refused("This plane is damaged.", "PlaneOffset takes one face");
        auto face = flatFace(refs[0]);
        if (!face)
            return R::failureFrom(face);
        g.direction = face.value().normal.normalized();
        g.origin = face.value().centroid + g.direction * datum.distance;
        g.center = g.origin;
        g.size = halfSizeForArea(face.value().area);
        g.xAxis = sketchPlaneOn(g).xAxis;
        return R::success(g);
    }
    case DatumMethod::PlaneAngle: {
        if (!needRefs(2) || !isEdge(refs[0]))
            return refused("This plane is damaged.", "PlaneAngle needs an edge and a face");
        const geom::EdgeInfo& edge = refs[0].edge;
        if (edge.kind != geom::CurveKind::Line)
            return refused("The edge it was made from is no longer straight.", "PlaneAngle edge is not a line");
        auto face = flatFace(refs[1]);
        if (!face)
            return R::failureFrom(face);
        const Vec3 d = edge.tangent.normalized();
        const Vec3 n = face.value().normal.normalized();
        if (std::abs(d.dot(n)) > 1e-3)
            return refused("The edge no longer runs along the face.", "PlaneAngle: edge not parallel to the face");
        // Across the edge, into the face (from the edge toward the face's
        // middle): at 0 the plane is the face's, at 90 degrees it stands up.
        const Vec3 toMiddle = face.value().centroid - edge.midpoint;
        Vec3 inward = toMiddle - d * toMiddle.dot(d) - n * toMiddle.dot(n);
        if (inward.length() < 1e-9)
            inward = n.cross(d);
        inward = inward.normalized();
        const Vec3 across = inward * std::cos(datum.angle) + n * std::sin(datum.angle);
        Vec3 normal = d.cross(across).normalized();
        if (d.cross(inward).dot(n) < 0)
            normal = normal * -1.0; // the face's own normal at 0
        g.size = std::max(edge.length * 0.6, 5.0);
        g.origin = edge.midpoint;
        g.direction = normal;
        g.xAxis = d;
        g.center = edge.midpoint + across * g.size;
        return R::success(g);
    }
    case DatumMethod::PlaneMidway: {
        if (!needRefs(2))
            return refused("This plane is damaged.", "PlaneMidway needs two faces");
        auto a = flatFace(refs[0]);
        if (!a)
            return R::failureFrom(a);
        auto b = flatFace(refs[1]);
        if (!b)
            return R::failureFrom(b);
        const Vec3 n = a.value().normal.normalized();
        if (std::abs(std::abs(n.dot(b.value().normal.normalized())) - 1.0) > 1e-6)
            return refused("The two faces are no longer parallel.", "PlaneMidway: faces not parallel");
        g.direction = n;
        g.origin = (a.value().centroid + b.value().centroid) * 0.5;
        g.center = g.origin;
        g.size = halfSizeForArea(std::max(a.value().area, b.value().area));
        g.xAxis = sketchPlaneOn(g).xAxis;
        return R::success(g);
    }
    }
    // Axes: centered where they were made.
    g.center = g.origin;
    return R::success(g);
}

// ---- Parameters ---------------------------------------------------------------------

std::vector<ParameterInfo> Datum::parameters() const
{
    if (method == DatumMethod::PlaneOffset)
        return {{"distance", "Distance", ParameterKind::Length, distance}};
    if (method == DatumMethod::PlaneAngle)
        return {{"angle", "Angle", ParameterKind::Angle, angle}};
    return {};
}

Status Datum::setParameter(std::string_view key, double value)
{
    if (!std::isfinite(value))
        return Status::failure(ErrorCode::InvalidArgument, "Enter a number.", "datum parameter not finite");
    if (key == "distance" && method == DatumMethod::PlaneOffset) {
        if (std::abs(value) > 1e6)
            return Status::failure(ErrorCode::InvalidArgument, "The distance is too large.", "datum distance out of range");
        distance = value;
        return okStatus();
    }
    if (key == "angle" && method == DatumMethod::PlaneAngle) {
        if (std::abs(value) > kPi + 1e-12)
            return Status::failure(ErrorCode::InvalidArgument, "The angle must be between -180° and 180°.",
                                   "datum angle out of range");
        angle = value;
        return okStatus();
    }
    return Status::failure(ErrorCode::InvalidArgument, "This value cannot be changed.",
                           "datum: unknown parameter '" + std::string(key) + "'");
}

std::vector<Uuid> Datum::bodies() const
{
    std::vector<Uuid> out;
    for (const auto& ref : refs)
        if (std::find(out.begin(), out.end(), ref.body) == out.end())
            out.push_back(ref.body);
    return out;
}

// ---- JSON ----------------------------------------------------------------------------

json Datum::toJson() const
{
    json refsJson = json::array();
    for (const auto& ref : refs) {
        std::string_view kindName = "Face";
        for (const auto& [k, name] : kRefKindNames)
            if (k == ref.kind)
                kindName = name;
        json entry{{"kind", std::string(kindName)}, {"body", ref.body.toString()}, {"feature", ref.feature.toString()}};
        if (ref.kind == GeometryRef::Kind::Face)
            entry["face"] = faceRefToJson(ref.face);
        else
            entry["edge"] = edgeRefToJson(ref.edge);
        if (ref.kind == GeometryRef::Kind::Vertex)
            entry["point"] = vecToJson(ref.point);
        refsJson.push_back(std::move(entry));
    }
    json out{{"id", id_.toString()},
             {"name", name_},
             {"visible", visible_},
             {"method", std::string(toString(method))},
             {"refs", refsJson},
             {"geometry",
              {{"origin", vecToJson(geometry_.origin)},
               {"direction", vecToJson(geometry_.direction)},
               {"xAxis", vecToJson(geometry_.xAxis)},
               {"center", vecToJson(geometry_.center)},
               {"size", geometry_.size}}}};
    if (originIndex >= 0)
        out["origin"] = originIndex;
    if (method == DatumMethod::PlaneOffset)
        out["distance"] = distance;
    if (method == DatumMethod::PlaneAngle)
        out["angle"] = angle;
    return out;
}

Result<Datum> Datum::fromJson(const json& j)
{
    auto bad = [](const std::string& why) {
        return Result<Datum>::failure(ErrorCode::FileFormatError, "The file contains an invalid construction axis or plane.",
                                      "datum: " + why);
    };
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string())
        return bad("missing id");
    const auto id = Uuid::parse(j["id"].get<std::string>());
    if (!id || id->isNil())
        return bad("invalid id");
    if (!j.contains("method") || !j["method"].is_string())
        return bad("missing method");
    const auto method = datumMethodFromString(j["method"].get<std::string>());
    if (!method)
        return Result<Datum>::failure(ErrorCode::FileVersionUnsupported,
                                      "This project uses a construction axis or plane this version of OpenShape does not support.",
                                      "unknown datum method '" + j["method"].get<std::string>() + "'");
    Datum d(*id);
    d.method = *method;
    if (j.contains("name")) {
        if (!j["name"].is_string())
            return bad("name is not a string");
        d.name_ = j["name"].get<std::string>();
    }
    if (j.contains("visible")) {
        if (!j["visible"].is_boolean())
            return bad("visible is not a boolean");
        d.visible_ = j["visible"].get<bool>();
    }
    if (j.contains("origin")) {
        const auto origin = intFrom(j, "origin");
        if (!origin || *origin < 0 || *origin > 2)
            return bad("invalid origin");
        d.originIndex = *origin;
    }
    if (j.contains("distance")) {
        const auto distance = numberFrom(j, "distance");
        if (!distance || std::abs(*distance) > 1e6)
            return bad("invalid distance");
        d.distance = *distance;
    }
    if (j.contains("angle")) {
        const auto angle = numberFrom(j, "angle");
        if (!angle || std::abs(*angle) > kPi + 1e-9)
            return bad("invalid angle");
        d.angle = *angle;
    }
    if (!j.contains("refs") || !j["refs"].is_array() || j["refs"].size() > 2)
        return bad("invalid refs");
    for (const auto& r : j["refs"]) {
        if (!r.is_object() || !r.contains("kind") || !r["kind"].is_string())
            return bad("reference without a kind");
        GeometryRef ref;
        bool known = false;
        for (const auto& [k, name] : kRefKindNames)
            if (name == r["kind"].get<std::string>()) {
                ref.kind = k;
                known = true;
            }
        if (!known)
            return bad("unknown reference kind");
        const auto body = r.contains("body") && r["body"].is_string() ? Uuid::parse(r["body"].get<std::string>()) : std::nullopt;
        const auto feature = r.contains("feature") && r["feature"].is_string() ? Uuid::parse(r["feature"].get<std::string>())
                                                                               : std::nullopt;
        if (!body || !feature)
            return bad("reference without body or step");
        ref.body = *body;
        ref.feature = *feature;
        if (ref.kind == GeometryRef::Kind::Face) {
            const auto face = faceRefFromJson(r, "face");
            if (!face)
                return bad("invalid face reference");
            ref.face = *face;
        } else {
            const auto edge = r.contains("edge") ? edgeRefFromJson(r["edge"]) : std::nullopt;
            if (!edge)
                return bad("invalid edge reference");
            ref.edge = *edge;
        }
        if (ref.kind == GeometryRef::Kind::Vertex) {
            const auto point = vecFromJson(r, "point");
            if (!point)
                return bad("a corner without its point");
            ref.point = *point;
        }
        d.refs.push_back(ref);
    }
    // What each method is made from.
    using K = GeometryRef::Kind;
    auto kinds = [&](std::initializer_list<std::initializer_list<K>> allowed) {
        if (d.refs.size() != allowed.size())
            return false;
        std::size_t i = 0;
        for (const auto& options : allowed) {
            if (std::find(options.begin(), options.end(), d.refs[i].kind) == options.end())
                return false;
            ++i;
        }
        return true;
    };
    bool fits = false;
    switch (d.method) {
    case DatumMethod::AxisThrough: fits = kinds({{K::Face, K::Edge}}); break;
    case DatumMethod::AxisAlongEdge: fits = kinds({{K::Edge}}); break;
    case DatumMethod::AxisTwoPoints: fits = kinds({{K::Vertex, K::Center}, {K::Vertex, K::Center}}); break;
    case DatumMethod::AxisParallel: fits = kinds({{K::Vertex, K::Center}}) && d.originIndex >= 0; break;
    case DatumMethod::PlaneOffset: fits = d.refs.empty() ? d.originIndex >= 0 : kinds({{K::Face}}); break;
    case DatumMethod::PlaneAngle: fits = kinds({{K::Edge}, {K::Face}}); break;
    case DatumMethod::PlaneMidway: fits = kinds({{K::Face}, {K::Face}}); break;
    }
    if (!fits)
        return bad("references do not fit the method");
    if (!j.contains("geometry") || !j["geometry"].is_object())
        return bad("missing geometry");
    const json& g = j["geometry"];
    const auto origin = vecFromJson(g, "origin");
    const auto direction = vecFromJson(g, "direction");
    const auto xAxis = vecFromJson(g, "xAxis");
    const auto center = vecFromJson(g, "center");
    const auto size = numberFrom(g, "size");
    if (!origin || !direction || !xAxis || !center || !size || direction->length() < 0.5 || *size < 0 || *size > 1e7)
        return bad("invalid geometry");
    d.geometry_ = {*origin, direction->normalized(), xAxis->length() > 0.5 ? xAxis->normalized() : Vec3{1, 0, 0}, *center, *size};
    return Result<Datum>::success(std::move(d));
}

} // namespace os::doc
