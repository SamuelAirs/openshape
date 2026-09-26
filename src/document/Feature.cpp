// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/Feature.h"

#include "core/Log.h"
#include "document/Document.h"
#include "document/JsonHelpers.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
    }
    return "Unknown";
}

std::optional<FeatureKind> featureKindFromString(std::string_view text)
{
    for (FeatureKind k : {FeatureKind::Box, FeatureKind::PushPull, FeatureKind::Fillet, FeatureKind::Chamfer,
                          FeatureKind::Extrude, FeatureKind::Shell, FeatureKind::Move, FeatureKind::Combine,
                          FeatureKind::Revolve, FeatureKind::Hole, FeatureKind::Mirror, FeatureKind::Pattern,
                          FeatureKind::DeleteFaces, FeatureKind::OffsetFace, FeatureKind::Split, FeatureKind::SplitPiece})
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
    return geom::booleanOp(input, tool->shape(), kind);
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
    return geom::mirrorJoined(input, planeOrigin, planeNormal);
}

Status MirrorFeature::setParameter(std::string_view key, double)
{
    return unknownParameter(key);
}

void MirrorFeature::writeParams(json& out) const
{
    out["origin"] = vecToJson(planeOrigin);
    out["normal"] = vecToJson(planeNormal);
}

Status MirrorFeature::readParams(const json& in)
{
    const auto origin = vecFromJson(in, "origin");
    const auto normal = vecFromJson(in, "normal");
    if (!origin || !normal || normal->length() < 1e-9)
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid mirror.", "Mirror: bad plane");
    planeOrigin = *origin;
    planeNormal = *normal;
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
    double length = symmetric ? std::abs(distance) : distance;
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
    case ExtrudeMode::Cut: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Subtract);
    }
    return tool;
}

void ExtrudeFeature::remapReferences(const std::map<Uuid, Uuid>& copies)
{
    remap(sketchId, copies);
}

std::vector<ParameterInfo> ExtrudeFeature::parameters() const
{
    if (symmetric)
        return {{"distance", "Thickness", ParameterKind::Length, std::abs(distance)}};
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
    symmetric = in.contains("symmetric") && in["symmetric"].is_boolean() && in["symmetric"].get<bool>();
    return okStatus();
}

// ---- Hole -----------------------------------------------------------------------

const std::vector<InsertPreset>& heatSetInsertPresets()
{
    static const std::vector<InsertPreset> presets{
        {"M2", 3.2, 4.0}, {"M2.5", 3.6, 5.0}, {"M3", 4.0, 6.0}, {"M4", 5.6, 8.5}, {"M5", 6.4, 10.0}};
    return presets;
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

Result<geom::Shape> HoleFeature::compute(const geom::Shape& input, const EvalContext&) const
{
    const auto index = geom::resolveEdge(input, rim.signature, rim.indexHint);
    const auto placement = index ? holePlacement(input, *index) : std::nullopt;
    if (!placement)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The hole edge this step uses no longer exists.",
                                            "Hole: rim unresolved");
    // Start slightly outside the surface so the cut opens cleanly.
    constexpr double kLead = 0.05;
    auto drill = geom::makeCylinder(placement->center - placement->direction * kLead, placement->direction, diameter / 2,
                                    depth + kLead);
    if (!drill)
        return drill;
    return geom::booleanOp(input, drill.value(), geom::BooleanKind::Subtract);
}

std::vector<ParameterInfo> HoleFeature::parameters() const
{
    return {{"diameter", "Diameter", ParameterKind::Length, diameter}, {"depth", "Depth", ParameterKind::Length, depth}};
}

Status HoleFeature::setParameter(std::string_view key, double value)
{
    double* target = key == "diameter" ? &diameter : key == "depth" ? &depth : nullptr;
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
    out["depth"] = depth;
    out["preset"] = preset;
}

Status HoleFeature::readParams(const json& in)
{
    const auto ref = in.contains("rim") ? edgeRefFromJson(in["rim"]) : std::nullopt;
    const auto d = numberFrom(in, "diameter");
    const auto h = numberFrom(in, "depth");
    if (!ref || !d || !h || !(*d > 0) || !(*h > 0))
        return Status::failure(ErrorCode::FileFormatError, "The file contains an invalid hole.", "Hole: bad params");
    rim = *ref;
    diameter = *d;
    depth = *h;
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
    case ExtrudeMode::Cut: return geom::booleanOp(input, tool.value(), geom::BooleanKind::Subtract);
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

} // namespace os::doc
