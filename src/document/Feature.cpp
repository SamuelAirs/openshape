// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/Feature.h"

#include "core/Log.h"
#include "document/Document.h"
#include "document/JsonHelpers.h"
#include "document/SketchProfiles.h"
#include "geometry/Holes.h"
#include "geometry/Loft.h"
#include "geometry/Modeling.h"
#include "geometry/Text.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

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
    case FeatureKind::Move: return "Move";
    case FeatureKind::Combine: return "Combine";
    case FeatureKind::Revolve: return "Revolve";
    case FeatureKind::Hole: return "Hole";
    case FeatureKind::Mirror: return "Mirror";
    case FeatureKind::Pattern: return "Pattern";
    case FeatureKind::DeleteFaces: return "DeleteFaces";
    case FeatureKind::OffsetFace: return "OffsetFace";
    case FeatureKind::Split: return "Split";
    case FeatureKind::SplitPiece: return "SplitPiece";
    case FeatureKind::Copy: return "Copy";
    case FeatureKind::Holes: return "Holes";
    case FeatureKind::Imported: return "Imported";
    case FeatureKind::Text: return "Text";
    case FeatureKind::Loft: return "Loft";
    }
    return "Unknown";
}

std::optional<FeatureKind> featureKindFromString(std::string_view text)
{
    for (FeatureKind k : {FeatureKind::Box, FeatureKind::PushPull, FeatureKind::Fillet, FeatureKind::Chamfer,
                          FeatureKind::Extrude, FeatureKind::Shell, FeatureKind::Move, FeatureKind::Combine,
                          FeatureKind::Revolve, FeatureKind::Hole, FeatureKind::Mirror, FeatureKind::Pattern,
                          FeatureKind::DeleteFaces, FeatureKind::OffsetFace, FeatureKind::Split, FeatureKind::SplitPiece,
                          FeatureKind::Copy, FeatureKind::Holes, FeatureKind::Imported, FeatureKind::Text,
                          FeatureKind::Loft})
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
    case FeatureKind::Move: return std::make_unique<MoveFeature>(id);
    case FeatureKind::Combine: return std::make_unique<CombineFeature>(id);
    case FeatureKind::Revolve: return std::make_unique<RevolveFeature>(id);
    case FeatureKind::Hole: return std::make_unique<HoleFeature>(id);
    case FeatureKind::Mirror: return std::make_unique<MirrorFeature>(id);
    case FeatureKind::Pattern: return std::make_unique<PatternFeature>(id);
    case FeatureKind::DeleteFaces: return std::make_unique<DeleteFacesFeature>(id);
    case FeatureKind::OffsetFace: return std::make_unique<OffsetFaceFeature>(id);
    case FeatureKind::Split: return std::make_unique<SplitFeature>(id);
    case FeatureKind::SplitPiece: return std::make_unique<SplitPieceFeature>(id);
    case FeatureKind::Copy: return std::make_unique<CopyFeature>(id);
    case FeatureKind::Holes: return std::make_unique<HolesFeature>(id);
    case FeatureKind::Imported: return std::make_unique<ImportedFeature>(id);
    case FeatureKind::Text: return std::make_unique<TextFeature>(id);
    case FeatureKind::Loft: return std::make_unique<LoftFeature>(id);
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

std::optional<std::string> Feature::textParameter(std::string_view key) const
{
    for (const auto& p : textParameters())
        if (p.key == key)
            return p.value;
    return std::nullopt;
}

Status Feature::setTextParameter(std::string_view key, const std::string&)
{
    return Status::failure(ErrorCode::InvalidArgument, "This value cannot be edited.",
                           "unknown text parameter '" + std::string(key) + "'");
}

std::unique_ptr<Feature> Feature::cloneWithNewId() const
{
    auto copy = clone();
    copy->id_ = Uuid::generate();
    return copy;
}

namespace {

// Duplicating a body: `id` becomes its copy's id when that object was copied too.
void remap(Uuid& id, const std::map<Uuid, Uuid>& copies)
{
    if (const auto it = copies.find(id); it != copies.end())
        id = it->second;
}

Status unknownParameter(std::string_view key)
{
    return Status::failure(ErrorCode::InvalidArgument, "This value cannot be edited.",
                           "unknown parameter '" + std::string(key) + "'");
}

// Replaces the kernel layer's generic wording for one failure kind with
// wording about this step ("This cut does not reach the body" rather than
// "The shapes do not overlap").
Result<geom::Shape> reworded(Result<geom::Shape> result, ErrorCode code, const char* userMessage)
{
    if (!result && result.error() == code)
        return Result<geom::Shape>::failure(code, userMessage, result.developerMessage());
    return result;
}

// Previews name a size that works, in the document's unit; recompute does not.
geom::SizeAdvice sizeAdvice(const EvalContext& context)
{
    return {context.interactive, context.document ? context.document->displayUnit() : LengthUnit::Millimeter};
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
    if (keepEdges) {
        auto moved = geom::pushPullFaceKeepingEdges(input, *index, distance);
        if (moved)
            return moved;
        // Not applicable here (no rounded edges, or not straight walls below
        // them): the plain push/pull.
        OS_LOG(Debug, Document) << "PushPull keeping edges not used: " << moved.developerMessage();
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
    if (keepEdges)
        out["keepEdges"] = true;
}

Status PushPullFeature::readParams(const json& in)
{
    auto ref = faceRefFromJson(in, "face");
    if (!ref || !in.contains("distance") || !in["distance"].is_number())
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid push/pull.", "PushPull: bad params");
    face = *ref;
    distance = in["distance"].get<double>();
    keepEdges = in.contains("keepEdges") && in["keepEdges"].is_boolean() && in["keepEdges"].get<bool>();
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

Result<geom::Shape> FilletFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    auto indices = resolveEdges(input);
    if (!indices)
        return Result<geom::Shape>::failureFrom(indices);
    return geom::filletEdges(input, indices.value(), size, sizeAdvice(context));
}

Result<geom::Shape> ChamferFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    auto indices = resolveEdges(input);
    if (!indices)
        return Result<geom::Shape>::failureFrom(indices);
    return geom::chamferEdges(input, indices.value(), size, sizeAdvice(context));
}

// ---- Combine --------------------------------------------------------------------

std::string_view toString(CombineMode mode)
{
    switch (mode) {
    case CombineMode::Union: return "Union";
    case CombineMode::Subtract: return "Subtract";
    case CombineMode::Intersect: return "Intersect";
    }
    return "Union";
}

Result<geom::Shape> CombineFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    const Body* tool = context.document ? context.document->body(toolBody) : nullptr;
    if (!tool || tool->shape().isNull())
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body this step combines with no longer exists.",
                                            "Combine: tool body " + toolBody.toString() + " missing");
    const geom::BooleanKind kind = mode == CombineMode::Union      ? geom::BooleanKind::Union
                                 : mode == CombineMode::Subtract ? geom::BooleanKind::Subtract
                                                                 : geom::BooleanKind::Intersect;
    return reworded(geom::booleanOp(input, tool->shape(), kind), ErrorCode::NoEffect,
                    "The bodies do not overlap, so nothing would be cut away. Move one into the other first.");
}

Status CombineFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void CombineFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    remap(toolBody, copies);
}

void CombineFeature::writeParams(json& out) const
{
    out["tool"] = toolBody.toString();
    out["mode"] = std::string(toString(mode));
}

Status CombineFeature::readParams(const json& in)
{
    const auto tool = in.contains("tool") && in["tool"].is_string() ? Uuid::parse(in["tool"].get<std::string>()) : std::nullopt;
    const std::string m = in.contains("mode") && in["mode"].is_string() ? in["mode"].get<std::string>() : std::string();
    if (!tool || (m != "Union" && m != "Subtract" && m != "Intersect"))
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid combine step.", "Combine: bad params");
    toolBody = *tool;
    mode = m == "Union" ? CombineMode::Union : m == "Subtract" ? CombineMode::Subtract : CombineMode::Intersect;
    return okStatus();
}

// ---- Move -----------------------------------------------------------------------

geom::RigidMotion MoveFeature::motion() const
{
    geom::RigidMotion m;
    m.translation = translation;
    if (rotates) {
        m.center = rotationCenter;
        m.axis = rotationAxis;
        m.angle = rotationAngle;
    }
    return m;
}

void MoveFeature::setMotion(const geom::RigidMotion& m)
{
    translation = m.translation;
    rotates = std::abs(m.angle) > 0;
    rotationCenter = rotates ? m.center : Vec3{};
    rotationAxis = rotates ? m.axis : Vec3{0, 0, 1};
    rotationAngle = rotates ? m.angle : 0.0;
}

Result<geom::Shape> MoveFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    const geom::RigidMotion m = motion();
    if (m.isIdentity())
        return Result<geom::Shape>::success(input);
    if (std::abs(m.angle) < 1e-12)
        return geom::translated(input, translation);
    return geom::transformed(input, m);
}

std::vector<ParameterInfo> MoveFeature::parameters() const
{
    std::vector<ParameterInfo> out{{"x", "X", ParameterKind::Length, translation.x},
                                   {"y", "Y", ParameterKind::Length, translation.y},
                                   {"z", "Z", ParameterKind::Length, translation.z}};
    if (rotates)
        out.push_back({"angle", "Angle", ParameterKind::Angle, rotationAngle});
    return out;
}

Status MoveFeature::setParameter(std::string_view key, double value)
{
    if (!std::isfinite(value))
        return Status::failure(ErrorCode::InvalidArgument, "Enter a valid value.", "non-finite move parameter");
    if (key == "angle" && rotates) {
        rotationAngle = value;
        return okStatus();
    }
    double* target = key == "x" ? &translation.x : key == "y" ? &translation.y : key == "z" ? &translation.z : nullptr;
    if (!target)
        return unknownParameter(key);
    *target = value;
    return okStatus();
}

void MoveFeature::writeParams(json& out) const
{
    out["translation"] = vecToJson(translation);
    if (rotates)
        out["rotation"] = json{{"center", vecToJson(rotationCenter)},
                               {"axis", vecToJson(rotationAxis)},
                               {"angle", rotationAngle}};
}

Status MoveFeature::readParams(const json& in)
{
    const auto t = vecFromJson(in, "translation");
    if (!t)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid move.", "Move: bad translation");
    translation = *t;
    rotates = false;
    if (in.contains("rotation")) {
        const json& r = in["rotation"];
        const auto center = r.is_object() ? vecFromJson(r, "center") : std::nullopt;
        const auto axis = r.is_object() ? vecFromJson(r, "axis") : std::nullopt;
        if (!center || !axis || axis->length() < 1e-9 || !r.contains("angle") || !r["angle"].is_number()
            || !std::isfinite(r["angle"].get<double>()))
            return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid rotation.",
                                   "Move: bad rotation");
        rotates = true;
        rotationCenter = *center;
        rotationAxis = *axis;
        rotationAngle = r["angle"].get<double>();
    }
    return okStatus();
}

// ---- Delete faces ---------------------------------------------------------------

Result<geom::Shape> DeleteFacesFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    std::vector<int> indices;
    for (const FaceRef& ref : faces) {
        const auto index = geom::resolveFace(input, ref.signature, ref.indexHint);
        if (!index)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "A face this step removed no longer exists.",
                                                "DeleteFaces: face reference unresolved");
        indices.push_back(*index);
    }
    return geom::deleteFaces(input, indices);
}

Status DeleteFacesFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void DeleteFacesFeature::writeParams(json& out) const
{
    json list = json::array();
    for (const FaceRef& ref : faces)
        list.push_back(faceRefToJson(ref));
    out["faces"] = list;
}

Status DeleteFacesFeature::readParams(const json& in)
{
    if (!in.contains("faces") || !in["faces"].is_array() || in["faces"].empty())
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid face removal.", "DeleteFaces: faces");
    faces.clear();
    for (const auto& item : in["faces"]) {
        const json wrapper{{"face", item}};
        const auto ref = faceRefFromJson(wrapper, "face");
        if (!ref)
            return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid face removal.",
                                   "DeleteFaces: bad face ref");
        faces.push_back(*ref);
    }
    return okStatus();
}

// ---- Offset face ----------------------------------------------------------------

Result<geom::Shape> OffsetFaceFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    const auto index = geom::resolveFace(input, face.signature, face.indexHint);
    if (!index)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The face this step moved no longer exists.",
                                            "OffsetFace: face reference unresolved");
    return geom::offsetFace(input, *index, distance);
}

std::vector<ParameterInfo> OffsetFaceFeature::parameters() const
{
    return {{"distance", "Offset", ParameterKind::Length, distance}};
}

Status OffsetFaceFeature::setParameter(std::string_view key, double value)
{
    if (key != "distance")
        return unknownParameter(key);
    if (!std::isfinite(value))
        return Status::failure(ErrorCode::InvalidArgument, "Enter a valid distance.", "non-finite offset");
    distance = value;
    return okStatus();
}

void OffsetFaceFeature::writeParams(json& out) const
{
    out["face"] = faceRefToJson(face);
    out["distance"] = distance;
}

Status OffsetFaceFeature::readParams(const json& in)
{
    auto ref = faceRefFromJson(in, "face");
    if (!ref || !in.contains("distance") || !in["distance"].is_number())
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid face offset.", "OffsetFace: params");
    face = *ref;
    distance = in["distance"].get<double>();
    return okStatus();
}

// ---- Mirror ---------------------------------------------------------------------

Result<geom::Shape> MirrorFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    if (keepOriginal)
        return geom::mirrorJoined(input, planeOrigin, planeNormal);
    return geom::mirrored(input, planeOrigin, planeNormal);
}

Status MirrorFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void MirrorFeature::writeParams(json& out) const
{
    const json plane{{"origin", vecToJson(planeOrigin)}, {"normal", vecToJson(planeNormal)}};
    if (keepOriginal) {
        out["origin"] = plane["origin"];
        out["normal"] = plane["normal"];
        return;
    }
    // The image alone: the plane goes under "plane", so builds that predate
    // this mode refuse the file instead of joining the image to the body.
    out["keepOriginal"] = false;
    out["plane"] = plane;
}

Status MirrorFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid mirror.", std::string("Mirror: ") + why);
    };
    bool keep = true;
    if (in.contains("keepOriginal")) {
        if (!in["keepOriginal"].is_boolean())
            return bad("keepOriginal is not a boolean");
        keep = in["keepOriginal"].get<bool>();
    }
    // Joined: the plane at the top level; the image alone: under "plane" (never both).
    if (!keep && (in.contains("origin") || in.contains("normal")))
        return bad("image-only mirror with a top-level plane");
    if (keep && in.contains("plane"))
        return bad("joined mirror with a nested plane");
    const json* source = &in;
    if (!keep) {
        if (!in.contains("plane") || !in["plane"].is_object())
            return bad("image-only mirror without its plane");
        source = &in["plane"];
    }
    const auto origin = vecFromJson(*source, "origin");
    const auto normal = vecFromJson(*source, "normal");
    if (!origin || !normal || normal->length() < 1e-9)
        return bad("bad plane");
    planeOrigin = *origin;
    planeNormal = *normal;
    keepOriginal = keep;
    return okStatus();
}

// ---- Pattern --------------------------------------------------------------------

std::vector<geom::RigidMotion> PatternFeature::copies() const
{
    std::vector<geom::RigidMotion> out;
    for (int k = 1; k < count; ++k) {
        geom::RigidMotion m;
        if (layout == Layout::Linear) {
            m.translation = direction.normalized() * (spacing * k);
        } else {
            // A full turn spaces copies evenly without doubling the original;
            // a partial sweep puts the last copy at its end.
            const bool fullTurn = std::abs(angle - 2 * kPi) < 1e-9;
            const double step = fullTurn ? angle / count : angle / std::max(count - 1, 1);
            m.center = axisOrigin;
            m.axis = axis;
            m.angle = step * k;
        }
        out.push_back(m);
    }
    return out;
}

Result<geom::Shape> PatternFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    if (count < 1)
        return Result<geom::Shape>::failure(ErrorCode::InvalidArgument, "A pattern needs at least one copy.",
                                            "Pattern: count < 1");
    if (count == 1)
        return Result<geom::Shape>::success(input);
    if (layout == Layout::Linear && std::abs(spacing) < 1e-6)
        return Result<geom::Shape>::failure(ErrorCode::NoEffect,
                                            "With no spacing the copies would all be in the same place. Set a spacing.",
                                            "Pattern: spacing 0");
    return geom::repeatJoined(input, copies());
}

std::vector<ParameterInfo> PatternFeature::parameters() const
{
    std::vector<ParameterInfo> out{{"count", "Count", ParameterKind::Count, double(count)}};
    if (layout == Layout::Linear)
        out.push_back({"spacing", "Spacing", ParameterKind::Length, spacing});
    else
        out.push_back({"angle", "Angle", ParameterKind::Angle, angle});
    return out;
}

Status PatternFeature::setParameter(std::string_view key, double value)
{
    if (!std::isfinite(value))
        return Status::failure(ErrorCode::InvalidArgument, "Enter a valid value.", "non-finite pattern parameter");
    if (key == "count") {
        const long n = std::lround(value);
        if (n < 1 || n > 500)
            return Status::failure(ErrorCode::InvalidArgument, "Use between 1 and 500 copies.", "pattern count out of range");
        count = int(n);
        return okStatus();
    }
    if (key == "spacing" && layout == Layout::Linear) {
        spacing = value;
        return okStatus();
    }
    if (key == "angle" && layout == Layout::Circular) {
        if (std::abs(value) < 1e-9 || std::abs(value) > 2 * kPi + 1e-9)
            return Status::failure(ErrorCode::InvalidArgument, "The angle must be between 0\xC2\xB0 and 360\xC2\xB0.",
                                   "pattern angle out of range");
        angle = value;
        return okStatus();
    }
    return unknownParameter(key);
}

void PatternFeature::writeParams(json& out) const
{
    out["layout"] = layout == Layout::Linear ? "Linear" : "Circular";
    out["count"] = count;
    if (layout == Layout::Linear) {
        out["direction"] = vecToJson(direction);
        out["spacing"] = spacing;
    } else {
        out["axisOrigin"] = vecToJson(axisOrigin);
        out["axis"] = vecToJson(axis);
        out["angle"] = angle;
    }
}

Status PatternFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid pattern.", std::string("Pattern: ") + why);
    };
    if (!in.contains("layout") || !in["layout"].is_string() || !in.contains("count") || !in["count"].is_number_integer())
        return bad("layout/count");
    const std::string kind = in["layout"].get<std::string>();
    count = in["count"].get<int>();
    if (count < 1 || count > 500)
        return bad("count out of range");
    if (kind == "Linear") {
        layout = Layout::Linear;
        const auto d = vecFromJson(in, "direction");
        if (!d || d->length() < 1e-9 || !in.contains("spacing") || !in["spacing"].is_number())
            return bad("direction/spacing");
        direction = *d;
        spacing = in["spacing"].get<double>();
    } else if (kind == "Circular") {
        layout = Layout::Circular;
        const auto o = vecFromJson(in, "axisOrigin");
        const auto a = vecFromJson(in, "axis");
        if (!o || !a || a->length() < 1e-9 || !in.contains("angle") || !in["angle"].is_number())
            return bad("axis/angle");
        axisOrigin = *o;
        axis = *a;
        angle = in["angle"].get<double>();
    } else {
        return bad("unknown layout");
    }
    return okStatus();
}

// ---- Split into bodies ------------------------------------------------------------

namespace {

json solidToJson(const geom::SolidSignature& s)
{
    return {{"volume", s.volume}, {"centroid", vecToJson(s.centroid)}, {"min", vecToJson(s.min)}, {"max", vecToJson(s.max)}};
}

std::optional<geom::SolidSignature> solidFromJson(const json& in)
{
    const auto volume = numberFrom(in, "volume");
    const auto centroid = vecFromJson(in, "centroid");
    const auto min = vecFromJson(in, "min");
    const auto max = vecFromJson(in, "max");
    if (!volume || !(*volume > 0) || !centroid || !min || !max)
        return std::nullopt;
    return geom::SolidSignature{*volume, *centroid, *min, *max};
}

std::vector<geom::SolidSignature> signaturesOf(const std::vector<geom::Shape>& solids)
{
    std::vector<geom::SolidSignature> out;
    out.reserve(solids.size());
    for (const geom::Shape& s : solids)
        out.push_back(geom::solidSignature(s));
    return out;
}

} // namespace

std::vector<int> SplitFeature::assign(const std::vector<geom::Shape>& solids) const
{
    return geom::matchSolids(signaturesOf(solids), pieces);
}

Result<geom::Shape> SplitFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    if (pieces.empty())
        return Result<geom::Shape>::failure(ErrorCode::InvalidArgument, "This split has no pieces.", "Split: no pieces");
    const std::vector<geom::Shape> solids = geom::solids(input);
    const std::vector<int> owner = assign(solids);
    if (owner[0] < 0)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                            "The piece this body kept after the split no longer exists.",
                                            "Split: kept piece unresolved among " + std::to_string(solids.size()) + " solids");
    // The kept piece, plus pieces nobody claims (they appeared after the
    // split): they stay here rather than vanish.
    std::vector<geom::Shape> kept{solids[std::size_t(owner[0])]};
    for (std::size_t j = 0; j < solids.size(); ++j)
        if (std::find(owner.begin(), owner.end(), static_cast<int>(j)) == owner.end())
            kept.push_back(solids[j]);
    return geom::gatherSolids(kept);
}

Status SplitFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void SplitFeature::writeParams(json& out) const
{
    json list = json::array();
    for (const auto& p : pieces)
        list.push_back(solidToJson(p));
    out["pieces"] = list;
}

Status SplitFeature::readParams(const json& in)
{
    if (!in.contains("pieces") || !in["pieces"].is_array() || in["pieces"].size() < 2 || in["pieces"].size() > 10000)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid split.", "Split: pieces");
    std::vector<geom::SolidSignature> list;
    for (const auto& p : in["pieces"]) {
        const auto signature = solidFromJson(p);
        if (!signature)
            return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid split.", "Split: bad piece");
        list.push_back(*signature);
    }
    pieces = std::move(list);
    return okStatus();
}

Result<geom::Shape> SplitPieceFeature::compute(const geom::Shape&, const EvalContext& context) const
{
    using R = Result<geom::Shape>;
    const Body* source = context.document ? context.document->body(sourceBody) : nullptr;
    if (!source)
        return R::failure(ErrorCode::InvalidReference, "The body this piece was split from no longer exists.",
                          "SplitPiece: source body " + sourceBody.toString() + " missing");
    const int index = source->featureIndex(splitFeature);
    const auto* split = index >= 0 ? dynamic_cast<const SplitFeature*>(source->features()[std::size_t(index)].get()) : nullptr;
    if (!split)
        return R::failure(ErrorCode::InvalidReference,
                          "The split this piece came from was deleted from " + source->name() + ".",
                          "SplitPiece: split step " + splitFeature.toString() + " missing");
    if (split->isSuppressed())
        return R::failure(ErrorCode::InvalidReference,
                          "The split of " + source->name() + " is suppressed, so this piece is still part of it.",
                          "SplitPiece: split suppressed");
    if (piece < 1 || piece >= static_cast<int>(split->pieces.size()))
        return R::failure(ErrorCode::InvalidReference, "This piece no longer exists.", "SplitPiece: piece index out of range");
    const geom::Shape shape = source->shapeBefore(index);
    if (shape.isNull())
        return R::failure(ErrorCode::InvalidReference,
                          "This piece cannot be built because " + source->name() + " could not be built.",
                          "SplitPiece: source shape before the split is null");
    const std::vector<geom::Shape> solids = geom::solids(shape);
    const std::vector<int> owner = split->assign(solids);
    const int mine = owner[std::size_t(piece)];
    if (mine < 0)
        return R::failure(ErrorCode::InvalidReference,
                          "This piece is no longer separate from " + source->name() + ", or no longer exists.",
                          "SplitPiece: piece " + std::to_string(piece) + " unresolved among " + std::to_string(solids.size())
                              + " solids");
    return R::success(solids[std::size_t(mine)]);
}

Status SplitPieceFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void SplitPieceFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    remap(sourceBody, copies);
    remap(splitFeature, copies);
}

void SplitPieceFeature::writeParams(json& out) const
{
    out["body"] = sourceBody.toString();
    out["split"] = splitFeature.toString();
    out["piece"] = piece;
}

Status SplitPieceFeature::readParams(const json& in)
{
    const auto body = in.contains("body") && in["body"].is_string() ? Uuid::parse(in["body"].get<std::string>()) : std::nullopt;
    const auto split = in.contains("split") && in["split"].is_string() ? Uuid::parse(in["split"].get<std::string>()) : std::nullopt;
    const auto index = intFrom(in, "piece");
    if (!body || !split || !index || *index < 1)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid split-off piece.", "SplitPiece: params");
    sourceBody = *body;
    splitFeature = *split;
    piece = *index;
    return okStatus();
}

// ---- Copy (Mirror / Pattern as separate bodies) ------------------------------------

Result<geom::Shape> CopyFeature::compute(const geom::Shape&, const EvalContext& context) const
{
    using R = Result<geom::Shape>;
    const Body* source = context.document ? context.document->body(sourceBody) : nullptr;
    if (!source)
        return R::failure(ErrorCode::InvalidReference, "The body this is a copy of no longer exists.",
                          "Copy: source body " + sourceBody.toString() + " missing");
    const geom::Shape& shape = source->shape();
    if (shape.isNull())
        return R::failure(ErrorCode::InvalidReference,
                          "This copy cannot be built because " + source->name() + " could not be built.",
                          "Copy: source shape is null");
    if (mirror)
        return geom::mirrored(shape, planeOrigin, planeNormal);
    if (motion.isIdentity())
        return R::success(shape);
    return geom::transformed(shape, motion);
}

Status CopyFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void CopyFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    remap(sourceBody, copies);
}

void CopyFeature::writeParams(json& out) const
{
    out["body"] = sourceBody.toString();
    if (mirror) {
        out["mirror"] = json{{"origin", vecToJson(planeOrigin)}, {"normal", vecToJson(planeNormal)}};
        return;
    }
    out["translation"] = vecToJson(motion.translation);
    if (std::abs(motion.angle) > 0)
        out["rotation"] = json{{"center", vecToJson(motion.center)}, {"axis", vecToJson(motion.axis)}, {"angle", motion.angle}};
}

Status CopyFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid copy of a body.", std::string("Copy: ") + why);
    };
    const auto body = in.contains("body") && in["body"].is_string() ? Uuid::parse(in["body"].get<std::string>()) : std::nullopt;
    if (!body)
        return bad("body");
    if (in.contains("mirror")) {
        const json& m = in["mirror"];
        const auto origin = m.is_object() ? vecFromJson(m, "origin") : std::nullopt;
        const auto normal = m.is_object() ? vecFromJson(m, "normal") : std::nullopt;
        if (!origin || !normal || normal->length() < 1e-9)
            return bad("mirror plane");
        sourceBody = *body;
        mirror = true;
        planeOrigin = *origin;
        planeNormal = *normal;
        motion = {};
        return okStatus();
    }
    const auto translation = vecFromJson(in, "translation");
    if (!translation)
        return bad("translation");
    geom::RigidMotion m;
    m.translation = *translation;
    if (in.contains("rotation")) {
        const json& r = in["rotation"];
        const auto center = r.is_object() ? vecFromJson(r, "center") : std::nullopt;
        const auto axis = r.is_object() ? vecFromJson(r, "axis") : std::nullopt;
        const auto angle = r.is_object() ? numberFrom(r, "angle") : std::nullopt;
        if (!center || !axis || axis->length() < 1e-9 || !angle)
            return bad("rotation");
        m.center = *center;
        m.axis = *axis;
        m.angle = *angle;
    }
    sourceBody = *body;
    mirror = false;
    motion = m;
    return okStatus();
}

// ---- Imported ---------------------------------------------------------------------

std::string ImportedFeature::entryName() const
{
    return "imports/" + id().toString() + ".brep";
}

const std::string& ImportedFeature::brepText() const
{
    if (!brep_)
        brep_ = std::make_shared<const std::string>(geom::toBrepString(shape, false));
    return *brep_;
}

std::string ImportedFeature::hashOf(const std::string& text)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i, hash >>= 4)
        out[std::size_t(i)] = digits[hash & 0xF];
    return out;
}

void ImportedFeature::setShape(const geom::Shape& imported)
{
    shape = imported;
    volume = geom::volume(imported);
    brep_.reset();
}

void ImportedFeature::setLoadedShape(const geom::Shape& loaded, const std::string& brep)
{
    shape = loaded;
    brep_ = std::make_shared<const std::string>(brep);
}

Result<geom::Shape> ImportedFeature::compute(const geom::Shape&, const EvalContext&) const
{
    if (shape.isNull())
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The imported geometry of this body is missing.",
                                            "Imported: no shape");
    return Result<geom::Shape>::success(shape);
}

Status ImportedFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void ImportedFeature::writeParams(json& out) const
{
    out["geometry"] = entryName();
    out["hash"] = hashOf(brepText());
    out["source"] = source;
    out["volume"] = volume;
}

Status ImportedFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid imported body.",
                               std::string("Imported: ") + why);
    };
    if (!in.contains("geometry") || !in["geometry"].is_string())
        return bad("geometry");
    const std::string entry = in["geometry"].get<std::string>();
    if (entry.rfind("imports/", 0) != 0 || entry.size() <= 8 || entry.size() > 512)
        return bad("geometry entry name");
    const auto v = numberFrom(in, "volume");
    if (!v || !(*v > 0) || !std::isfinite(*v))
        return bad("volume");
    if (!in.contains("hash") || !in["hash"].is_string())
        return bad("hash");
    const std::string hash = in["hash"].get<std::string>();
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
        return bad("hash format");
    std::string from;
    if (in.contains("source")) {
        if (!in["source"].is_string() || in["source"].get<std::string>().size() > 1024)
            return bad("source");
        from = in["source"].get<std::string>();
    }
    loadedEntry_ = entry;
    loadedHash_ = hash;
    volume = *v;
    source = std::move(from);
    shape = {};
    brep_.reset();
    return okStatus();
}

// ---- Shell ----------------------------------------------------------------------

Result<geom::Shape> ShellFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    std::vector<int> indices;
    for (const FaceRef& ref : faces) {
        const auto index = geom::resolveFace(input, ref.signature, ref.indexHint);
        if (!index)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                                "A face this shell opens no longer exists.", "Shell face unresolved");
        indices.push_back(*index);
    }
    return geom::shell(input, indices, thickness, sizeAdvice(context));
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
    if (regions.value().empty())
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                            "The sketch has no closed shape to extrude. Close its outline first.",
                                            "Extrude: sketch has no regions");
    std::vector<geom::Shape> faces;
    for (const ProfileRef& ref : profiles) {
        const auto index = resolveProfile(regions.value(), plane, ref);
        if (!index)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                                "A shape this extrusion used is no longer closed or no longer exists.",
                                                "Extrude: profile reference unresolved");
        faces.push_back(regions.value()[std::size_t(*index)].face);
    }
    double length = symmetric ? std::abs(distance) : distance;
    if (std::abs(draftAngle) > 1e-12) {
        if (throughAll && mode == ExtrudeMode::Cut)
            return Result<geom::Shape>::failure(ErrorCode::InvalidArgument,
                                                "A draft needs a distance: turn off Through all, or set the draft to 0.",
                                                "Extrude: draft with through all");
        if (!symmetric)
            return geom::extrudeFacesDrafted(faces, plane.normal() * length, draftAngle);
        // Both ways from the sketch, each half narrowing away from it.
        auto up = geom::extrudeFacesDrafted(faces, plane.normal() * (length / 2), draftAngle);
        if (!up)
            return up;
        auto down = geom::extrudeFacesDrafted(faces, plane.normal() * (-length / 2), draftAngle);
        if (!down)
            return down;
        return geom::booleanOp(up.value(), down.value(), geom::BooleanKind::Union);
    }
    if (throughAll && mode == ExtrudeMode::Cut && !input.isNull()) {
        const auto box = geom::approximateBoundingBox(input);
        if (box.valid) {
            // Far enough to leave the body from anywhere on the sketch plane
            // (both ways for a symmetric cut).
            const double reach = box.size().length() + (box.center() - plane.origin).length() + 1.0;
            length = (length < 0 ? -1.0 : 1.0) * std::max(std::abs(length), symmetric ? 2 * reach : reach);
        }
    }
    if (!symmetric)
        return geom::extrudeFaces(faces, plane.normal() * length);
    // Extrude the full thickness, then center it on the sketch plane.
    auto solid = geom::extrudeFaces(faces, plane.normal() * length);
    if (!solid)
        return solid;
    return geom::translated(solid.value(), plane.normal() * (-length / 2));
}

Result<geom::Shape> ExtrudeFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    auto tool = toolSolid(input, context);
    if (!tool)
        return tool;
    switch (mode) {
    case ExtrudeMode::NewBody: return tool;
    case ExtrudeMode::Join: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Union);
    case ExtrudeMode::Cut:
        return reworded(geom::booleanOp(input, tool.value(), geom::BooleanKind::Subtract), ErrorCode::NoEffect,
                        "This cut does not reach the body, so nothing would be removed. Extrude toward the body, or further.");
    }
    return tool;
}

void ExtrudeFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    remap(sketchId, copies);
}

std::vector<ParameterInfo> ExtrudeFeature::parameters() const
{
    std::vector<ParameterInfo> out;
    if (symmetric)
        out.push_back({"distance", "Thickness", ParameterKind::Length, std::abs(distance)});
    else
        out.push_back({"distance", "Distance", ParameterKind::Length, distance});
    out.push_back({"draft", "Draft", ParameterKind::Angle, draftAngle});
    return out;
}

Status ExtrudeFeature::setParameter(std::string_view key, double value)
{
    if (key == "draft") {
        if (!std::isfinite(value) || std::abs(value) > 89.0 * kPi / 180.0 + 1e-12)
            return Status::failure(ErrorCode::InvalidArgument, "The draft must be between -89\xC2\xB0 and 89\xC2\xB0.",
                                   "draft out of range");
        draftAngle = value;
        return okStatus();
    }
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
    // A drafted extrusion keeps its distance inside "draft", so builds that
    // predate drafts refuse the file instead of extruding straight walls.
    if (draftAngle != 0)
        out["draft"] = {{"angle", draftAngle}, {"distance", distance}};
    else
        out["distance"] = distance;
    out["mode"] = std::string(toString(mode));
    out["throughAll"] = throughAll;
    if (symmetric)
        out["symmetric"] = true;
}

Status ExtrudeFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid extrusion.", why);
    };
    if (!in.contains("sketch") || !in["sketch"].is_string())
        return bad("Extrude: missing sketch");
    const auto id = Uuid::parse(in["sketch"].get<std::string>());
    auto d = numberFrom(in, "distance");
    double draft = 0;
    if (in.contains("draft")) {
        // { "angle", "distance" }: the distance is only there.
        const json& taper = in["draft"];
        const auto angle = taper.is_object() ? numberFrom(taper, "angle") : std::nullopt;
        if (d || !angle || !std::isfinite(*angle) || std::abs(*angle) > 89.0 * kPi / 180.0 + 1e-12)
            return bad("Extrude: bad draft");
        draft = *angle;
        d = numberFrom(taper, "distance");
    }
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
    symmetric = in.contains("symmetric") && in["symmetric"].is_boolean() && in["symmetric"].get<bool>();
    draftAngle = draft;
    return okStatus();
}

// ---- Hole -----------------------------------------------------------------------

std::string_view toString(HoleKind kind)
{
    switch (kind) {
    case HoleKind::Plain: return "Plain";
    case HoleKind::Counterbore: return "Counterbore";
    case HoleKind::Countersink: return "Countersink";
    }
    return "Plain";
}

std::optional<HolePlacement> holePlacement(const geom::Shape& shape, int edgeIndex)
{
    const auto edge = geom::edgeInfo(shape, edgeIndex);
    if (!edge || edge->kind != geom::CurveKind::Circle)
        return std::nullopt;
    // The drilling direction is into the flat face that owns this rim.
    for (int f : geom::facesOfEdge(shape, edgeIndex)) {
        const auto face = geom::faceInfo(shape, f);
        if (face && face->isPlanar() && std::abs(face->normal.dot(edge->axis)) > 0.999)
            return HolePlacement{edge->center, face->normal * -1.0, edge->radius};
    }
    return std::nullopt;
}

namespace {

std::string millimeters(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.2f mm", value);
    return text;
}

// Two unit vectors perpendicular to `axis` (and to each other).
std::pair<Vec3, Vec3> perpendiculars(const Vec3& axis)
{
    const Vec3 a = axis.normalized();
    const Vec3 other = std::abs(a.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 u = a.cross(other).normalized();
    return {u, a.cross(u).normalized()};
}

// How thick the part is around a hole, measured along the hole at a few
// points on a circle of `radius` around it (the thinnest); nullopt when no
// point there starts into material.
std::optional<double> thicknessAround(const geom::Shape& shape, const HolePlacement& at, double radius)
{
    const auto [u, v] = perpendiculars(at.direction);
    std::optional<double> thinnest;
    constexpr int kSamples = 8;
    for (int i = 0; i < kSamples; ++i) {
        const double a = 2 * kPi * i / kSamples;
        const Vec3 p = at.center + (u * std::cos(a) + v * std::sin(a)) * radius;
        if (const auto depth = geom::materialDepth(shape, p, at.direction); depth && (!thinnest || *depth < *thinnest))
            thinnest = depth;
    }
    return thinnest;
}

} // namespace

Result<geom::Shape> HoleFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    using R = Result<geom::Shape>;
    const auto index = geom::resolveEdge(input, rim.signature, rim.indexHint);
    const auto placement = index ? holePlacement(input, *index) : std::nullopt;
    if (!placement)
        return R::failure(ErrorCode::InvalidReference, "The hole edge this step uses no longer exists.", "Hole: rim unresolved");
    if (holeKind == HoleKind::Plain) {
        // Start slightly outside the surface so the cut opens cleanly.
        constexpr double kLead = 0.05;
        auto drill = geom::makeCylinder(placement->center - placement->direction * kLead, placement->direction,
                                        diameter / 2, depth + kLead);
        if (!drill)
            return drill;
        // Drilled from the rim of a hole at least as wide and deep, the drill
        // only meets air: the insert fits the hole as it is.
        const bool fitsAlready = diameter / 2 <= placement->rimRadius + 1e-6;
        return reworded(geom::booleanOp(input, drill.value(), geom::BooleanKind::Subtract), ErrorCode::NoEffect,
                        fitsAlready ? "This hole is already wide and deep enough for the insert, so nothing would change. "
                                      "Pick a smaller hole, or a larger insert."
                                    : "The hole does not reach into the body. Pick the rim of a hole on a flat face.");
    }

    // A screw head's seat on the existing hole.
    const bool counterbore = holeKind == HoleKind::Counterbore;
    const std::string what = counterbore ? "counterbore" : "countersink";
    geom::HoleCut cut;
    cut.entry = placement->center;
    cut.direction = placement->direction;
    cut.diameter = 2 * placement->rimRadius;
    cut.drillShaft = false;
    cut.head = counterbore ? geom::HoleHead::Counterbore : geom::HoleHead::Countersink;
    cut.headDiameter = diameter;
    cut.headDepth = depth;
    cut.headAngle = angle;
    // How deep the hole is: empty along its axis and across its opening
    // (nullopt: through the part). The head must end above its bottom, and a
    // countersink's cone stops short of it.
    const auto [u, v] = perpendiculars(placement->direction);
    std::vector<Vec3> across{placement->center};
    for (int i = 0; i < 4; ++i)
        across.push_back(placement->center
                         + (u * std::cos(kPi / 2 * i) + v * std::sin(kPi / 2 * i)) * (0.75 * placement->rimRadius));
    const auto holeDepth = geom::emptyDepth(input, across, placement->direction);
    if (holeDepth && *holeDepth < 1e-6)
        return R::failure(ErrorCode::InvalidArgument,
                          "A " + what + " goes around a hole: select the rim of a round hole.",
                          "Hole: no empty hole inside the rim");
    cut.throughAll = !holeDepth;
    cut.depth = holeDepth.value_or(0);
    if (!(diameter > cut.diameter + 1e-6))
        return R::failure(ErrorCode::InvalidArgument,
                          "The " + what + " must be wider than the hole (" + millimeters(cut.diameter) + ").",
                          "Hole: head diameter " + std::to_string(diameter) + " <= hole " + std::to_string(cut.diameter));
    // It must not reach through the part around the hole.
    const double reach = geom::headReach(cut);
    if (const auto thickness = thicknessAround(input, *placement, (placement->rimRadius + diameter / 2) / 2);
        thickness && reach >= *thickness - 1e-6)
        return R::failure(ErrorCode::InvalidArgument,
                          "The " + what + " would reach through the part: it is " + millimeters(*thickness) + " thick here.",
                          "Hole: head reach " + std::to_string(reach) + " >= thickness " + std::to_string(*thickness));
    if (holeDepth && reach >= *holeDepth - 1e-6)
        return R::failure(ErrorCode::InvalidArgument, "The " + what + " is deeper than the hole.",
                          "Hole: head reach " + std::to_string(reach) + " >= hole depth " + std::to_string(*holeDepth));
    auto result = geom::drillHoles(input, {cut});
    if (!result)
        return result;
    // The head takes away exactly its ring (or cone) around the hole; more
    // means the hole was narrower or shallower than the head (e.g. a step
    // inside it).
    const double removed = geom::volume(input) - geom::volume(result.value());
    const double expected = geom::headVolume(cut);
    if (removed > expected * (1 + 1e-6) + 1e-6)
        return R::failure(ErrorCode::InvalidArgument, "The " + what + " is deeper than the hole.",
                          "Hole: removed " + std::to_string(removed) + " > expected " + std::to_string(expected));
    return result;
}

std::vector<ParameterInfo> HoleFeature::parameters() const
{
    if (holeKind == HoleKind::Countersink)
        return {{"diameter", "Diameter", ParameterKind::Length, diameter}, {"angle", "Angle", ParameterKind::Angle, angle}};
    return {{"diameter", "Diameter", ParameterKind::Length, diameter}, {"depth", "Depth", ParameterKind::Length, depth}};
}

Status HoleFeature::setParameter(std::string_view key, double value)
{
    if (key == "angle" && holeKind == HoleKind::Countersink) {
        if (!(value > 1e-3) || !(value < kPi - 1e-3))
            return Status::failure(ErrorCode::InvalidArgument, "The angle must be between 0\xC2\xB0 and 180\xC2\xB0.",
                                   "countersink angle out of range");
        angle = value;
        return okStatus();
    }
    double* target = key == "diameter" ? &diameter : key == "depth" && holeKind != HoleKind::Countersink ? &depth : nullptr;
    if (!target)
        return unknownParameter(key);
    if (auto s = requirePositive(value, key == "diameter" ? "Diameter" : "Depth"); !s)
        return s;
    *target = value;
    return okStatus();
}

void HoleFeature::writeParams(json& out) const
{
    out["rim"] = edgeRefToJson(rim);
    out["diameter"] = diameter;
    out["preset"] = preset;
    if (holeKind != HoleKind::Plain)
        out["type"] = std::string(toString(holeKind));
    // A countersink has no depth (its angle sets it). Leaving it out also
    // makes builds that predate countersinks refuse the file instead of
    // drilling a plain hole of the countersink's diameter.
    if (holeKind == HoleKind::Countersink)
        out["angle"] = angle;
    else
        out["depth"] = depth;
}

Status HoleFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid hole.", std::string("Hole: ") + why);
    };
    const auto ref = in.contains("rim") ? edgeRefFromJson(in["rim"]) : std::nullopt;
    const auto d = numberFrom(in, "diameter");
    if (!ref || !d || !(*d > 0))
        return bad("rim/diameter");
    HoleKind kind = HoleKind::Plain;
    if (in.contains("type")) {
        const std::string type = in["type"].is_string() ? in["type"].get<std::string>() : std::string();
        if (type == "Counterbore")
            kind = HoleKind::Counterbore;
        else if (type == "Countersink")
            kind = HoleKind::Countersink;
        else if (type != "Plain")
            return bad("unknown type");
    }
    if (kind == HoleKind::Countersink) {
        const auto a = numberFrom(in, "angle");
        if (!a || !(*a > 1e-3) || !(*a < kPi - 1e-3))
            return bad("angle");
        angle = *a;
    } else {
        const auto h = numberFrom(in, "depth");
        if (!h || !(*h > 0))
            return bad("depth");
        depth = *h;
    }
    rim = *ref;
    holeKind = kind;
    diameter = *d;
    preset = in.contains("preset") && in["preset"].is_string() ? in["preset"].get<std::string>() : std::string();
    return okStatus();
}

// ---- Holes on a face (the Hole tool) ----------------------------------------------

std::optional<HoleFrame> holeFrame(const geom::Shape& shape, int faceIndex)
{
    const auto info = geom::faceInfo(shape, faceIndex);
    if (!info || !info->isPlanar())
        return std::nullopt;
    const Vec3 n = info->normal.normalized();
    const sketch::Plane plane = sketch::Plane::fromNormal(n * n.dot(info->centroid), n);
    return HoleFrame{plane.origin, plane.xAxis, plane.yAxis, n};
}

Result<geom::Shape> HolesFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    using R = Result<geom::Shape>;
    if (positions.empty())
        return R::failure(ErrorCode::InvalidArgument, "Place at least one hole.", "Holes: no positions");
    const auto index = geom::resolveFace(input, face.signature, face.indexHint);
    const auto frame = index ? holeFrame(input, *index) : std::nullopt;
    if (!frame)
        return R::failure(ErrorCode::InvalidReference, "The face these holes were drilled into no longer exists.",
                          "Holes: face unresolved");
    std::vector<geom::HoleCut> cuts;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        geom::HoleCut cut;
        cut.entry = frame->toWorld(positions[i]);
        cut.direction = frame->normal * -1.0;
        cut.diameter = diameter;
        cut.depth = depth;
        cut.throughAll = throughAll;
        cut.head = head == HoleKind::Counterbore   ? geom::HoleHead::Counterbore
                 : head == HoleKind::Countersink ? geom::HoleHead::Countersink
                                                 : geom::HoleHead::None;
        cut.headDiameter = headDiameter;
        cut.headDepth = headDepth;
        cut.headAngle = headAngle;
        const std::string which = positions.size() == 1 ? "The hole" : "Hole " + std::to_string(i + 1);
        if (!geom::faceContains(input, *index, cut.entry))
            return R::failure(ErrorCode::InvalidReference, which + " no longer lies on its face.",
                              "Holes: position " + std::to_string(i) + " off the face");
        // A screw head's seat must not reach through the part (as on a rim).
        if (cut.head != geom::HoleHead::None && headDiameter > diameter) {
            const HolePlacement at{cut.entry, cut.direction, diameter / 2};
            const double reach = geom::headReach(cut);
            if (const auto thickness = thicknessAround(input, at, (diameter + headDiameter) / 4);
                thickness && reach >= *thickness - 1e-6)
                return R::failure(ErrorCode::InvalidArgument,
                                  std::string(head == HoleKind::Counterbore ? "The counterbore" : "The countersink")
                                      + " would reach through the part: it is " + millimeters(*thickness) + " thick here.",
                                  "Holes: head reach " + std::to_string(reach) + " >= thickness " + std::to_string(*thickness));
        }
        cuts.push_back(cut);
    }
    return geom::drillHoles(input, cuts);
}

std::vector<ParameterInfo> HolesFeature::parameters() const
{
    std::vector<ParameterInfo> out{{"diameter", "Diameter", ParameterKind::Length, diameter}};
    if (!throughAll)
        out.push_back({"depth", "Depth", ParameterKind::Length, depth});
    if (head == HoleKind::Counterbore) {
        out.push_back({"headDiameter", "Counterbore \xC3\x98", ParameterKind::Length, headDiameter});
        out.push_back({"headDepth", "Counterbore depth", ParameterKind::Length, headDepth});
    } else if (head == HoleKind::Countersink) {
        out.push_back({"headDiameter", "Countersink \xC3\x98", ParameterKind::Length, headDiameter});
        out.push_back({"headAngle", "Countersink angle", ParameterKind::Angle, headAngle});
    }
    return out;
}

Status HolesFeature::setParameter(std::string_view key, double value)
{
    if (key == "headAngle" && head == HoleKind::Countersink) {
        if (!(value > 1e-3) || !(value < kPi - 1e-3))
            return Status::failure(ErrorCode::InvalidArgument, "The angle must be between 0\xC2\xB0 and 180\xC2\xB0.",
                                   "countersink angle out of range");
        headAngle = value;
        return okStatus();
    }
    double* target = key == "diameter"                                  ? &diameter
                   : key == "depth" && !throughAll                      ? &depth
                   : key == "headDiameter" && head != HoleKind::Plain    ? &headDiameter
                   : key == "headDepth" && head == HoleKind::Counterbore ? &headDepth
                                                                         : nullptr;
    if (!target)
        return unknownParameter(key);
    if (auto s = requirePositive(value, key == "diameter" ? "Diameter" : key == "depth" ? "Depth" : "The head size"); !s)
        return s;
    *target = value;
    return okStatus();
}

void HolesFeature::writeParams(json& out) const
{
    out["face"] = faceRefToJson(face);
    json list = json::array();
    for (const Vec2& p : positions)
        list.push_back(json::array({p.x, p.y}));
    out["positions"] = list;
    out["diameter"] = diameter;
    out["throughAll"] = throughAll;
    if (!throughAll)
        out["depth"] = depth;
    if (head != HoleKind::Plain) {
        out["head"] = std::string(toString(head));
        out["headDiameter"] = headDiameter;
        if (head == HoleKind::Counterbore)
            out["headDepth"] = headDepth;
        else
            out["headAngle"] = headAngle;
    }
    out["preset"] = preset;
}

Status HolesFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains invalid holes.", std::string("Holes: ") + why);
    };
    const auto ref = faceRefFromJson(in, "face");
    if (!ref)
        return bad("face");
    if (!in.contains("positions") || !in["positions"].is_array() || in["positions"].empty() || in["positions"].size() > 1000)
        return bad("positions");
    std::vector<Vec2> list;
    for (const auto& p : in["positions"]) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number())
            return bad("position");
        const Vec2 v{p[0].get<double>(), p[1].get<double>()};
        if (!std::isfinite(v.x) || !std::isfinite(v.y))
            return bad("position");
        list.push_back(v);
    }
    const auto d = numberFrom(in, "diameter");
    if (!d || !(*d > 0) || !std::isfinite(*d))
        return bad("diameter");
    const bool through = in.contains("throughAll") && in["throughAll"].is_boolean() && in["throughAll"].get<bool>();
    double h = depth;
    if (!through) {
        const auto value = numberFrom(in, "depth");
        if (!value || !(*value > 0))
            return bad("depth");
        h = *value;
    }
    HoleKind kind = HoleKind::Plain;
    double hd = headDiameter, hh = headDepth, ha = headAngle;
    if (in.contains("head")) {
        const std::string name = in["head"].is_string() ? in["head"].get<std::string>() : std::string();
        if (name == "Counterbore")
            kind = HoleKind::Counterbore;
        else if (name == "Countersink")
            kind = HoleKind::Countersink;
        else
            return bad("head");
        const auto value = numberFrom(in, "headDiameter");
        if (!value || !(*value > 0))
            return bad("headDiameter");
        hd = *value;
        if (kind == HoleKind::Counterbore) {
            const auto depthValue = numberFrom(in, "headDepth");
            if (!depthValue || !(*depthValue > 0))
                return bad("headDepth");
            hh = *depthValue;
        } else {
            const auto angleValue = numberFrom(in, "headAngle");
            if (!angleValue || !(*angleValue > 1e-3) || !(*angleValue < kPi - 1e-3))
                return bad("headAngle");
            ha = *angleValue;
        }
    }
    face = *ref;
    positions = std::move(list);
    diameter = *d;
    throughAll = through;
    depth = h;
    head = kind;
    headDiameter = hd;
    headDepth = hh;
    headAngle = ha;
    preset = in.contains("preset") && in["preset"].is_string() ? in["preset"].get<std::string>() : std::string();
    return okStatus();
}

// ---- Revolve --------------------------------------------------------------------

Result<geom::Shape> RevolveFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    const sketch::Sketch* sk = context.sketch(sketchId);
    if (!sk)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The sketch for this revolve no longer exists.",
                                            "Revolve: sketch missing");
    const sketch::Plane plane = effectivePlane(*sk, context);
    auto regions = sketchRegions(*sk, plane);
    if (!regions)
        return Result<geom::Shape>::failureFrom(regions);
    if (regions.value().empty())
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                            "The sketch has no closed shape to revolve. Close its outline first.",
                                            "Revolve: sketch has no regions");
    std::vector<geom::Shape> faces;
    for (const ProfileRef& ref : profiles) {
        const auto index = resolveProfile(regions.value(), plane, ref);
        if (!index)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference,
                                                "A shape this revolve used is no longer closed or no longer exists.",
                                                "Revolve: profile unresolved");
        faces.push_back(regions.value()[std::size_t(*index)].face);
    }
    auto tool = geom::revolveFaces(faces, plane.origin, axis == SketchAxis::Y ? plane.yAxis : plane.xAxis, angle);
    if (!tool)
        return tool;
    switch (mode) {
    case ExtrudeMode::NewBody: return tool;
    case ExtrudeMode::Join: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Union);
    case ExtrudeMode::Cut:
        return reworded(geom::booleanOp(input, tool.value(), geom::BooleanKind::Subtract), ErrorCode::NoEffect,
                        "This cut does not reach the body, so nothing would be removed.");
    }
    return tool;
}

void RevolveFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    remap(sketchId, copies);
}

std::vector<ParameterInfo> RevolveFeature::parameters() const
{
    return {{"angle", "Angle", ParameterKind::Angle, angle}};
}

Status RevolveFeature::setParameter(std::string_view key, double value)
{
    if (key != "angle")
        return unknownParameter(key);
    if (!(value > 1e-9) || value > 2 * kPi + 1e-9)
        return Status::failure(ErrorCode::InvalidArgument, "The angle must be between 0\xC2\xB0 and 360\xC2\xB0.",
                               "revolve angle out of range");
    angle = std::min(value, 2 * kPi);
    return okStatus();
}

void RevolveFeature::writeParams(json& out) const
{
    json refs = json::array();
    for (const auto& r : profiles)
        refs.push_back({{"point", json::array({r.interiorPoint.x, r.interiorPoint.y})}, {"area", r.area}});
    out["sketch"] = sketchId.toString();
    out["profiles"] = refs;
    out["axis"] = axis == SketchAxis::Y ? "Y" : "X";
    out["angle"] = angle;
    out["mode"] = std::string(toString(mode));
}

Status RevolveFeature::readParams(const json& in)
{
    // Same profile/mode encoding as Extrude; reuse its validation.
    ExtrudeFeature probe;
    json asExtrude = in;
    asExtrude["distance"] = 1.0;
    if (Status s = probe.readParams(asExtrude); !s)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid revolve.", s.developerMessage());
    const auto a = numberFrom(in, "angle");
    const std::string ax = in.contains("axis") && in["axis"].is_string() ? in["axis"].get<std::string>() : std::string();
    if (!a || !(*a > 0) || *a > 2 * kPi + 1e-9 || (ax != "X" && ax != "Y"))
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid revolve.", "Revolve: bad angle/axis");
    sketchId = probe.sketchId;
    profiles = probe.profiles;
    mode = probe.mode;
    angle = *a;
    axis = ax == "Y" ? SketchAxis::Y : SketchAxis::X;
    return okStatus();
}

// ---- Loft -------------------------------------------------------------------------

Result<geom::Shape> LoftFeature::toolSolid(const EvalContext& context) const
{
    using R = Result<geom::Shape>;
    if (sections.size() < 2)
        return R::failure(ErrorCode::InvalidArgument, "Select two or more closed profiles to loft.",
                          "Loft: " + std::to_string(sections.size()) + " sections");
    if (sections.size() > kMaxLoftSections)
        return R::failure(ErrorCode::InvalidArgument,
                          "A loft joins at most " + std::to_string(kMaxLoftSections) + " profiles.", "Loft: too many sections");
    // Each sketch's regions once (a loft may come back to a sketch).
    struct Found {
        Uuid sketch;
        sketch::Plane plane;
        std::vector<geom::Region> regions;
    };
    std::vector<Found> found;
    std::vector<geom::Shape> faces;
    for (const LoftSection& section : sections) {
        const sketch::Sketch* sk = context.sketch(section.sketchId);
        if (!sk)
            return R::failure(ErrorCode::InvalidReference, "A sketch this loft used no longer exists.",
                              "Loft: sketch " + section.sketchId.toString() + " not found");
        auto it = std::find_if(found.begin(), found.end(), [&](const Found& f) { return f.sketch == section.sketchId; });
        if (it == found.end()) {
            const sketch::Plane plane = effectivePlane(*sk, context);
            auto regions = sketchRegions(*sk, plane);
            if (!regions)
                return R::failureFrom(regions);
            it = found.insert(found.end(), Found{section.sketchId, plane, std::move(regions.value())});
        }
        const auto index = resolveProfile(it->regions, it->plane, section.profile);
        if (!index)
            return R::failure(ErrorCode::InvalidReference, "A profile this loft used is no longer closed or no longer exists.",
                              "Loft: profile unresolved in sketch " + section.sketchId.toString());
        faces.push_back(it->regions[std::size_t(*index)].face);
    }
    return geom::loftFaces(faces, ruled);
}

Result<geom::Shape> LoftFeature::compute(const geom::Shape& input, const EvalContext& context) const
{
    auto tool = toolSolid(context);
    if (!tool)
        return tool;
    switch (mode) {
    case ExtrudeMode::NewBody: return tool;
    case ExtrudeMode::Join: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Union);
    case ExtrudeMode::Cut:
        return reworded(geom::booleanOp(input, tool.value(), geom::BooleanKind::Subtract), ErrorCode::NoEffect,
                        "This loft does not reach the body, so nothing would be cut away.");
    }
    return tool;
}

Status LoftFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

std::vector<TextParameterInfo> LoftFeature::textParameters() const
{
    std::vector<TextParameterInfo> out{{"sections", "Sections", ruled ? "Straight" : "Smooth", {"Smooth", "Straight"}}};
    // A new body stays one (the step starts the body); a join can become a cut.
    if (mode != ExtrudeMode::NewBody)
        out.push_back({"mode", "Result", mode == ExtrudeMode::Join ? "Join" : "Cut", {"Join", "Cut"}});
    return out;
}

Status LoftFeature::setTextParameter(std::string_view key, const std::string& value)
{
    if (key == "sections" && (value == "Smooth" || value == "Straight")) {
        ruled = value == "Straight";
        return okStatus();
    }
    if (key == "mode" && mode != ExtrudeMode::NewBody && (value == "Join" || value == "Cut")) {
        mode = value == "Join" ? ExtrudeMode::Join : ExtrudeMode::Cut;
        return okStatus();
    }
    if (key == "sections" || key == "mode")
        return Status::failure(ErrorCode::InvalidArgument, key == "sections" ? "Choose Smooth or Straight." : "Choose Join or Cut.",
                               "Loft: bad " + std::string(key) + " '" + value + "'");
    return Feature::setTextParameter(key, value);
}

std::vector<Uuid> LoftFeature::dependencies() const
{
    std::vector<Uuid> out;
    for (const LoftSection& section : sections)
        if (std::find(out.begin(), out.end(), section.sketchId) == out.end())
            out.push_back(section.sketchId);
    return out;
}

void LoftFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    for (LoftSection& section : sections)
        remap(section.sketchId, copies);
}

void LoftFeature::writeParams(json& out) const
{
    json list = json::array();
    for (const LoftSection& s : sections)
        list.push_back({{"sketch", s.sketchId.toString()},
                        {"point", json::array({s.profile.interiorPoint.x, s.profile.interiorPoint.y})},
                        {"area", s.profile.area}});
    out["sections"] = list;
    out["ruled"] = ruled;
    out["mode"] = std::string(toString(mode));
}

Status LoftFeature::readParams(const json& in)
{
    auto bad = [](const std::string& why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid loft.", "Loft: " + why);
    };
    if (!in.contains("sections") || !in["sections"].is_array())
        return bad("missing sections");
    const json& list = in["sections"];
    if (list.size() < 2 || list.size() > kMaxLoftSections)
        return bad(std::to_string(list.size()) + " sections");
    std::vector<LoftSection> read;
    for (const json& s : list) {
        if (!s.is_object() || !s.contains("sketch") || !s["sketch"].is_string() || !s.contains("point") || !s["point"].is_array()
            || s["point"].size() != 2 || !s["point"][0].is_number() || !s["point"][1].is_number())
            return bad("bad section");
        const auto id = Uuid::parse(s["sketch"].get<std::string>());
        const auto area = numberFrom(s, "area");
        const double x = s["point"][0].get<double>(), y = s["point"][1].get<double>();
        if (!id || id->isNil() || !area || !std::isfinite(*area) || !std::isfinite(x) || !std::isfinite(y))
            return bad("bad section fields");
        read.push_back({*id, {{x, y}, *area}});
    }
    bool straight = false;
    if (in.contains("ruled")) {
        if (!in["ruled"].is_boolean())
            return bad("ruled is not a boolean");
        straight = in["ruled"].get<bool>();
    }
    const std::string m = in.contains("mode") && in["mode"].is_string() ? in["mode"].get<std::string>() : std::string();
    ExtrudeMode readMode = ExtrudeMode::NewBody;
    if (m == "Join")
        readMode = ExtrudeMode::Join;
    else if (m == "Cut")
        readMode = ExtrudeMode::Cut;
    else if (m != "NewBody")
        return bad("unknown mode '" + m + "'");
    sections = std::move(read);
    ruled = straight;
    mode = readMode;
    return okStatus();
}

// ---- Text (emboss / deboss) -------------------------------------------------------

namespace {
// Stored text is kept to a sane size (the geometry takes at most
// geom::kMaxTextLength characters; a longer one fails the step with a message).
constexpr std::size_t kMaxStoredTextBytes = 4096;

Status checkDepth(double value)
{
    if (!std::isfinite(value) || std::abs(value) < 1e-3)
        return Status::failure(ErrorCode::InvalidArgument,
                               "The depth must not be zero: positive raises the text, negative cuts it in.", "text depth 0");
    if (std::abs(value) > 10000)
        return Status::failure(ErrorCode::InvalidArgument, "The depth is too large.", "text depth " + std::to_string(value));
    return okStatus();
}
} // namespace

Result<geom::Shape> TextFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    using R = Result<geom::Shape>;
    const auto index = geom::resolveFace(input, face.signature, face.indexHint);
    const auto frame = index ? holeFrame(input, *index) : std::nullopt;
    if (!frame)
        return R::failure(ErrorCode::InvalidReference, "The face this text was placed on no longer exists.",
                          "Text: face unresolved");
    const Vec3 center = frame->toWorld(position);
    if (!geom::faceContains(input, *index, center))
        return R::failure(ErrorCode::InvalidReference, "The text no longer lies on its face.", "Text: center off the face");
    const Vec3 along = frame->xAxis * std::cos(angle) + frame->yAxis * std::sin(angle);
    return geom::embossText(input, geom::TextSpec{text, font, size}, geom::TextFrame{center, along, frame->normal}, depth);
}

std::vector<ParameterInfo> TextFeature::parameters() const
{
    return {{"size", "Size", ParameterKind::Length, size},
            {"depth", "Depth", ParameterKind::Length, depth},
            {"angle", "Angle", ParameterKind::Angle, angle}};
}

Status TextFeature::setParameter(std::string_view key, double value)
{
    if (key == "size") {
        if (!(value >= geom::kMinCapHeight) || !(value <= geom::kMaxCapHeight))
            return Status::failure(ErrorCode::InvalidArgument,
                                   "The size (the height of capital letters) must be between 0.5 and 1000 mm.",
                                   "text size " + std::to_string(value));
        size = value;
        return okStatus();
    }
    if (key == "depth") {
        if (Status s = checkDepth(value); !s)
            return s;
        depth = value;
        return okStatus();
    }
    if (key == "angle") {
        if (!std::isfinite(value) || std::abs(value) > 1000)
            return Status::failure(ErrorCode::InvalidArgument, "Type an angle in degrees.", "text angle");
        angle = normalizedAngle(value);
        return okStatus();
    }
    return unknownParameter(key);
}

double TextFeature::normalizedAngle(double radians)
{
    if (!std::isfinite(radians))
        return 0;
    if (radians >= 0 && radians < 2 * kPi)
        return radians; // exactly as it is (a file reads back what it wrote)
    double a = std::remainder(radians, 2 * kPi); // [-pi, pi]
    if (a < 0)
        a += 2 * kPi;
    if (std::abs(a) < 1e-12 || std::abs(a - 2 * kPi) < 1e-12)
        a = 0;
    return a;
}

std::vector<TextParameterInfo> TextFeature::textParameters() const
{
    return {{"text", "Text", text}};
}

Status TextFeature::setTextParameter(std::string_view key, const std::string& value)
{
    if (key != "text")
        return Feature::setTextParameter(key, value);
    if (value.size() > kMaxStoredTextBytes)
        return Status::failure(ErrorCode::InvalidArgument, "The text is too long.", "text too long");
    // Refuse what could never be made (nothing typed, a character the font
    // does not have), rather than keep a step that can only fail.
    if (std::string why = geom::checkText(geom::TextSpec{value, font, size}); !why.empty())
        return Status::failure(ErrorCode::InvalidArgument, why, "text: " + why);
    text = value;
    return okStatus();
}

void TextFeature::restoreTextParameter(std::string_view key, const std::string& value)
{
    if (key == "text")
        text = value;
}

void TextFeature::writeParams(json& out) const
{
    out["face"] = faceRefToJson(face);
    out["position"] = json::array({position.x, position.y});
    out["text"] = text;
    out["size"] = size;
    out["depth"] = depth;
    out["angle"] = angle;
    out["font"] = font;
}

Status TextFeature::readParams(const json& in)
{
    auto bad = [](const char* why) {
        return Status::failure(ErrorCode::FileFormatError, "The file contains invalid text.", std::string("Text: ") + why);
    };
    const auto ref = faceRefFromJson(in, "face");
    if (!ref)
        return bad("face");
    if (!in.contains("position") || !in["position"].is_array() || in["position"].size() != 2 || !in["position"][0].is_number()
        || !in["position"][1].is_number())
        return bad("position");
    const Vec2 p{in["position"][0].get<double>(), in["position"][1].get<double>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y))
        return bad("position");
    if (!in.contains("text") || !in["text"].is_string())
        return bad("text");
    std::string t = in["text"].get<std::string>();
    if (t.empty() || t.size() > kMaxStoredTextBytes)
        return bad("text length");
    if (!in.contains("font") || !in["font"].is_string())
        return bad("font");
    std::string f = in["font"].get<std::string>();
    if (f.empty() || f.size() > 128)
        return bad("font name");
    const auto s = numberFrom(in, "size");
    if (!s || !(*s >= geom::kMinCapHeight) || !(*s <= geom::kMaxCapHeight))
        return bad("size");
    const auto d = numberFrom(in, "depth");
    if (!d || !checkDepth(*d))
        return bad("depth");
    // Any finite angle is a direction (files written before the tool kept
    // its angle in range may hold one of many turns).
    const auto a = numberFrom(in, "angle");
    if (!a || !std::isfinite(*a))
        return bad("angle");
    face = *ref;
    position = p;
    text = std::move(t);
    font = std::move(f);
    size = *s;
    depth = *d;
    angle = normalizedAngle(*a);
    return okStatus();
}

} // namespace os::doc
