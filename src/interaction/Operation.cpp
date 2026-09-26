// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/Operation.h"

#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>

namespace os::interact {

namespace {
std::uint64_t nextPreviewKey()
{
    // Preview mesh keys live in the upper half of the key space so they never
    // collide with body shape revisions.
    static std::uint64_t counter = 1ull << 62;
    return ++counter;
}

// Preview requests, numbered across all operations (0 = none).
std::atomic<std::uint64_t> g_previewSerial{0};
std::uint64_t nextPreviewSerial()
{
    return ++g_previewSerial;
}

std::atomic<std::uint64_t> g_operationInstances{0};
thread_local PreviewScheduler* t_creationScheduler = nullptr;
} // namespace

Operation::Operation(Uuid bodyId, LinearManipulator manipulator)
    : bodyId_(bodyId), manipulator_(std::move(manipulator)), instance_(++g_operationInstances),
      scheduler_(t_creationScheduler), floorSerial_(g_previewSerial.load() + 1)
{
}

PreviewSchedulerScope::PreviewSchedulerScope(PreviewScheduler* scheduler) : previous_(t_creationScheduler)
{
    t_creationScheduler = scheduler;
}

PreviewSchedulerScope::~PreviewSchedulerScope()
{
    t_creationScheduler = previous_;
}

void Operation::setValue(double value, const doc::Document& document, Change change)
{
    if (!allowsNegative() && value < 0)
        value = 0;
    value_ = value;
    if (std::string why = checkValue(value); !why.empty()) {
        dropPreviews();
        error_ = std::move(why);
        return;
    }
    if (std::abs(value - neutralValue()) < 1e-12 && handleCount() == 1 && neutralIsIdentity()) {
        dropPreviews();
        error_.clear();
        return;
    }
    if (scheduler_) {
        if (std::shared_ptr<Operation> copy = clone()) {
            // The worker gets everything it reads: this operation as it is now
            // and the document as it is now. The shown preview stays until
            // the result arrives (acceptPreview).
            copy->scheduler_ = nullptr;
            std::shared_ptr<const doc::Document> snapshot = scheduler_->previewSnapshot(document);
            const std::uint64_t serial = nextPreviewSerial();
            pendingSerial_ = serial;
            // Earlier requests computed other parameters: none of them may
            // show any more (the shown preview stays until this one arrives).
            if (change == Change::Parameters)
                floorSerial_ = serial;
            scheduler_->schedulePreview([copy, snapshot, value, serial]() {
                PreviewOutcome outcome = copy->computeOutcome(value, *snapshot);
                outcome.serial = serial;
                outcome.computedBy = copy;
                return outcome;
            });
            return;
        }
    }
    error_.clear();
    PreviewOutcome outcome = computeOutcome(value, document);
    // Anything still coming from the worker is older than this.
    outcome.serial = nextPreviewSerial();
    if (pendingSerial_ != 0 && scheduler_)
        scheduler_->dropScheduledPreview();
    pendingSerial_ = 0;
    floorSerial_ = outcome.serial;
    resolvedSerial_ = outcome.serial;
    if (!outcome.error.empty())
        OS_LOG(Debug, Interaction) << title() << " preview failed at " << value << ": " << outcome.developerMessage;
    error_ = outcome.error;
    showOutcome(outcome);
}

PreviewOutcome Operation::computeOutcome(double value, const doc::Document& document)
{
    const auto start = std::chrono::steady_clock::now();
    PreviewOutcome outcome;
    outcome.operation = instance_;
    outcome.value = value;
    try {
        resetAutomaticChoices();
        auto result = computePreview(value, document);
        if (result ? reconsider(result.value(), document) : reconsiderRefusal(result.error()))
            result = computePreview(value, document);
        outcome.meshBody = previewBody();
        if (!result) {
            outcome.error = result.userMessage();
            outcome.developerMessage = result.developerMessage();
        } else {
            // Isolated: the result shares faces and edges with the document's
            // body, which must not collect this preview's mesh.
            geom::TessellationParams params;
            params.isolated = true;
            outcome.mesh = std::make_shared<const geom::Mesh>(geom::tessellate(result.value(), params));
        }
    } catch (const std::exception& e) {
        // Kernel failures come back as Results; this would be a bug (or memory).
        outcome.mesh.reset();
        outcome.error = "Unable to preview this.";
        outcome.developerMessage = std::string("preview threw: ") + e.what();
        OS_LOG(Error, Interaction) << title() << " " << outcome.developerMessage;
    } catch (...) {
        // E.g. a kernel exception outside guarded(). Still an outcome: the
        // operation awaits one for every request it made.
        outcome.mesh.reset();
        outcome.error = "Unable to preview this.";
        outcome.developerMessage = "preview threw an unknown exception";
        OS_LOG(Error, Interaction) << title() << " " << outcome.developerMessage;
    }
    outcome.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return outcome;
}

void Operation::showOutcome(const PreviewOutcome& outcome)
{
    previewMesh_ = outcome.mesh;
    previewMeshBody_ = outcome.meshBody;
    if (previewMesh_)
        previewKey_ = nextPreviewKey();
}

bool Operation::acceptPreview(const PreviewOutcome& outcome)
{
    if (outcome.operation != instance_ || outcome.serial < floorSerial_ || outcome.serial <= resolvedSerial_)
        return false;
    const bool latest = outcome.serial == pendingSerial_;
    if (!latest && (!outcome.mesh || pendingSerial_ == 0))
        return false; // a value the user has left: its failure is no news
    resolvedSerial_ = outcome.serial;
    if (latest) {
        pendingSerial_ = 0;
        if (outcome.computedBy)
            adoptAutomaticChoices(*outcome.computedBy);
        error_ = outcome.error;
        if (!outcome.error.empty())
            OS_LOG(Debug, Interaction) << title() << " preview failed at " << outcome.value << ": "
                                       << outcome.developerMessage;
    } else {
        error_.clear(); // an older value that works, shown while the newest computes
    }
    showOutcome(outcome);
    return true;
}

void Operation::refusePendingValue(std::string message)
{
    // As a synchronous preview's refusal: the message, and no preview of
    // another value left on screen.
    dropPreviews();
    error_ = std::move(message);
}

void Operation::dropPreviews()
{
    previewMesh_.reset();
    floorSerial_ = nextPreviewSerial();
    if (pendingSerial_ != 0 && scheduler_)
        scheduler_->dropScheduledPreview();
    pendingSerial_ = 0;
}

Result<geom::Shape> Operation::computePreview(double value, const doc::Document& document) const
{
    const auto feature = makeFeature(value);
    return document.preview(previewBody(), *feature);
}

std::unique_ptr<cmd::Command> Operation::makeCommand(const doc::Document&) const
{
    return std::make_unique<cmd::AddFeatureCommand>(bodyId_, makeFeature(value_));
}

namespace {

// More separate bodies than this would bury the Model panel (and cost a
// recompute and a mesh each); a joined pattern has no such limit.
constexpr std::size_t kMaxSeparateCopies = 100;

// Separate bodies: the source and its copies side by side, not fused.
Result<geom::Shape> previewCopies(const doc::Document& document, const Uuid& source,
                                  const std::vector<std::unique_ptr<doc::CopyFeature>>& copies)
{
    if (copies.size() > kMaxSeparateCopies)
        return Result<geom::Shape>::failure(ErrorCode::InvalidArgument,
                                            "Separate bodies work for up to " + std::to_string(kMaxSeparateCopies) + " copies.",
                                            "separate copies: too many");
    const doc::Body* body = document.body(source);
    if (!body || body->shape().isNull())
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body no longer exists.", "previewCopies: body");
    std::vector<geom::Shape> shapes{body->shape()};
    for (const auto& copy : copies) {
        auto shape = document.preview(Uuid(), *copy);
        if (!shape)
            return shape;
        shapes.push_back(shape.value());
    }
    return geom::gatherSolids(shapes);
}

// One new body per copy ("Body 2", "Body 3", ...), as one undo step.
std::unique_ptr<cmd::Command> createCopyBodies(const doc::Document& document,
                                               std::vector<std::unique_ptr<doc::CopyFeature>> copies, const char* label)
{
    const std::vector<std::string> names = document.nextBodyNames(copies.size());
    std::vector<std::unique_ptr<cmd::Command>> steps;
    for (std::size_t i = 0; i < copies.size(); ++i)
        steps.push_back(std::make_unique<cmd::CreateBodyCommand>(names[i], std::move(copies[i])));
    return std::make_unique<cmd::CompositeCommand>(label, std::move(steps));
}

} // namespace

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
    // The arrow sits on the face (a washer's centroid is in its hole) and
    // measures the thickness from there.
    const Vec3 anchor = geom::pointOnFace(body->shape(), faceIndex, info->centroid).value_or(info->centroid);
    auto op = std::unique_ptr<PushPullOperation>(
        new PushPullOperation(bodyId, LinearManipulator(anchor, info->normal), doc::FaceRef{faceIndex, *signature}));
    op->thickness_ = geom::faceThickness(body->shape(), faceIndex, anchor);
    if (op->thickness_) {
        const Vec3 n = info->normal.normalized();
        op->thicknessLabel_ = std::abs(n.z) > 0.9999 ? "Height"
                            : std::abs(n.x) > 0.9999 ? "Width"
                            : std::abs(n.y) > 0.9999 ? "Depth"
                                                     : "Thickness";
        op->setStoredValue(op->thickness_->distance);
    }
    return op;
}

std::unique_ptr<doc::Feature> PushPullOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::PushPullFeature>();
    feature->face = face_;
    feature->distance = value - neutralValue();
    feature->keepEdges = true; // fillets and chamfers around the face come along
    return feature;
}

std::string PushPullOperation::checkValue(double value) const
{
    // Same wording as a typed value that is refused.
    return thickness_ && value <= 1e-6 ? thicknessLabel_ + " must be greater than zero." : std::string();
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
    return translation().length() > 1e-9 && previewUsable();
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

void MirrorOperation::setSeparate(bool separate, const doc::Document& document)
{
    separate_ = separate;
    if (plane_)
        setValue(value(), document);
}

std::vector<std::unique_ptr<doc::CopyFeature>> MirrorOperation::makeCopies() const
{
    std::vector<std::unique_ptr<doc::CopyFeature>> copies;
    if (!plane_)
        return copies;
    auto copy = std::make_unique<doc::CopyFeature>();
    copy->sourceBody = bodyId();
    copy->mirror = true;
    copy->planeOrigin = plane_->origin;
    copy->planeNormal = plane_->normal;
    copies.push_back(std::move(copy));
    return copies;
}

Result<geom::Shape> MirrorOperation::computePreview(double value, const doc::Document& document) const
{
    return separate_ ? previewCopies(document, bodyId(), makeCopies()) : Operation::computePreview(value, document);
}

std::unique_ptr<cmd::Command> MirrorOperation::makeCommand(const doc::Document& document) const
{
    return separate_ ? createCopyBodies(document, makeCopies(), "Mirror") : Operation::makeCommand(document);
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

void PatternOperation::setSeparate(bool separate, const doc::Document& document)
{
    separate_ = separate;
    setValue(value(), document);
}

std::vector<std::unique_ptr<doc::CopyFeature>> PatternOperation::makeCopies(double value) const
{
    std::vector<std::unique_ptr<doc::CopyFeature>> copies;
    const auto feature = makeFeature(value);
    for (const geom::RigidMotion& motion : static_cast<const doc::PatternFeature&>(*feature).copies()) {
        auto copy = std::make_unique<doc::CopyFeature>();
        copy->sourceBody = bodyId();
        copy->motion = motion;
        copies.push_back(std::move(copy));
    }
    return copies;
}

Result<geom::Shape> PatternOperation::computePreview(double value, const doc::Document& document) const
{
    return separate_ ? previewCopies(document, bodyId(), makeCopies(value)) : Operation::computePreview(value, document);
}

std::unique_ptr<cmd::Command> PatternOperation::makeCommand(const doc::Document& document) const
{
    return separate_ ? createCopyBodies(document, makeCopies(value()), "Pattern") : Operation::makeCommand(document);
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
    if (axis_)
        return "Angle";
    static const char* names[] = {"Angle X", "Angle Y", "Angle Z"};
    return names[std::clamp(activeHandle(), 0, 2)];
}

RingManipulator RotateOperation::ring(int index) const
{
    return RingManipulator(center_, axis_ ? *axis_ : axisVector(std::clamp(index, 0, 2)));
}

int RotateOperation::handleAxis(int index) const
{
    if (!axis_)
        return index;
    for (int k = 0; k < 3; ++k)
        if (std::abs(axis_->dot(axisVector(k))) > 1 - 1e-9)
            return k;
    return -1;
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

void RotateOperation::setAxis(const Vec3& point, const Vec3& direction, const doc::Document& document)
{
    Vec3 d = direction.normalized();
    // A predictable sense: positive angles turn counterclockwise looking down
    // the axis' main direction, as on the X/Y/Z rings.
    const double c[3] = {d.x, d.y, d.z};
    int main = 0;
    for (int k = 1; k < 3; ++k)
        if (std::abs(c[k]) > std::abs(c[main]))
            main = k;
    if (c[main] < 0)
        d = d * -1.0;
    axis_ = d;
    center_ = point;
    Operation::setActiveHandle(0); // the one ring; the angle is kept
    setValue(value(), document);
}

void RotateOperation::setPivot(const Vec3& point, const doc::Document& document)
{
    if (axis_) {
        axis_.reset();
        Operation::setActiveHandle(2); // Z, as when the tool starts; the angle is kept
    }
    center_ = point;
    setValue(value(), document);
}

void RotateOperation::resetPivot(const doc::Document& document)
{
    setPivot(bodyCenter_, document);
}

std::unique_ptr<doc::Feature> RotateOperation::makeFeature(double degrees) const
{
    auto feature = std::make_unique<doc::MoveFeature>();
    feature->setName("Rotate");
    feature->rotates = true;
    feature->rotationCenter = center_;
    feature->rotationAxis = axis_ ? *axis_ : axisVector(std::clamp(activeHandle(), 0, 2));
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

// ---- Counterbore / countersink ------------------------------------------------------

std::unique_ptr<HeadOperation> HeadOperation::create(const doc::Document& document, const Uuid& bodyId, int rimEdge,
                                                     doc::HoleKind kind, std::size_t presetIndex)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || kind == doc::HoleKind::Plain)
        return nullptr;
    const auto placement = doc::holePlacement(body->shape(), rimEdge);
    const auto signature = geom::captureEdgeSignature(body->shape(), rimEdge);
    if (!placement || !signature)
        return nullptr;
    // The diameter arrow lies in the face, along its horizontal direction
    // (world X on a floor or ceiling), like a sketch's X axis there.
    const Vec3 n = placement->direction * -1.0;
    Vec3 radial = Vec3{0, 0, 1}.cross(n);
    if (radial.length() < 1e-6)
        radial = n.z > 0 ? Vec3{1, 0, 0} : Vec3{-1, 0, 0};
    radial = radial.normalized();
    auto op = std::unique_ptr<HeadOperation>(new HeadOperation(bodyId, LinearManipulator(placement->center, radial),
                                                               doc::EdgeRef{rimEdge, *signature}, *placement, kind));
    op->radial_ = radial;
    op->setPreset(std::min(presetIndex, doc::metricScrews().size() - 1), document);
    return op;
}

std::string HeadOperation::title() const
{
    const std::string name = kind_ == doc::HoleKind::Counterbore ? "Counterbore" : "Countersink";
    const auto preset = presetIndex();
    return preset ? name + " " + doc::metricScrews()[*preset].name : name;
}

LinearManipulator HeadOperation::handle(int index) const
{
    if (index == 1)
        return LinearManipulator(placement_.center, placement_.direction);
    return LinearManipulator(placement_.center, radial_);
}

double HeadOperation::handleOffset(int index) const
{
    return index == 1 ? depth() : diameter() / 2;
}

void HeadOperation::setActiveHandle(int index)
{
    if (index == activeHandle() || index < 0 || index >= handleCount())
        return;
    // The value moves to its field; the other handle's field becomes the value.
    const double d = diameter(), h = depth();
    Operation::setActiveHandle(index);
    diameter_ = d;
    depth_ = h;
    setStoredValue(index == 1 ? h : d);
}

std::optional<std::size_t> HeadOperation::presetIndex() const
{
    const doc::ScrewSize& screw = doc::metricScrews()[presetIndex_];
    const bool counterbore = kind_ == doc::HoleKind::Counterbore;
    const double d = counterbore ? screw.counterboreDiameter : screw.countersinkDiameter;
    if (std::abs(diameter() - d) > 1e-9 || (counterbore && std::abs(depth() - screw.counterboreDepth) > 1e-9))
        return std::nullopt;
    return presetIndex_;
}

void HeadOperation::setPreset(std::size_t index, const doc::Document& document)
{
    presetIndex_ = std::min(index, doc::metricScrews().size() - 1);
    const doc::ScrewSize& screw = doc::metricScrews()[presetIndex_];
    diameter_ = kind_ == doc::HoleKind::Counterbore ? screw.counterboreDiameter : screw.countersinkDiameter;
    depth_ = screw.counterboreDepth;
    setValue(activeHandle() == 1 ? depth_ : diameter_, document); // preview right away
}

std::unique_ptr<doc::Feature> HeadOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::HoleFeature>();
    feature->rim = rim_;
    feature->holeKind = kind_;
    feature->diameter = activeHandle() == 1 ? diameter_ : value;
    if (kind_ == doc::HoleKind::Counterbore)
        feature->depth = activeHandle() == 1 ? value : depth_;
    feature->angle = doc::kCountersinkAngleDegrees * kPi / 180.0;
    // Named after the screw only while the sizes are the preset's.
    const doc::ScrewSize& screw = doc::metricScrews()[presetIndex_];
    const bool counterbore = kind_ == doc::HoleKind::Counterbore;
    const bool matches = std::abs(feature->diameter - (counterbore ? screw.counterboreDiameter : screw.countersinkDiameter)) < 1e-9
                      && (!counterbore || std::abs(feature->depth - screw.counterboreDepth) < 1e-9);
    feature->preset = matches ? screw.name : "";
    return feature;
}

// ---- Hole tool ------------------------------------------------------------------------

std::unique_ptr<HoleOperation> HoleOperation::create(const doc::Document& document, const Uuid& bodyId, int faceIndex,
                                                     const HoleSettings& settings)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return nullptr;
    const geom::Shape& shape = body->shape();
    const auto frame = doc::holeFrame(shape, faceIndex);
    const auto signature = geom::captureFaceSignature(shape, faceIndex);
    const auto info = geom::faceInfo(shape, faceIndex);
    if (!frame || !signature || !info)
        return nullptr;
    auto op = std::unique_ptr<HoleOperation>(new HoleOperation(bodyId, doc::FaceRef{faceIndex, *signature}, *frame));
    op->shape_ = shape;
    op->outline_ = geom::faceOutline(shape, faceIndex, frame->origin, frame->xAxis, frame->yAxis);
    if (!op->outline_.valid)
        return nullptr;
    op->facePoint_ = geom::pointOnFace(shape, faceIndex, info->centroid).value_or(info->centroid);
    op->settings_ = settings;
    op->settings_.screw = std::min(settings.screw, doc::metricScrews().size() - 1);
    op->depth_ = settings.depth;
    op->applyPreset();
    op->setStoredValue(op->diameter_);
    return op;
}

std::string HoleOperation::valueLabel() const
{
    const bool fromLast = fromLastHole_ && current_ > 0;
    switch (field_) {
    case Field::Diameter: return "Diameter";
    case Field::Depth: return "Depth";
    case Field::X: return fromLast ? "X from last hole" : "X from corner";
    case Field::Y: return fromLast ? "Y from last hole" : "Y from corner";
    }
    return "Diameter";
}

std::string HoleOperation::prompt() const
{
    return positions_.empty()
               ? std::string("Click or tap the face where a hole goes (it snaps to the center and the middles of the edges) "
                             "\xC2\xB7 Esc cancels")
               : std::string();
}

std::optional<Vec3> HoleOperation::labelAnchor() const
{
    if (current_ < 0)
        return facePoint_;
    return frame_.toWorld(livePositions(value())[std::size_t(current_)]);
}

Vec2 HoleOperation::reference() const
{
    if (fromLastHole_ && current_ > 0)
        return positions_[std::size_t(current_ - 1)];
    return {outline_.minU, outline_.minV};
}

std::vector<Vec2> HoleOperation::livePositions(double value) const
{
    std::vector<Vec2> out = positions_;
    if (current_ >= 0 && (field_ == Field::X || field_ == Field::Y)) {
        Vec2& p = out[std::size_t(current_)];
        (field_ == Field::X ? p.x : p.y) = (field_ == Field::X ? reference().x : reference().y) + value;
    }
    return out;
}

double HoleOperation::fieldValue(Field field) const
{
    switch (field) {
    case Field::Diameter: return diameter_;
    case Field::Depth: return depth_;
    case Field::X: return current_ >= 0 ? positions_[std::size_t(current_)].x - reference().x : 0.0;
    case Field::Y: return current_ >= 0 ? positions_[std::size_t(current_)].y - reference().y : 0.0;
    }
    return 0.0;
}

void HoleOperation::storeValue()
{
    switch (field_) {
    case Field::Diameter: diameter_ = value(); break;
    case Field::Depth: depth_ = settings_.depth = value(); break;
    case Field::X:
    case Field::Y: positions_ = livePositions(value()); break;
    }
}

double HoleOperation::presetDiameter() const
{
    return doc::holeDiameterFor(doc::metricScrews()[settings_.screw], settings_.fit);
}

void HoleOperation::applyPreset()
{
    diameter_ = presetDiameter();
}

bool HoleOperation::onFace(Vec2 p) const
{
    return geom::faceContains(shape_, face_.indexHint, frame_.toWorld(p));
}

bool HoleOperation::onOutline(Vec2 p) const
{
    const double size = std::max({outline_.maxU - outline_.minU, outline_.maxV - outline_.minV, 1.0});
    return geom::outlineContains(outline_, p, 1e-7 * size);
}

std::pair<Vec2, std::string> HoleOperation::snap(const Vec3& world, double snapDistance) const
{
    const Vec2 p = frame_.toLocal(world);
    const Vec2 center{(outline_.minU + outline_.maxU) / 2, (outline_.minV + outline_.maxV) / 2};
    // Onto a point: the center (unless it is off the face, e.g. in an
    // L-shaped face's notch or a ring's hole), or the middle of a straight edge.
    std::optional<Vec2> best;
    std::string what;
    double bestDistance = snapDistance;
    auto consider = [&](const Vec2& q, const char* name) {
        if (const double d = (q - p).length(); d <= bestDistance && onOutline(q)) {
            bestDistance = d;
            best = q;
            what = name;
        }
    };
    consider(center, "center");
    for (const Vec3& m : outline_.edgeMidpoints)
        consider(frame_.toLocal(m), "midpoint");
    if (best)
        return {*best, what};
    // Otherwise in line (X or Y) with those, with circles on the face (holes
    // already there) and with the holes placed so far.
    std::vector<Vec2> lines{center};
    for (const Vec3& m : outline_.edgeMidpoints)
        lines.push_back(frame_.toLocal(m));
    for (const Vec3& c : outline_.circleCenters)
        lines.push_back(frame_.toLocal(c));
    for (const Vec2& h : livePositions(value()))
        lines.push_back(h);
    double du = snapDistance, dv = snapDistance;
    std::optional<double> u, v;
    for (const Vec2& q : lines) {
        if (const double d = std::abs(q.x - p.x); d <= du) {
            du = d;
            u = q.x;
        }
        if (const double d = std::abs(q.y - p.y); d <= dv) {
            dv = d;
            v = q.y;
        }
    }
    // Lined up in both directions if that stays on the face, else in one.
    for (const auto& [x, y] : {std::pair{u, v}, std::pair{u, std::optional<double>()}, std::pair{std::optional<double>(), v}}) {
        if (!x && !y)
            continue;
        const Vec2 q{x.value_or(p.x), y.value_or(p.y)};
        if (onOutline(q))
            return {q, "aligned"};
    }
    return {p, ""};
}

std::string HoleOperation::placeAt(const Vec3& world, double snapDistance, const doc::Document& document)
{
    storeValue();
    // A click on a placed hole makes it the current one (its X / Y can be typed).
    const Vec2 raw = frame_.toLocal(world);
    const double reach = std::max(diameter_ / 2, snapDistance);
    for (std::size_t i = 0; i < positions_.size(); ++i)
        if ((positions_[i] - raw).length() <= reach) {
            current_ = static_cast<int>(i);
            setStoredValue(fieldValue(field_));
            setValue(value(), document);
            return "hole";
        }
    const auto [point, what] = snap(world, snapDistance);
    positions_.push_back(point);
    current_ = static_cast<int>(positions_.size()) - 1;
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
    return what;
}

void HoleOperation::setField(Field field, const doc::Document& document)
{
    if ((field == Field::X || field == Field::Y) && current_ < 0)
        return; // no hole to move yet
    if (field == Field::Depth && settings_.throughAll)
        return;
    storeValue();
    field_ = field;
    setStoredValue(fieldValue(field));
    setValue(value(), document);
}

void HoleOperation::nextField(const doc::Document& document)
{
    std::vector<Field> order{Field::Diameter};
    if (!settings_.throughAll)
        order.push_back(Field::Depth);
    if (current_ >= 0) {
        order.push_back(Field::X);
        order.push_back(Field::Y);
    }
    const auto it = std::find(order.begin(), order.end(), field_);
    const std::size_t next = it == order.end() ? 0 : (std::size_t(it - order.begin()) + 1) % order.size();
    setField(order[next], document);
}

void HoleOperation::setScrew(std::size_t index, const doc::Document& document)
{
    storeValue();
    settings_.screw = std::min(index, doc::metricScrews().size() - 1);
    applyPreset();
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
}

void HoleOperation::setFit(doc::HoleFit fit, const doc::Document& document)
{
    storeValue();
    settings_.fit = fit;
    applyPreset();
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
}

void HoleOperation::setThroughAll(bool throughAll, const doc::Document& document)
{
    storeValue();
    settings_.throughAll = throughAll;
    if (throughAll && field_ == Field::Depth)
        field_ = Field::Diameter;
    if (!throughAll)
        field_ = Field::Depth; // the next thing to type is the depth
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
}

void HoleOperation::setHead(doc::HoleKind head, const doc::Document& document)
{
    storeValue();
    settings_.head = settings_.head == head ? doc::HoleKind::Plain : head;
    setValue(value(), document);
}

void HoleOperation::setFromLastHole(bool on, const doc::Document& document)
{
    storeValue();
    fromLastHole_ = on;
    setStoredValue(fieldValue(field_)); // the same hole, measured from elsewhere
    setValue(value(), document);
}

void HoleOperation::removeCurrent(const doc::Document& document)
{
    if (current_ < 0)
        return;
    storeValue();
    positions_.erase(positions_.begin() + current_);
    current_ = positions_.empty() ? -1 : std::max(current_ - 1, 0);
    if (current_ < 0 && (field_ == Field::X || field_ == Field::Y))
        field_ = Field::Diameter; // no hole left to move
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
}

std::unique_ptr<doc::Feature> HoleOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::HolesFeature>();
    feature->face = face_;
    feature->positions = livePositions(value);
    feature->diameter = field_ == Field::Diameter ? value : diameter_;
    feature->depth = field_ == Field::Depth ? value : depth_;
    feature->throughAll = settings_.throughAll;
    const doc::ScrewSize& screw = doc::metricScrews()[settings_.screw];
    feature->head = settings_.head;
    feature->headDiameter = settings_.head == doc::HoleKind::Countersink ? screw.countersinkDiameter : screw.counterboreDiameter;
    feature->headDepth = screw.counterboreDepth;
    feature->headAngle = doc::kCountersinkAngleDegrees * kPi / 180.0;
    // Named after the screw only while the diameter is the preset's.
    if (std::abs(feature->diameter - presetDiameter()) < 1e-9)
        feature->preset = doc::holeFitLabel(screw, settings_.fit);
    return feature;
}

Result<geom::Shape> HoleOperation::computePreview(double value, const doc::Document& document) const
{
    if (positions_.empty()) {
        const doc::Body* body = document.body(bodyId());
        if (!body)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body no longer exists.", "hole: body");
        return Result<geom::Shape>::success(body->shape());
    }
    // A hole typed (or placed) off the face: say so here; the step's own
    // message ("no longer lies on its face") is for upstream changes.
    const std::vector<Vec2> live = livePositions(value);
    for (std::size_t i = 0; i < live.size(); ++i)
        if (!onFace(live[i]))
            return Result<geom::Shape>::failure(
                ErrorCode::InvalidArgument,
                (live.size() == 1 ? std::string("The hole") : "Hole " + std::to_string(i + 1))
                    + " is off the face: type an X / Y on it, or Remove hole.",
                "hole: position " + std::to_string(i) + " off the face");
    return Operation::computePreview(value, document);
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
    return distance() < 0 ? doc::ExtrudeMode::Cut : doc::ExtrudeMode::Join;
}

void ExtrudeOperation::setActiveHandle(int index)
{
    index = index == 1 ? 1 : 0;
    if (index == activeHandle())
        return;
    const double d = distance(), a = draftDegrees();
    Operation::setActiveHandle(index);
    distance_ = d;
    draftDegrees_ = a;
    setStoredValue(index == 1 ? a : d);
}

bool ExtrudeOperation::reconsider(const geom::Shape& result, const doc::Document& document)
{
    if (modeOverride_ || mode() != doc::ExtrudeMode::Join || !joinMissedBody(result, document, host_))
        return false;
    autoNewBody_ = true;
    return true;
}

bool ExtrudeOperation::reconsiderRefusal(ErrorCode code)
{
    // Pushed in beside the body, an automatic cut removes nothing: the user
    // meant a new body on that side (as a join that misses does).
    if (modeOverride_ || mode() != doc::ExtrudeMode::Cut || code != ErrorCode::NoEffect)
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
    feature->distance = editingDraft() ? distance_ : value;
    feature->mode = mode();
    feature->throughAll = throughAll_ && feature->mode == doc::ExtrudeMode::Cut;
    feature->symmetric = symmetric_;
    feature->draftAngle = (editingDraft() ? value : draftDegrees_) * kPi / 180.0;
    return feature;
}

std::string ExtrudeOperation::prompt() const
{
    return pickingTarget_ ? std::string("Click a flat face parallel to the sketch to extrude up to it \xC2\xB7 Esc cancels")
                          : std::string();
}

void ExtrudeOperation::setSymmetric(bool symmetric, const doc::Document& document)
{
    symmetric_ = symmetric;
    // The number stays: 10 one way becomes a 10 mm thick slab centered on the
    // sketch (a symmetric thickness is never negative).
    if (editingDraft()) {
        distance_ = std::abs(distance_);
        setValue(value(), document);
        return;
    }
    setValue(std::abs(value()), document);
}

Status ExtrudeOperation::extendToFace(const doc::Document& document, const Uuid& bodyId, int faceIndex)
{
    const doc::Body* body = document.body(bodyId);
    const auto info = body ? geom::faceInfo(body->shape(), faceIndex) : std::nullopt;
    const Vec3 normal = manipulator().direction().normalized();
    if (!info || !info->isPlanar() || std::abs(info->normal.normalized().dot(normal)) < 1 - 1e-9)
        return Status::failure(ErrorCode::InvalidArgument, "Pick a flat face parallel to the sketch.",
                               "extrude up to: face not planar/parallel");
    const double distance = (info->planeOrigin - manipulator().anchor(0)).dot(normal);
    if (std::abs(distance) < 1e-6)
        return Status::failure(ErrorCode::InvalidArgument, "That face lies in the sketch's plane.", "extrude up to: distance 0");
    pickingTarget_ = false;
    symmetric_ = false;
    setActiveHandle(0);
    setValue(distance, document);
    return okStatus();
}

std::unique_ptr<cmd::Command> ExtrudeOperation::makeCommand(const doc::Document& document) const
{
    if (mode() == doc::ExtrudeMode::NewBody) {
        return std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), makeFeature(value()));
    }
    return std::make_unique<cmd::AddFeatureCommand>(*host_, makeFeature(value()));
}

} // namespace os::interact
