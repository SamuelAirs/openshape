#include "document/Feature.h"

#include "core/Log.h"
#include "document/Document.h"
#include "document/JsonHelpers.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace os::doc {

using nlohmann::json;

std::string_view toString(FeatureKind kind)
{
    switch (kind) {
    case FeatureKind::Box: return "Box";
    case FeatureKind::PushPull: return "PushPull";
    case FeatureKind::Fillet: return "Fillet";
    case FeatureKind::Chamfer: return "Chamfer";
    case FeatureKind::Extrude: return "Extrude";
    case FeatureKind::Shell: return "Shell";
    }
    return "Unknown";
}

std::optional<FeatureKind> featureKindFromString(std::string_view text)
{
    for (FeatureKind k : {FeatureKind::Box, FeatureKind::PushPull, FeatureKind::Fillet, FeatureKind::Chamfer,
                          FeatureKind::Extrude, FeatureKind::Shell})
        if (toString(k) == text)
            return k;
    return std::nullopt;
}

std::unique_ptr<Feature> createFeature(FeatureKind kind, Uuid id)
{
    switch (kind) {
    case FeatureKind::Box: return std::make_unique<BoxFeature>(id);
    case FeatureKind::PushPull: return std::make_unique<PushPullFeature>(id);
    case FeatureKind::Fillet: return std::make_unique<FilletFeature>(id);
    case FeatureKind::Chamfer: return std::make_unique<ChamferFeature>(id);
    case FeatureKind::Extrude: return std::make_unique<ExtrudeFeature>(id);
    case FeatureKind::Shell: return std::make_unique<ShellFeature>(id);
    }
    return nullptr;
}

const sketch::Sketch* EvalContext::sketch(const Uuid& id) const
{
    return document ? document->sketch(id) : nullptr;
}

std::optional<double> Feature::parameter(std::string_view key) const
{
    for (const auto& p : parameters())
        if (p.key == key)
            return p.value;
    return std::nullopt;
}

namespace {

Status unknownParameter(std::string_view key)
{
    return Status::failure(ErrorCode::InvalidArgument, "This value cannot be edited.",
                           "unknown parameter '" + std::string(key) + "'");
}

Status requirePositive(double value, const char* what)
{
    if (!(value > 0.0) || !std::isfinite(value))
        return Status::failure(ErrorCode::InvalidArgument, std::string(what) + " must be greater than zero.",
                               std::string(what) + " = " + std::to_string(value));
    return okStatus();
}

} // namespace

// ---- Box ----------------------------------------------------------------------

Result<geom::Shape> BoxFeature::compute(const geom::Shape&, const EvalContext&) const
{
    return geom::makeBox(origin, size);
}

std::vector<ParameterInfo> BoxFeature::parameters() const
{
    return {{"width", "Width", ParameterKind::Length, size.x},
            {"depth", "Depth", ParameterKind::Length, size.y},
            {"height", "Height", ParameterKind::Length, size.z}};
}

Status BoxFeature::setParameter(std::string_view key, double value)
{
    double* target = key == "width" ? &size.x : key == "depth" ? &size.y : key == "height" ? &size.z : nullptr;
    if (!target)
        return unknownParameter(key);
    if (auto s = requirePositive(value, "Box size"); !s)
        return s;
    *target = value;
    return okStatus();
}

void BoxFeature::writeParams(json& out) const
{
    out["origin"] = vecToJson(origin);
    out["size"] = vecToJson(size);
}

Status BoxFeature::readParams(const json& in)
{
    auto o = vecFromJson(in, "origin");
    auto s = vecFromJson(in, "size");
    if (!o || !s)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid box.", "Box: bad origin/size");
    if (s->x <= 0 || s->y <= 0 || s->z <= 0)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid box.", "Box: non-positive size");
    origin = *o;
    size = *s;
    return okStatus();
}

// ---- Push/pull ----------------------------------------------------------------

Result<geom::Shape> PushPullFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    const auto index = geom::resolveFace(input, face.signature, face.indexHint);
    if (!index) {
        OS_LOG(Warning, Document) << "PushPull " << id().toString() << ": face reference did not resolve (hint "
                                  << face.indexHint << ")";
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                            "The face this operation was applied to no longer exists.",
                                            "PushPull face reference unresolved");
    }
    return geom::pushPullFace(input, *index, distance);
}

std::vector<ParameterInfo> PushPullFeature::parameters() const
{
    return {{"distance", "Distance", ParameterKind::Length, distance}};
}

Status PushPullFeature::setParameter(std::string_view key, double value)
{
    if (key != "distance")
        return unknownParameter(key);
    if (!std::isfinite(value))
        return Status::failure(ErrorCode::InvalidArgument, "Enter a valid distance.", "non-finite distance");
    distance = value;
    return okStatus();
}

void PushPullFeature::writeParams(json& out) const
{
    out["face"] = faceRefToJson(face);
    out["distance"] = distance;
}

Status PushPullFeature::readParams(const json& in)
{
    auto ref = faceRefFromJson(in, "face");
    if (!ref || !in.contains("distance") || !in["distance"].is_number())
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid push/pull.", "PushPull: bad params");
    face = *ref;
    distance = in["distance"].get<double>();
    return okStatus();
}

// ---- Fillet / chamfer -----------------------------------------------------------

Result<std::vector<int>> EdgeTreatmentFeature::resolveEdges(const geom::Shape& input) const
{
    std::vector<int> indices;
    for (const auto& ref : edges) {
        const auto index = geom::resolveEdge(input, ref.signature, ref.indexHint);
        if (!index)
            return Result<std::vector<int>>::failure(ErrorCode::InvalidReference,
                                                     "An edge this operation was applied to no longer exists.",
                                                     "edge reference unresolved (hint " + std::to_string(ref.indexHint) + ")");
        indices.push_back(*index);
    }
    return Result<std::vector<int>>::success(std::move(indices));
}

std::vector<ParameterInfo> EdgeTreatmentFeature::parameters() const
{
    return {{"size", sizeLabel(), ParameterKind::Length, size}};
}

Status EdgeTreatmentFeature::setParameter(std::string_view key, double value)
{
    if (key != "size")
        return unknownParameter(key);
    if (auto s = requirePositive(value, sizeLabel()); !s)
        return s;
    size = value;
    return okStatus();
}

void EdgeTreatmentFeature::writeParams(json& out) const
{
    json list = json::array();
    for (const auto& e : edges)
        list.push_back(edgeRefToJson(e));
    out["edges"] = list;
    out["size"] = size;
}

Status EdgeTreatmentFeature::readParams(const json& in)
{
    if (!in.contains("edges") || !in["edges"].is_array() || !in.contains("size") || !in["size"].is_number())
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid edge feature.", "edges/size missing");
    std::vector<EdgeRef> refs;
    for (const auto& e : in["edges"]) {
        auto ref = edgeRefFromJson(e);
        if (!ref)
            return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid edge feature.", "bad edge ref");
        refs.push_back(*ref);
    }
    const double s = in["size"].get<double>();
    if (!(s > 0))
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid edge feature.", "size <= 0");
    edges = std::move(refs);
    size = s;
    return okStatus();
}

Result<geom::Shape> FilletFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    auto indices = resolveEdges(input);
    if (!indices)
        return Result<geom::Shape>::failureFrom(indices);
    return geom::filletEdges(input, indices.value(), size);
}

Result<geom::Shape> ChamferFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    auto indices = resolveEdges(input);
    if (!indices)
        return Result<geom::Shape>::failureFrom(indices);
    return geom::chamferEdges(input, indices.value(), size);
}

// ---- Shell ----------------------------------------------------------------------

Result<geom::Shape> ShellFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    std::vector<int> indices;
    for (const FaceRef& ref : faces) {
        const auto index = geom::resolveFace(input, ref.signature, ref.indexHint);
        if (!index)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                                "A face this shell opens no longer exists.", "Shell face unresolved");
        indices.push_back(*index);
    }
    return geom::shell(input, indices, thickness);
}

std::vector<ParameterInfo> ShellFeature::parameters() const
{
    return {{"thickness", "Wall", ParameterKind::Length, thickness}};
}

Status ShellFeature::setParameter(std::string_view key, double value)
{
    if (key != "thickness")
        return unknownParameter(key);
    if (auto s = requirePositive(value, "Wall thickness"); !s)
        return s;
    thickness = value;
    return okStatus();
}

void ShellFeature::writeParams(json& out) const
{
    json list = json::array();
    for (const auto& f : faces)
        list.push_back(faceRefToJson(f));
    out["faces"] = list;
    out["thickness"] = thickness;
}

Status ShellFeature::readParams(const json& in)
{
    const auto t = numberFrom(in, "thickness");
    if (!t || !(*t > 0) || !in.contains("faces") || !in["faces"].is_array() || in["faces"].empty())
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid shell.", "Shell: bad params");
    std::vector<FaceRef> refs;
    for (const auto& f : in["faces"]) {
        const json wrapper = {{"face", f}};
        auto ref = faceRefFromJson(wrapper, "face");
        if (!ref)
            return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid shell.", "Shell: bad face");
        refs.push_back(*ref);
    }
    faces = std::move(refs);
    thickness = *t;
    return okStatus();
}

// ---- Extrude --------------------------------------------------------------------

std::string_view toString(ExtrudeMode mode)
{
    switch (mode) {
    case ExtrudeMode::NewBody: return "NewBody";
    case ExtrudeMode::Join: return "Join";
    case ExtrudeMode::Cut: return "Cut";
    }
    return "NewBody";
}

Result<geom::Shape> ExtrudeFeature::toolSolid(const geom::Shape& input, const EvalContext& context) const
{
    const sketch::Sketch* sk = context.sketch(sketchId);
    if (!sk)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The sketch for this extrusion no longer exists.",
                                            "Extrude: sketch " + sketchId.toString() + " not found");
    if (profiles.empty())
        return Result<geom::Shape>::failure(ErrorCode::InvalidArgument, "Select a closed shape to extrude.",
                                            "Extrude: no profiles");
    const sketch::Plane plane = effectivePlane(*sk, context);
    auto regions = sketchRegions(*sk, plane);
    if (!regions)
        return Result<geom::Shape>::failureFrom(regions);
    std::vector<geom::Shape> faces;
    for (const ProfileRef& ref : profiles) {
        const auto index = resolveProfile(regions.value(), plane, ref);
        if (!index)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                                "A shape this extrusion used is no longer closed or no longer exists.",
                                                "Extrude: profile reference unresolved");
        faces.push_back(regions.value()[std::size_t(*index)].face);
    }
    double length = distance;
    if (throughAll && mode == ExtrudeMode::Cut && !input.isNull()) {
        const auto box = geom::boundingBox(input);
        if (box.valid) {
            // Far enough to leave the body from anywhere on the sketch plane.
            const double reach = box.size().length() + (box.center() - plane.origin).length() + 1.0;
            length = (distance < 0 ? -1.0 : 1.0) * std::max(std::abs(distance), reach);
        }
    }
    return geom::extrudeFaces(faces, plane.normal() * length);
}

Result<geom::Shape> ExtrudeFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    auto tool = toolSolid(input, context);
    if (!tool)
        return tool;
    switch (mode) {
    case ExtrudeMode::NewBody: return tool;
    case ExtrudeMode::Join: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Union);
    case ExtrudeMode::Cut: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Subtract);
    }
    return tool;
}

std::vector<ParameterInfo> ExtrudeFeature::parameters() const
{
    return {{"distance", "Distance", ParameterKind::Length, distance}};
}

Status ExtrudeFeature::setParameter(std::string_view key, double value)
{
    if (key != "distance")
        return unknownParameter(key);
    if (!std::isfinite(value) || std::abs(value) < 1e-6)
        return Status::failure(ErrorCode::InvalidArgument, "The extrusion distance must not be zero.", "distance ~ 0");
    distance = value;
    return okStatus();
}

void ExtrudeFeature::writeParams(json& out) const
{
    json refs = json::array();
    for (const auto& r : profiles)
        refs.push_back({{"point", json::array({r.interiorPoint.x, r.interiorPoint.y})}, {"area", r.area}});
    out["sketch"] = sketchId.toString();
    out["profiles"] = refs;
    out["distance"] = distance;
    out["mode"] = std::string(toString(mode));
    out["throughAll"] = throughAll;
}

Status ExtrudeFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid extrusion.", why);
    };
    if (!in.contains("sketch") || !in["sketch"].is_string())
        return bad("Extrude: missing sketch");
    const auto id = Uuid::parse(in["sketch"].get<std::string>());
    const auto d = numberFrom(in, "distance");
    if (!id || !d || std::abs(*d) < 1e-6 || !in.contains("profiles") || !in["profiles"].is_array()
        || !in.contains("mode") || !in["mode"].is_string())
        return bad("Extrude: bad fields");
    std::vector<ProfileRef> refs;
    for (const auto& r : in["profiles"]) {
        if (!r.is_object() || !r.contains("point") || !r["point"].is_array() || r["point"].size() != 2
            || !r["point"][0].is_number() || !r["point"][1].is_number())
            return bad("Extrude: bad profile");
        const auto area = numberFrom(r, "area");
        if (!area)
            return bad("Extrude: bad profile area");
        refs.push_back({{r["point"][0].get<double>(), r["point"][1].get<double>()}, *area});
    }
    const std::string m = in["mode"].get<std::string>();
    if (m == "NewBody")
        mode = ExtrudeMode::NewBody;
    else if (m == "Join")
        mode = ExtrudeMode::Join;
    else if (m == "Cut")
        mode = ExtrudeMode::Cut;
    else
        return bad("Extrude: unknown mode");
    sketchId = *id;
    profiles = std::move(refs);
    distance = *d;
    throughAll = in.contains("throughAll") && in["throughAll"].is_boolean() && in["throughAll"].get<bool>();
    return okStatus();
}

} // namespace os::doc
