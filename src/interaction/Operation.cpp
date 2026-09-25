#include "interaction/Operation.h"

#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"

#include <algorithm>

namespace os::interact {

namespace {
std::uint64_t nextPreviewKey()
{
    // Preview mesh keys live in the upper half of the key space so they never
    // collide with body shape revisions.
    static std::uint64_t counter = 1ull << 62;
    return ++counter;
}
} // namespace

void Operation::setValue(double value, const doc::Document& document)
{
    if (!allowsNegative() && value < 0)
        value = 0;
    value_ = value;
    error_.clear();
    if (value == 0.0 && handleCount() == 1 && zeroIsIdentity()) {
        previewMesh_.reset();
        return;
    }
    resetAutomaticChoices();
    auto feature = makeFeature(value);
    auto result = document.preview(previewBody(), *feature);
    if (result && reconsider(result.value(), document)) {
        feature = makeFeature(value);
        result = document.preview(previewBody(), *feature);
    }
    if (!result) {
        previewMesh_.reset();
        error_ = result.userMessage();
        OS_LOG(Debug, Interaction) << title() << " preview failed at " << value << ": " << result.developerMessage();
        return;
    }
    previewMesh_ = std::make_shared<const geom::Mesh>(geom::tessellate(result.value()));
    previewKey_ = nextPreviewKey();
}

std::unique_ptr<cmd::Command> Operation::makeCommand(const doc::Document&) const
{
    return std::make_unique<cmd::AddFeatureCommand>(bodyId_, makeFeature(value_));
}

// ---- Push/pull -----------------------------------------------------------------

std::unique_ptr<PushPullOperation> PushPullOperation::create(const doc::Document& document, const Uuid& bodyId, int faceIndex)
{
    const doc::Body* body = document.body(bodyId);
    if (!body)
        return nullptr;
    const auto info = geom::faceInfo(body->shape(), faceIndex);
    const auto signature = geom::captureFaceSignature(body->shape(), faceIndex);
    if (!info || !signature || !info->isPlanar())
        return nullptr;
    return std::unique_ptr<PushPullOperation>(
        new PushPullOperation(bodyId, LinearManipulator(info->centroid, info->normal), doc::FaceRef{faceIndex, *signature}));
}

std::unique_ptr<doc::Feature> PushPullOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::PushPullFeature>();
    feature->face = face_;
    feature->distance = value;
    return feature;
}

// ---- Align -----------------------------------------------------------------------

std::unique_ptr<AlignOperation> AlignOperation::create(const doc::Document& document, const Uuid& bodyId,
                                                       geom::SubShapeKind kind, int index)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return nullptr;
    const auto frame = geom::alignFrame(body->shape(), kind, index);
    if (!frame)
        return nullptr;
    return std::unique_ptr<AlignOperation>(new AlignOperation(bodyId, *frame));
}

std::string AlignOperation::prompt() const
{
    return target_ ? std::string() : std::string("Click the face or edge to align to, on another body \xC2\xB7 Esc cancels");
}

Status AlignOperation::setTarget(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index)
{
    if (bodyId == this->bodyId())
        return Status::failure(ErrorCode::InvalidArgument, "Pick a face or edge on another body.",
                               "align: target on the moving body");
    const doc::Body* body = document.body(bodyId);
    const auto frame = body ? geom::alignFrame(body->shape(), kind, index) : std::nullopt;
    if (!frame)
        return Status::failure(ErrorCode::InvalidArgument, "Align to a flat or round face, a straight edge or a circle.",
                               "align: unsupported target");
    target_ = *frame;
    targetBody_ = bodyId;
    targetKind_ = kind;
    targetIndex_ = index;
    setValue(value(), document);
    return okStatus();
}

Status AlignOperation::setGroundTarget(const doc::Document& document)
{
    if (!source_.sided)
        return Status::failure(ErrorCode::InvalidArgument, "Only a flat face can be laid on the ground.",
                               "align: ground needs a flat source face");
    // Lay the face down where it is: straight below its centroid, facing down.
    target_ = geom::AlignFrame{{source_.point.x, source_.point.y, 0.0}, {0, 0, 1}, true};
    targetBody_ = Uuid();
    targetKind_ = geom::SubShapeKind::Whole;
    targetIndex_ = -1;
    setValue(value(), document);
    return okStatus();
}

void AlignOperation::clearTarget()
{
    target_.reset();
    targetBody_ = Uuid();
    targetKind_ = geom::SubShapeKind::Whole;
    targetIndex_ = -1;
    clearPreview();
}

void AlignOperation::setFlipped(bool flip, const doc::Document& document)
{
    flip_ = flip;
    if (target_)
        setValue(value(), document);
}

LinearManipulator AlignOperation::handle(int index) const
{
    if (index != 0 || !target_)
        return {};
    return LinearManipulator(target_->point, target_->direction);
}

std::unique_ptr<doc::Feature> AlignOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::MoveFeature>();
    feature->setName("Align");
    if (!target_)
        return feature;
    const geom::RigidMotion m = geom::alignMotion(source_, *target_, flip_, value);
    feature->translation = m.translation;
    if (std::abs(m.angle) > 1e-12) {
        feature->rotates = true;
        feature->rotationCenter = m.center;
        feature->rotationAxis = m.axis;
        feature->rotationAngle = m.angle;
    }
    return feature;
}

std::unique_ptr<cmd::Command> AlignOperation::makeCommand(const doc::Document&) const
{
    return std::make_unique<cmd::AddFeatureCommand>(bodyId(), makeFeature(value()));
}

// ---- Move ------------------------------------------------------------------------

namespace {
Vec3 axisVector(int axis)
{
    return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}
double& component(Vec3& v, int axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
double component(const Vec3& v, int axis)
{
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
} // namespace

std::unique_ptr<MoveOperation> MoveOperation::create(const doc::Document& document, const Uuid& bodyId)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return nullptr;
    const auto box = geom::approximateBoundingBox(body->shape());
    if (!box.valid)
        return nullptr;
    return std::unique_ptr<MoveOperation>(new MoveOperation(bodyId, box.center()));
}

std::string MoveOperation::valueLabel() const
{
    static const char* names[] = {"X", "Y", "Z"};
    return names[std::clamp(activeHandle(), 0, 2)];
}

Vec3 MoveOperation::translation() const
{
    Vec3 t = offset_;
    component(t, activeHandle()) = value();
    return t;
}

bool MoveOperation::canCommit() const
{
    return translation().length() > 1e-9 && error().empty() && hasPreview();
}

LinearManipulator MoveOperation::handle(int index) const
{
    // Every arrow starts at the moved center; handle i slides along axis i.
    const Vec3 t = translation();
    const Vec3 axis = axisVector(index);
    return LinearManipulator(center_ + t - axis * component(t, index), axis);
}

double MoveOperation::handleOffset(int index) const
{
    return component(translation(), index);
}

void MoveOperation::setActiveHandle(int index)
{
    if (index == activeHandle())
        return;
    component(offset_, activeHandle()) = value();
    Operation::setActiveHandle(index);
    setStoredValue(component(offset_, index));
}

std::unique_ptr<doc::Feature> MoveOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::MoveFeature>();
    Vec3 t = offset_;
    component(t, activeHandle()) = value;
    feature->translation = t;
    return feature;
}

// ---- Offset face -------------------------------------------------------------------

std::unique_ptr<OffsetFaceOperation> OffsetFaceOperation::create(const doc::Document& document, const Uuid& bodyId,
                                                                 int faceIndex)
{
    const doc::Body* body = document.body(bodyId);
    if (!body)
        return nullptr;
    const auto info = geom::faceInfo(body->shape(), faceIndex);
    const auto signature = geom::captureFaceSignature(body->shape(), faceIndex);
    if (!info || !signature)
        return nullptr;
    if (info->kind == geom::SurfaceKind::Cylinder && info->radius > 0) {
        // The arrow sits on the wall and points away from the axis: dragging
        // out widens the circle. (A full cylinder's centroid is on its axis.)
        Vec3 radial = info->point - info->axisOrigin;
        radial = radial - info->axisDirection.normalized() * radial.dot(info->axisDirection.normalized());
        if (radial.length() < 1e-9)
            return nullptr;
        radial = radial.normalized();
        auto op = std::unique_ptr<OffsetFaceOperation>(
            new OffsetFaceOperation(bodyId, LinearManipulator(info->point, radial), doc::FaceRef{faceIndex, *signature}));
        op->round_ = true;
        op->diameter_ = 2 * info->radius;
        op->outward_ = info->normal.dot(radial) > 0 ? 1.0 : -1.0; // boss: normal points away from the axis
        op->setStoredValue(op->diameter_);
        return op;
    }
    return std::unique_ptr<OffsetFaceOperation>(new OffsetFaceOperation(
        bodyId, LinearManipulator(info->centroid, info->normal), doc::FaceRef{faceIndex, *signature}));
}

std::unique_ptr<doc::Feature> OffsetFaceOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::OffsetFaceFeature>();
    feature->face = face_;
    feature->distance = round_ ? outward_ * (value - diameter_) / 2 : value;
    return feature;
}

// ---- Mirror ------------------------------------------------------------------------

std::unique_ptr<MirrorOperation> MirrorOperation::create(const doc::Document& document, const Uuid& bodyId)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return nullptr;
    const auto box = geom::approximateBoundingBox(body->shape());
    if (!box.valid)
        return nullptr;
    return std::unique_ptr<MirrorOperation>(new MirrorOperation(bodyId, box.center()));
}

std::string MirrorOperation::prompt() const
{
    return plane_ ? std::string()
                  : std::string("Click a flat face to mirror across, or choose a plane \xC2\xB7 Esc cancels");
}

Status MirrorOperation::setPlaneFromFace(const doc::Document& document, const Uuid& bodyId, int faceIndex)
{
    const doc::Body* body = document.body(bodyId);
    const auto info = body ? geom::faceInfo(body->shape(), faceIndex) : std::nullopt;
    if (!info || !info->isPlanar())
        return Status::failure(ErrorCode::NotPlanar, "Mirror across a flat face, or choose a plane.",
                               "mirror: face is not planar");
    plane_ = Plane{info->centroid, info->normal.normalized()};
    originAxis_ = -1;
    setValue(value(), document);
    return okStatus();
}

void MirrorOperation::setOriginPlane(int normalAxis, const doc::Document& document)
{
    originAxis_ = std::clamp(normalAxis, 0, 2);
    plane_ = Plane{{0, 0, 0}, axisVector(originAxis_)};
    setValue(value(), document);
}

std::unique_ptr<doc::Feature> MirrorOperation::makeFeature(double) const
{
    auto feature = std::make_unique<doc::MirrorFeature>();
    if (plane_) {
        feature->planeOrigin = plane_->origin;
        feature->planeNormal = plane_->normal;
    }
    return feature;
}

// ---- Pattern -----------------------------------------------------------------------

std::unique_ptr<PatternOperation> PatternOperation::create(const doc::Document& document, const Uuid& bodyId)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return nullptr;
    const auto box = geom::approximateBoundingBox(body->shape());
    if (!box.valid)
        return nullptr;
    auto op = std::unique_ptr<PatternOperation>(new PatternOperation(bodyId, box.center(), box.size()));
    op->setValue(op->defaultSpacing(), document); // preview right away: copies side by side
    return op;
}

std::string PatternOperation::valueLabel() const
{
    return std::string(circular_ ? "Angle" : "Spacing") + " \xC3\x97" + std::to_string(count_);
}

Vec3 PatternOperation::axisVectorFor() const
{
    return axisIndex_ >= 0 ? axisVector(axisIndex_) : customAxis_.normalized();
}

double PatternOperation::defaultSpacing() const
{
    // The body's extent along the direction plus a 5 mm gap, rounded up.
    const Vec3 d = axisVectorFor();
    const double extent = size_.x * std::abs(d.x) + size_.y * std::abs(d.y) + size_.z * std::abs(d.z);
    return std::ceil(extent + 5.0 - 1e-3); // the fast box carries a tolerance: 20.0000002 is 20
}

void PatternOperation::setCircular(bool circular, const doc::Document& document)
{
    if (circular == circular_)
        return;
    circular_ = circular;
    count_ = circular ? 6 : 3;
    // Rows default to X, turns to Z (on the table); a picked edge/axis only
    // makes sense for the layout it was picked for.
    axisIndex_ = circular ? 2 : 0;
    setActiveHandle(0);
    setValue(circular ? 360.0 : defaultSpacing(), document);
}

void PatternOperation::setAxisIndex(int axis, const doc::Document& document)
{
    axisIndex_ = std::clamp(axis, 0, 2);
    setValue(circular_ ? value() : defaultSpacing(), document);
}

void PatternOperation::setCount(int count, const doc::Document& document)
{
    count_ = std::clamp(count, 2, 500);
    setValue(value(), document);
}

Status PatternOperation::setAxisFrom(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index)
{
    const doc::Body* body = document.body(bodyId);
    const auto frame = body ? geom::alignFrame(body->shape(), kind, index) : std::nullopt;
    if (!frame || frame->sided) // flat faces give no direction to repeat along
        return Status::failure(ErrorCode::InvalidArgument,
                               circular_ ? "Pick a hole, shaft, circle or straight edge to turn around."
                                         : "Pick a straight edge to repeat along.",
                               "pattern: unusable axis pick");
    if (!circular_) {
        const auto edge = kind == geom::SubShapeKind::Edge ? geom::edgeInfo(body->shape(), index) : std::nullopt;
        if (!edge || edge->kind != geom::CurveKind::Line)
            return Status::failure(ErrorCode::InvalidArgument, "Pick a straight edge to repeat along.",
                                   "pattern: linear needs a straight edge");
    }
    customOrigin_ = frame->point;
    customAxis_ = frame->direction;
    axisIndex_ = -1;
    setValue(circular_ ? value() : defaultSpacing(), document);
    return okStatus();
}

LinearManipulator PatternOperation::handle(int index) const
{
    if (index != 0 || circular_)
        return {};
    return LinearManipulator(center_, axisVectorFor());
}

std::unique_ptr<doc::Feature> PatternOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::PatternFeature>();
    feature->count = count_;
    if (circular_) {
        feature->layout = doc::PatternFeature::Layout::Circular;
        feature->axisOrigin = axisIndex_ >= 0 ? center_ : customOrigin_;
        feature->axis = axisVectorFor();
        feature->angle = std::clamp(value, -360.0, 360.0) * kPi / 180.0;
    } else {
        feature->layout = doc::PatternFeature::Layout::Linear;
        feature->direction = axisVectorFor();
        feature->spacing = value;
    }
    return feature;
}

// ---- Rotate ------------------------------------------------------------------------

std::unique_ptr<RotateOperation> RotateOperation::create(const doc::Document& document, const Uuid& bodyId)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return nullptr;
    const auto box = geom::approximateBoundingBox(body->shape());
    if (!box.valid)
        return nullptr;
    auto op = std::unique_ptr<RotateOperation>(new RotateOperation(bodyId, box.center()));
    op->Operation::setActiveHandle(2); // Z: turning on the table is the common case
    return op;
}

std::string RotateOperation::valueLabel() const
{
    static const char* names[] = {"Angle X", "Angle Y", "Angle Z"};
    return names[std::clamp(activeHandle(), 0, 2)];
}

RingManipulator RotateOperation::ring(int index) const
{
    return RingManipulator(center_, axisVector(std::clamp(index, 0, 2)));
}

void RotateOperation::setActiveHandle(int index)
{
    if (index == activeHandle())
        return;
    // One axis per step: a different ring starts from zero.
    Operation::setActiveHandle(index);
    setStoredValue(0.0);
    clearPreview();
}

std::unique_ptr<doc::Feature> RotateOperation::makeFeature(double degrees) const
{
    auto feature = std::make_unique<doc::MoveFeature>();
    feature->setName("Rotate");
    feature->rotates = true;
    feature->rotationCenter = center_;
    feature->rotationAxis = axisVector(std::clamp(activeHandle(), 0, 2));
    feature->rotationAngle = degrees * kPi / 180.0;
    return feature;
}

// ---- Revolve -------------------------------------------------------------------------

std::unique_ptr<RevolveOperation> RevolveOperation::create(const doc::Document& document, const Uuid& sketchId,
                                                           std::vector<doc::ProfileRef> profiles, const Vec3& anchor,
                                                           doc::SketchAxis axis)
{
    const sketch::Sketch* sk = document.sketch(sketchId);
    if (!sk || profiles.empty())
        return nullptr;
    std::optional<Uuid> host;
    if (sk->hostBody() && document.body(*sk->hostBody()) && document.body(*sk->hostBody())->isVisible())
        host = sk->hostBody();
    const sketch::Plane& plane = sk->plane();
    const Vec3 axisDir = axis == doc::SketchAxis::Y ? plane.yAxis : plane.xAxis;
    const Vec3 fromAxis = anchor - plane.origin;
    const Vec3 radial = fromAxis - axisDir * fromAxis.dot(axisDir);
    const double radius = std::max(radial.length(), 1.0);
    Vec3 tangent = axisDir.cross(radial).normalized();
    if (tangent.length() < 0.5)
        tangent = plane.normal();
    return std::unique_ptr<RevolveOperation>(new RevolveOperation(sketchId, host, LinearManipulator(anchor, tangent),
                                                                  std::move(profiles), axis, radius, plane.origin, axisDir,
                                                                  anchor));
}

LinearManipulator RevolveOperation::handle(int index) const
{
    if (index != 0)
        return {};
    // Rodrigues rotation of the start point around the axis by the current angle.
    const double angle = value() * kPi / 180.0;
    const Vec3 k = axisDirection_.normalized();
    const Vec3 v = start_ - axisOrigin_;
    const Vec3 rotated = v * std::cos(angle) + k.cross(v) * std::sin(angle) + k * (k.dot(v) * (1 - std::cos(angle)));
    const Vec3 onArc = axisOrigin_ + rotated;
    Vec3 tangent = k.cross(rotated - k * k.dot(rotated)).normalized();
    if (tangent.length() < 0.5)
        tangent = manipulator().direction();
    // anchor(handleOffset) must land on the arc point.
    return LinearManipulator(onArc - tangent * displayOffset(value()), tangent);
}

Uuid RevolveOperation::previewBody() const
{
    return mode() == doc::ExtrudeMode::NewBody ? Uuid() : host_.value_or(Uuid());
}

namespace {
// A join whose result has more separate pieces than the body had did not
// touch it: the user meant a new body (as Shapr3D does).
bool joinMissedBody(const geom::Shape& result, const doc::Document& document, const std::optional<Uuid>& host)
{
    const doc::Body* body = host ? document.body(*host) : nullptr;
    return body && result.solidCount() > std::max(body->shape().solidCount(), 1);
}
} // namespace

bool RevolveOperation::reconsider(const geom::Shape& result, const doc::Document& document)
{
    if (modeChosen_ || mode_ != doc::ExtrudeMode::Join || !joinMissedBody(result, document, host_))
        return false;
    autoNewBody_ = true;
    return true;
}

std::unique_ptr<doc::Feature> RevolveOperation::makeFeature(double degrees) const
{
    auto feature = std::make_unique<doc::RevolveFeature>();
    feature->sketchId = sketchId_;
    feature->profiles = profiles_;
    feature->axis = axis_;
    feature->angle = std::clamp(degrees, 0.0, 360.0) * kPi / 180.0;
    feature->mode = host_ ? mode() : doc::ExtrudeMode::NewBody;
    return feature;
}

std::unique_ptr<cmd::Command> RevolveOperation::makeCommand(const doc::Document& document) const
{
    if (!host_ || mode() == doc::ExtrudeMode::NewBody)
        return std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), makeFeature(value()));
    return std::make_unique<cmd::AddFeatureCommand>(*host_, makeFeature(value()));
}

// ---- Heat-set insert --------------------------------------------------------------

std::unique_ptr<InsertOperation> InsertOperation::create(const doc::Document& document, const Uuid& bodyId, int rimEdge,
                                                         std::size_t presetIndex)
{
    const doc::Body* body = document.body(bodyId);
    if (!body)
        return nullptr;
    const auto placement = doc::holePlacement(body->shape(), rimEdge);
    const auto signature = geom::captureEdgeSignature(body->shape(), rimEdge);
    if (!placement || !signature)
        return nullptr;
    presetIndex = std::min(presetIndex, doc::heatSetInsertPresets().size() - 1);
    auto op = std::unique_ptr<InsertOperation>(new InsertOperation(
        bodyId, LinearManipulator(placement->center, placement->direction), doc::EdgeRef{rimEdge, *signature}, presetIndex));
    op->setPreset(presetIndex, document);
    return op;
}

InsertOperation::Preset InsertOperation::preset() const
{
    const auto& p = doc::heatSetInsertPresets()[presetIndex_];
    return {p.name, p.diameter, p.depth};
}

void InsertOperation::setPreset(std::size_t index, const doc::Document& document)
{
    presetIndex_ = std::min(index, doc::heatSetInsertPresets().size() - 1);
    diameter_ = preset().diameter;
    setValue(preset().depth, document); // preview right away
}

std::unique_ptr<doc::Feature> InsertOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::HoleFeature>();
    feature->rim = rim_;
    feature->diameter = diameter_;
    feature->depth = value;
    feature->preset = preset().name + " heat-set insert";
    return feature;
}

// ---- Shell -----------------------------------------------------------------------

std::unique_ptr<ShellOperation> ShellOperation::create(const doc::Document& document, const Uuid& bodyId,
                                                       const std::vector<int>& faceIndices)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || faceIndices.empty())
        return nullptr;
    std::vector<doc::FaceRef> refs;
    for (int f : faceIndices) {
        const auto signature = geom::captureFaceSignature(body->shape(), f);
        if (!signature)
            return nullptr;
        refs.push_back({f, *signature});
    }
    const auto first = geom::faceInfo(body->shape(), faceIndices.front());
    if (!first)
        return nullptr;
    return std::unique_ptr<ShellOperation>(
        new ShellOperation(bodyId, LinearManipulator(first->centroid, first->normal * -1.0), std::move(refs)));
}

std::unique_ptr<doc::Feature> ShellOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::ShellFeature>();
    feature->faces = faces_;
    feature->thickness = value;
    return feature;
}

// ---- Fillet / chamfer ------------------------------------------------------------

std::unique_ptr<EdgeOperation> EdgeOperation::create(const doc::Document& document, const Uuid& bodyId,
                                                     const std::vector<int>& edgeIndices, doc::FeatureKind kind)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || edgeIndices.empty())
        return nullptr;
    std::vector<doc::EdgeRef> refs;
    for (int e : edgeIndices) {
        const auto signature = geom::captureEdgeSignature(body->shape(), e);
        if (!signature)
            return nullptr;
        refs.push_back({e, *signature});
    }
    const auto first = geom::edgeInfo(body->shape(), edgeIndices.front());
    if (!first)
        return nullptr;
    // Direction into the material: opposite the sum of the adjacent face normals.
    Vec3 outward;
    for (int f : geom::facesOfEdge(body->shape(), edgeIndices.front()))
        if (const auto info = geom::faceInfo(body->shape(), f))
            outward += info->normal;
    Vec3 direction = (outward * -1.0).normalized();
    if (direction.length() < 0.5) {
        // Degenerate (e.g. seam edge): fall back to any direction perpendicular to the edge.
        direction = first->tangent.cross(Vec3{0, 0, 1}).normalized();
        if (direction.length() < 0.5)
            direction = first->tangent.cross(Vec3{1, 0, 0}).normalized();
    }
    return std::unique_ptr<EdgeOperation>(
        new EdgeOperation(bodyId, LinearManipulator(first->midpoint, direction), std::move(refs), kind));
}

std::unique_ptr<doc::Feature> EdgeOperation::makeFeature(double value) const
{
    std::unique_ptr<doc::EdgeTreatmentFeature> feature;
    if (kind_ == doc::FeatureKind::Fillet)
        feature = std::make_unique<doc::FilletFeature>();
    else
        feature = std::make_unique<doc::ChamferFeature>();
    feature->edges = edges_;
    feature->size = value;
    return feature;
}

// ---- Extrude --------------------------------------------------------------------

std::unique_ptr<ExtrudeOperation> ExtrudeOperation::create(const doc::Document& document, const Uuid& sketchId,
                                                           std::vector<doc::ProfileRef> profiles, const Vec3& anchor)
{
    const sketch::Sketch* sk = document.sketch(sketchId);
    if (!sk || profiles.empty())
        return nullptr;
    std::optional<Uuid> host;
    if (sk->hostBody() && document.body(*sk->hostBody()) && document.body(*sk->hostBody())->isVisible())
        host = sk->hostBody();
    return std::unique_ptr<ExtrudeOperation>(
        new ExtrudeOperation(sketchId, host, LinearManipulator(anchor, sk->plane().normal()), std::move(profiles)));
}

doc::ExtrudeMode ExtrudeOperation::mode() const
{
    if (modeOverride_)
        return host_ || *modeOverride_ == doc::ExtrudeMode::NewBody ? *modeOverride_ : doc::ExtrudeMode::NewBody;
    if (!host_ || autoNewBody_)
        return doc::ExtrudeMode::NewBody;
    return value() < 0 ? doc::ExtrudeMode::Cut : doc::ExtrudeMode::Join;
}

bool ExtrudeOperation::reconsider(const geom::Shape& result, const doc::Document& document)
{
    if (modeOverride_ || mode() != doc::ExtrudeMode::Join || !joinMissedBody(result, document, host_))
        return false;
    autoNewBody_ = true;
    return true;
}

Uuid ExtrudeOperation::previewBody() const
{
    return mode() == doc::ExtrudeMode::NewBody ? Uuid() : host_.value_or(Uuid());
}

std::unique_ptr<doc::Feature> ExtrudeOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::ExtrudeFeature>();
    feature->sketchId = sketchId_;
    feature->profiles = profiles_;
    feature->distance = value;
    feature->mode = mode();
    feature->throughAll = throughAll_ && feature->mode == doc::ExtrudeMode::Cut;
    return feature;
}

std::unique_ptr<cmd::Command> ExtrudeOperation::makeCommand(const doc::Document& document) const
{
    if (mode() == doc::ExtrudeMode::NewBody) {
        return std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), makeFeature(value()));
    }
    return std::make_unique<cmd::AddFeatureCommand>(*host_, makeFeature(value()));
}

} // namespace os::interact
