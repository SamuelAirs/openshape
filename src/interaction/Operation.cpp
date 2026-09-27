// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/Operation.h"

#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "geometry/Text.h"

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

// A preview mesh's box (what the value chip keeps clear of when the preview
// is itself what moves or is made).
geom::BoundingBox meshBounds(const geom::Mesh& mesh)
{
    geom::BoundingBox box;
    if (mesh.vertexCount() == 0)
        return box;
    box.min = box.max = mesh.vertex(0);
    for (std::size_t i = 1; i < mesh.vertexCount(); ++i) {
        const Vec3 v = mesh.vertex(i);
        box.min = {std::min(box.min.x, v.x), std::min(box.min.y, v.y), std::min(box.min.z, v.z)};
        box.max = {std::max(box.max.x, v.x), std::max(box.max.y, v.y), std::max(box.max.z, v.z)};
    }
    box.valid = true;
    return box;
}
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
    if (!previewsShape()) {
        // Worked out here, without the kernel (DatumOperation): no worker.
        dropPreviews();
        error_ = refreshPreview(value, document);
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
            outcome.bounds = meshBounds(*outcome.mesh); // here: on the worker, not on the GUI thread
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
    previewBounds_ = outcome.bounds;
    // Where the operation that computed this preview had taken the selection
    // (the worker's copy, done with it by now; this operation when synchronous).
    const Operation& by = outcome.computedBy ? *outcome.computedBy : *this;
    previewCarry_ = by.carriedSelection();
}

Operation::Carry Operation::carriedSelection() const
{
    Carry carry;
    if (const int active = activeHandle(); active >= 0 && active < handleCount()) {
        const LinearManipulator h = handle(active);
        carry.shifts.push_back({h.base(), h.anchor(handleOffset(active))});
    }
    carry.wholePreview = previewBody().isNil();
    return carry;
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

// Separate bodies: the source and its copies side by side, not fused. Each
// copy is the source with its last step (`steps`: the Mirror or Move step the
// copy's own history ends with) applied.
Result<geom::Shape> previewCopies(const doc::Document& document, const Uuid& source,
                                  const std::vector<std::unique_ptr<doc::Feature>>& steps)
{
    if (steps.size() > kMaxSeparateCopies)
        return Result<geom::Shape>::failure(ErrorCode::InvalidArgument,
                                            "Separate bodies work for up to " + std::to_string(kMaxSeparateCopies) + " copies.",
                                            "separate copies: too many");
    const doc::Body* body = document.body(source);
    if (!body || body->shape().isNull())
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body no longer exists.", "previewCopies: body");
    // Each copy stores the body's imported geometry again.
    if (Status fits = cmd::checkImportedCopiesFit(document, source, steps.size()); !fits)
        return Result<geom::Shape>::failure(fits.error(), fits.userMessage() + " Turn off Separate bodies to join them.",
                                            fits.developerMessage());
    std::vector<geom::Shape> shapes{body->shape()};
    for (const auto& step : steps) {
        auto shape = document.preview(source, *step);
        if (!shape)
            return shape;
        shapes.push_back(shape.value());
    }
    return geom::gatherSolids(shapes);
}

// One new, independent body per copy ("Body 2", "Body 3", ...), as one undo step.
std::unique_ptr<cmd::Command> createCopyBodies(const doc::Document& document, const Uuid& source,
                                               std::vector<std::unique_ptr<doc::Feature>> steps, const char* label)
{
    const std::vector<std::string> names = document.nextBodyNames(steps.size());
    return cmd::makeCopyBodiesCommand(source, std::move(steps), names, label);
}

// A join whose result has more separate pieces than the body had did not
// touch it: the user meant a new body (as Shapr3D does).
bool joinMissedBody(const geom::Shape& result, const doc::Document& document, const std::optional<Uuid>& host)
{
    const doc::Body* body = host ? document.body(*host) : nullptr;
    return body && result.solidCount() > std::max(body->shape().solidCount(), 1);
}

// Copies (a mirror image, pattern copies) of which none touches the body or
// another copy: the joined result has (copies + 1) times the body's pieces.
// Then the user meant separate bodies (as Shapr3D does). One that touches a
// piece of a body in several pieces joins it: the result has fewer.
bool copiesMissedBody(const geom::Shape& result, const doc::Document& document, const Uuid& host, int copies)
{
    const doc::Body* body = document.body(host);
    return body && copies > 0 && result.solidCount() == (copies + 1) * std::max(body->shape().solidCount(), 1);
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
    const auto box = geom::approximateBoundingBox(body->shape());
    return std::unique_ptr<AlignOperation>(new AlignOperation(bodyId, *frame, box.valid ? box.center() : frame->point));
}

std::string AlignOperation::prompt() const
{
    return target_ ? std::string()
                   : std::string("Click the face or edge to align to (on another body, or an axis line), "
                                 "or choose an axis, a plane or the origin \xC2\xB7 Esc cancels");
}

void AlignOperation::forgetTarget()
{
    target_.reset();
    datumTarget_ = Uuid();
    targetKindOf_ = TargetOf::None;
    targetBody_ = Uuid();
    targetKind_ = geom::SubShapeKind::Whole;
    targetIndex_ = -1;
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
    forgetTarget();
    target_ = *frame;
    targetKindOf_ = TargetOf::Body;
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
    forgetTarget();
    target_ = geom::AlignFrame{{source_.point.x, source_.point.y, 0.0}, {0, 0, 1}, true, geom::AlignFrame::Extent::Finite};
    targetKindOf_ = TargetOf::Ground;
    setValue(value(), document);
    return okStatus();
}

void AlignOperation::setEndlessTarget(Vec3 point, Vec3 direction, bool plane, const doc::Document& document)
{
    Vec3 d = direction.normalized();
    geom::AlignFrame frame;
    if (plane) {
        // A plane has two sides: the body stays on the one it is on (a flat
        // face then touches the plane from there; Flip turns it over).
        if ((bodyCenter_ - point).dot(d) < -1e-9)
            d = d * -1.0;
        frame = {source_.point - d * (source_.point - point).dot(d), d, true, geom::AlignFrame::Extent::Plane};
    } else {
        frame = {point + d * (source_.point - point).dot(d), d, false, geom::AlignFrame::Extent::Line};
    }
    target_ = frame; // the arrow sits where the source lands
    setValue(value(), document);
}

Status AlignOperation::setOriginTarget(OriginTarget target, const doc::Document& document)
{
    forgetTarget();
    targetKindOf_ = TargetOf::Origin;
    origin_ = target;
    switch (target) {
    case OriginTarget::XAxis: setEndlessTarget({}, {1, 0, 0}, false, document); break;
    case OriginTarget::YAxis: setEndlessTarget({}, {0, 1, 0}, false, document); break;
    case OriginTarget::ZAxis: setEndlessTarget({}, {0, 0, 1}, false, document); break;
    case OriginTarget::XYPlane: setEndlessTarget({}, {0, 0, 1}, true, document); break;
    case OriginTarget::XZPlane: setEndlessTarget({}, {0, 1, 0}, true, document); break;
    case OriginTarget::YZPlane: setEndlessTarget({}, {1, 0, 0}, true, document); break;
    case OriginTarget::Point:
        // No turn: the source's point moves to the origin; an offset goes
        // along the source's own direction.
        target_ = geom::AlignFrame{{}, source_.direction.normalized(), false, geom::AlignFrame::Extent::Point};
        setValue(value(), document);
        break;
    }
    return okStatus();
}

Status AlignOperation::setDatumTarget(const doc::Datum& datum, const doc::Document& document)
{
    forgetTarget();
    targetKindOf_ = TargetOf::Datum;
    datumTarget_ = datum.id();
    const doc::DatumGeometry& g = datum.geometry();
    setEndlessTarget(g.origin, g.direction, datum.kind() == doc::DatumKind::Plane, document);
    return okStatus();
}

void AlignOperation::clearTarget()
{
    forgetTarget();
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

Operation::Carry MoveOperation::carriedSelection() const
{
    return {{{center_, center_ + translation()}}, true};
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

void MirrorOperation::setPlane(const Vec3& origin, const Vec3& normal, const doc::Document& document)
{
    plane_ = Plane{origin, normal.normalized()};
    originAxis_ = -1;
    setValue(value(), document);
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
    separateChoice_ = separate;
    if (plane_)
        setValue(value(), document);
}

std::vector<std::unique_ptr<doc::Feature>> MirrorOperation::makeCopySteps() const
{
    std::vector<std::unique_ptr<doc::Feature>> steps;
    if (!plane_)
        return steps;
    auto image = std::make_unique<doc::MirrorFeature>();
    image->planeOrigin = plane_->origin;
    image->planeNormal = plane_->normal;
    image->keepOriginal = false;
    steps.push_back(std::move(image));
    return steps;
}

Result<geom::Shape> MirrorOperation::computePreview(double value, const doc::Document& document) const
{
    return separate() ? previewCopies(document, bodyId(), makeCopySteps()) : Operation::computePreview(value, document);
}

bool MirrorOperation::reconsider(const geom::Shape& result, const doc::Document& document)
{
    // The joined image would not touch the body: it becomes its own body
    // (unless its imported geometry would not fit in the project).
    if (separateChoice_ || autoSeparate_ || !copiesMissedBody(result, document, bodyId(), 1)
        || !cmd::checkImportedCopiesFit(document, bodyId(), 1))
        return false;
    autoSeparate_ = true;
    return true;
}

std::unique_ptr<cmd::Command> MirrorOperation::makeCommand(const doc::Document& document) const
{
    return separate() ? createCopyBodies(document, bodyId(), makeCopySteps(), "Mirror") : Operation::makeCommand(document);
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

void PatternOperation::setAxisLine(const Vec3& point, const Vec3& direction, const doc::Document& document)
{
    customOrigin_ = point;
    customAxis_ = direction.normalized();
    axisIndex_ = -1;
    setValue(circular_ ? value() : defaultSpacing(), document);
}

LinearManipulator PatternOperation::handle(int index) const
{
    if (index != 0 || circular_)
        return {};
    return LinearManipulator(center_, axisVectorFor());
}

Operation::Carry PatternOperation::carriedSelection() const
{
    Carry carry = Operation::carriedSelection();
    carry.wholePreview = true;
    return carry;
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
    separateChoice_ = separate;
    setValue(value(), document);
}

std::vector<std::unique_ptr<doc::Feature>> PatternOperation::makeCopySteps(double value) const
{
    std::vector<std::unique_ptr<doc::Feature>> steps;
    const auto feature = makeFeature(value);
    for (const geom::RigidMotion& motion : static_cast<const doc::PatternFeature&>(*feature).copies()) {
        auto move = std::make_unique<doc::MoveFeature>();
        move->setName("Pattern copy");
        move->setMotion(motion);
        steps.push_back(std::move(move));
    }
    return steps;
}

Result<geom::Shape> PatternOperation::computePreview(double value, const doc::Document& document) const
{
    return separate() ? previewCopies(document, bodyId(), makeCopySteps(value)) : Operation::computePreview(value, document);
}

bool PatternOperation::reconsider(const geom::Shape& result, const doc::Document& document)
{
    // Copies that would touch neither the body nor each other become bodies
    // of their own (not beyond the separate-bodies limit, nor when their
    // imported geometry would not fit in the project: then they stay joined).
    if (separateChoice_ || autoSeparate_ || count_ - 1 > static_cast<int>(kMaxSeparateCopies)
        || !copiesMissedBody(result, document, bodyId(), count_ - 1)
        || !cmd::checkImportedCopiesFit(document, bodyId(), std::size_t(count_ - 1)))
        return false;
    autoSeparate_ = true;
    return true;
}

std::unique_ptr<cmd::Command> PatternOperation::makeCommand(const doc::Document& document) const
{
    return separate() ? createCopyBodies(document, bodyId(), makeCopySteps(value()), "Pattern")
                      : Operation::makeCommand(document);
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
                                                     doc::HoleKind kind, std::size_t presetIndex, double allowance)
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
    op->allowance_ = doc::validHoleAllowance(allowance);
    op->setPreset(std::min(presetIndex, doc::metricScrews().size() - 1), document);
    return op;
}

double HeadOperation::presetDiameter(std::size_t index) const
{
    const doc::ScrewSize& screw = doc::metricScrews()[std::min(index, doc::metricScrews().size() - 1)];
    return kind_ == doc::HoleKind::Counterbore ? doc::counterboreDiameterFor(screw, allowance_)
                                               : doc::countersinkDiameterFor(screw, allowance_);
}

void HeadOperation::setAllowance(double allowance, const doc::Document& document)
{
    allowance = doc::validHoleAllowance(allowance);
    if (std::abs(allowance - allowance_) < 1e-12)
        return;
    const bool fromPreset = presetIndex().has_value();
    allowance_ = allowance;
    if (fromPreset)
        setPreset(presetIndex_, document); // a typed size stays as typed
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
    if (std::abs(diameter() - presetDiameter(presetIndex_)) > 1e-9
        || (counterbore && std::abs(depth() - screw.counterboreDepth) > 1e-9))
        return std::nullopt;
    return presetIndex_;
}

void HeadOperation::setPreset(std::size_t index, const doc::Document& document)
{
    presetIndex_ = std::min(index, doc::metricScrews().size() - 1);
    const doc::ScrewSize& screw = doc::metricScrews()[presetIndex_];
    diameter_ = presetDiameter(presetIndex_);
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
    const bool matches = std::abs(feature->diameter - presetDiameter(presetIndex_)) < 1e-9
                      && (!counterbore || std::abs(feature->depth - screw.counterboreDepth) < 1e-9);
    feature->preset = matches ? screw.name + doc::allowanceSuffix(allowance_) : "";
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
    op->settings_.allowance = doc::validHoleAllowance(settings.allowance);
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
    return doc::holeDiameterFor(doc::metricScrews()[settings_.screw], settings_.fit, settings_.allowance);
}

double HoleOperation::headDiameter() const
{
    const doc::ScrewSize& screw = doc::metricScrews()[settings_.screw];
    return settings_.head == doc::HoleKind::Countersink ? doc::countersinkDiameterFor(screw, settings_.allowance)
                                                        : doc::counterboreDiameterFor(screw, settings_.allowance);
}

void HoleOperation::setAllowance(double allowance, const doc::Document& document)
{
    allowance = doc::validHoleAllowance(allowance);
    if (std::abs(allowance - settings_.allowance) < 1e-12)
        return;
    storeValue();
    const bool fromPreset = std::abs(diameter_ - presetDiameter()) < 1e-9;
    settings_.allowance = allowance;
    if (fromPreset)
        applyPreset(); // a typed diameter stays as typed
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
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
    feature->headDiameter = headDiameter();
    feature->headDepth = screw.counterboreDepth;
    feature->headAngle = doc::kCountersinkAngleDegrees * kPi / 180.0;
    // Named after the screw only while the diameter is the preset's.
    if (std::abs(feature->diameter - presetDiameter()) < 1e-9)
        feature->preset = doc::holeFitLabel(screw, settings_.fit, settings_.allowance);
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

// ---- Text tool ------------------------------------------------------------------------

std::unique_ptr<TextOperation> TextOperation::create(const doc::Document& document, const Uuid& bodyId, int faceIndex,
                                                     const TextSettings& settings)
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
    auto op = std::unique_ptr<TextOperation>(new TextOperation(bodyId, doc::FaceRef{faceIndex, *signature}, *frame));
    op->shape_ = shape;
    op->outline_ = geom::faceOutline(shape, faceIndex, frame->origin, frame->xAxis, frame->yAxis);
    if (!op->outline_.valid)
        return nullptr;
    // The text starts at the face's center (of its outline), or at a point
    // inside the face when that is off it (a ring, an L).
    const Vec2 center{(op->outline_.minU + op->outline_.maxU) / 2, (op->outline_.minV + op->outline_.maxV) / 2};
    op->position_ = op->onOutline(center)
                        ? center
                        : frame->toLocal(geom::pointOnFace(shape, faceIndex, info->centroid).value_or(info->centroid));
    op->text_ = settings.text;
    op->size_ = std::clamp(settings.size, geom::kMinCapHeight, geom::kMaxCapHeight);
    op->depth_ = std::abs(settings.depth) < 1e-3 ? 1.0 : settings.depth;
    // A remembered angle that was refused (typed out of range) starts over.
    op->angleDegrees_ = std::abs(settings.angleDegrees) <= 360.0 ? settings.angleDegrees : 0.0;
    op->bold_ = settings.bold;
    op->setStoredValue(op->depth_);
    op->setValue(op->depth_, document); // a remembered text previews at once
    op->initial_ = op->settings();
    return op;
}

bool TextOperation::edited() const
{
    if (edited_)
        return true;
    const TextSettings now = settings();
    // A drag of the arrow, or a value typed, changes a field.
    return now.text != initial_.text || now.bold != initial_.bold || std::abs(now.size - initial_.size) > 1e-12
        || std::abs(now.depth - initial_.depth) > 1e-12 || std::abs(now.angleDegrees - initial_.angleDegrees) > 1e-12;
}

std::string TextOperation::valueLabel() const
{
    switch (field_) {
    case Field::Depth: return "Depth";
    case Field::Size: return "Size";
    case Field::Angle: return "Angle";
    }
    return "Depth";
}

std::string TextOperation::prompt() const
{
    return text_.empty() ? std::string("Type the text in the box \xC2\xB7 click the face to move it "
                                       "(it snaps to the center and the middles of the edges)")
                         : std::string();
}

bool TextOperation::canCommit() const
{
    return !text_.empty() && std::abs(depth()) >= 1e-3 && previewUsable();
}

LinearManipulator TextOperation::handle(int index) const
{
    return index == 0 ? LinearManipulator(center(), frame_.normal) : LinearManipulator();
}

void TextOperation::setActiveHandle(int index)
{
    Operation::setActiveHandle(0);
    if (index == 0 && field_ != Field::Depth) {
        storeValue();
        field_ = Field::Depth;
        setStoredValue(depth_);
    }
}

TextSettings TextOperation::settings() const
{
    // A value refused in the active field is not kept: the last accepted one is.
    const bool accepted = checkValue(value()).empty();
    auto kept = [&](Field field) { return field == field_ && accepted ? value() : fieldValue(field); };
    return {text_, kept(Field::Size), kept(Field::Depth), kept(Field::Angle), bold_};
}

double TextOperation::fieldValue(Field field) const
{
    switch (field) {
    case Field::Depth: return depth_;
    case Field::Size: return size_;
    case Field::Angle: return angleDegrees_;
    }
    return depth_;
}

void TextOperation::storeValue()
{
    // A refused value (an angle beyond 360 degrees, a size of 2000 mm) stays
    // in its field with its message; the step keeps the last accepted one.
    if (!checkValue(value()).empty())
        return;
    switch (field_) {
    case Field::Depth: depth_ = value(); break;
    case Field::Size: size_ = value(); break;
    case Field::Angle: angleDegrees_ = value(); break;
    }
}

std::string TextOperation::checkValue(double value) const
{
    if (field_ == Field::Depth && std::abs(value) < 1e-3)
        return "The depth must not be zero: positive raises the text, negative cuts it in.";
    if (field_ == Field::Size && (!(value >= geom::kMinCapHeight) || !(value <= geom::kMaxCapHeight)))
        return "The size (the height of capital letters) must be between 0.5 and 1000 mm.";
    if (field_ == Field::Angle && !(std::abs(value) <= 360.0 + 1e-9))
        return "The angle must be between -360\xC2\xB0 and 360\xC2\xB0.";
    return {};
}

void TextOperation::setText(const std::string& text, const doc::Document& document)
{
    edited_ = true;
    wordsTyped_ = true; // even the same words: typed on purpose (the next key adds to them)
    // The same words again (Enter in the text field sends them once more):
    // the preview shown or computing, or its verdict, stays.
    if (text == text_ && (previewPending() || hasPreview() || !error().empty()))
        return;
    text_ = text;
    setValue(value(), document);
}

void TextOperation::setField(Field field, const doc::Document& document)
{
    storeValue();
    field_ = field;
    setStoredValue(fieldValue(field));
    setValue(value(), document);
}

void TextOperation::nextField(const doc::Document& document)
{
    setField(field_ == Field::Depth ? Field::Size : field_ == Field::Size ? Field::Angle : Field::Depth, document);
}

void TextOperation::setAngleDegrees(double degrees, const doc::Document& document)
{
    storeValue();
    angleDegrees_ = degrees;
    edited_ = true;
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
}

void TextOperation::setRaised(bool raised, const doc::Document& document)
{
    storeValue();
    depth_ = raised ? std::abs(depth_) : -std::abs(depth_);
    edited_ = true;
    setStoredValue(fieldValue(field_));
    setValue(value(), document);
}

void TextOperation::setBold(bool bold, const doc::Document& document)
{
    bold_ = bold;
    edited_ = true;
    setValue(value(), document);
}

bool TextOperation::onFace(Vec2 p) const
{
    return geom::faceContains(shape_, face_.indexHint, frame_.toWorld(p));
}

bool TextOperation::onOutline(Vec2 p) const
{
    const double size = std::max({outline_.maxU - outline_.minU, outline_.maxV - outline_.minV, 1.0});
    return geom::outlineContains(outline_, p, 1e-7 * size);
}

std::pair<Vec2, std::string> TextOperation::snap(const Vec3& world, double snapDistance) const
{
    const Vec2 p = frame_.toLocal(world);
    const Vec2 center{(outline_.minU + outline_.maxU) / 2, (outline_.minV + outline_.maxV) / 2};
    std::vector<std::pair<Vec2, const char*>> points{{center, "center"}};
    for (const Vec3& m : outline_.edgeMidpoints)
        points.push_back({frame_.toLocal(m), "midpoint"});
    // Onto a point (on the face), else lined up in X or Y with one.
    std::optional<Vec2> best;
    std::string what;
    double bestDistance = snapDistance;
    for (const auto& [q, name] : points)
        if (const double d = (q - p).length(); d <= bestDistance && onOutline(q)) {
            bestDistance = d;
            best = q;
            what = name;
        }
    if (best)
        return {*best, what};
    double du = snapDistance, dv = snapDistance;
    std::optional<double> u, v;
    for (const auto& [q, name] : points) {
        if (const double d = std::abs(q.x - p.x); d <= du) {
            du = d;
            u = q.x;
        }
        if (const double d = std::abs(q.y - p.y); d <= dv) {
            dv = d;
            v = q.y;
        }
    }
    for (const auto& [x, y] : {std::pair{u, v}, std::pair{u, std::optional<double>()}, std::pair{std::optional<double>(), v}}) {
        if (!x && !y)
            continue;
        const Vec2 q{x.value_or(p.x), y.value_or(p.y)};
        if (onOutline(q))
            return {q, "aligned"};
    }
    return {p, ""};
}

std::string TextOperation::placeAt(const Vec3& world, double snapDistance, const doc::Document& document)
{
    const auto [point, what] = snap(world, snapDistance);
    if (!onOutline(point))
        return {};
    position_ = point;
    edited_ = true;
    setValue(value(), document);
    return what;
}

void TextOperation::measureExtent() const
{
    const double s = size();
    if (text_.empty() || (extent_.text == text_ && std::abs(extent_.size - s) <= 1e-12 && extent_.bold == bold_))
        return;
    Extent fresh;
    fresh.text = text_;
    fresh.size = s;
    fresh.bold = bold_;
    const auto faces = geom::textFaces({text_, bold_ ? doc::kTextFontBold : doc::kTextFontRegular, s});
    if (faces) {
        const geom::BoundingBox box = geom::approximateBoundingBox(faces.value());
        fresh.valid = box.valid;
        fresh.minX = box.min.x;
        fresh.maxX = box.max.x;
        fresh.minY = box.min.y;
        fresh.maxY = box.max.y;
    }
    extent_ = fresh;
}

void TextOperation::adoptAutomaticChoices(const Operation& from)
{
    if (const auto* worker = dynamic_cast<const TextOperation*>(&from); worker && !worker->extent_.text.empty())
        extent_ = worker->extent_;
}

std::vector<Vec3> TextOperation::textCorners() const
{
    // The extent measured with the preview (never here: this runs on the GUI
    // thread, where a kernel call would wait for the worker's preview).
    if (text_.empty() || !extent_.valid)
        return {};
    const double a = angleDegrees() * kPi / 180.0;
    const Vec3 along = frame_.xAxis * std::cos(a) + frame_.yAxis * std::sin(a);
    const Vec3 up = frame_.normal.cross(along);
    const Vec3 c = center();
    std::vector<Vec3> corners;
    for (const auto& [x, y] : {std::pair{extent_.minX, extent_.minY}, std::pair{extent_.maxX, extent_.minY},
                               std::pair{extent_.maxX, extent_.maxY}, std::pair{extent_.minX, extent_.maxY}})
        corners.push_back(c + along * x + up * y);
    return corners;
}

std::unique_ptr<doc::Feature> TextOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::TextFeature>();
    feature->face = face_;
    feature->position = position_;
    feature->text = text_;
    feature->size = field_ == Field::Size ? value : size_;
    feature->depth = field_ == Field::Depth ? value : depth_;
    // Stored in [0, 2 pi) whatever was typed (-90 degrees is 270).
    feature->angle = doc::TextFeature::normalizedAngle((field_ == Field::Angle ? value : angleDegrees_) * kPi / 180.0);
    feature->font = bold_ ? doc::kTextFontBold : doc::kTextFontRegular;
    return feature;
}

Result<geom::Shape> TextOperation::computePreview(double value, const doc::Document& document) const
{
    measureExtent(); // for textCorners (adopted from the worker's copy)
    if (text_.empty()) {
        const doc::Body* body = document.body(bodyId());
        if (!body)
            return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body no longer exists.", "text: body");
        return Result<geom::Shape>::success(body->shape());
    }
    if (!onFace(position_))
        return Result<geom::Shape>::failure(ErrorCode::InvalidArgument, "The text's center is off the face: click on the face.",
                                            "text: center off the face");
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

Operation::Carry ExtrudeOperation::carriedSelection() const
{
    const Vec3 base = manipulator().base();
    const Vec3 end = manipulator().anchor(displayOffset(distance()));
    Carry carry{{{base, end}}, previewBody().isNil()};
    if (symmetric_)
        carry.shifts.push_back({base, base - (end - base)});
    return carry;
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

// ---- Loft -------------------------------------------------------------------------

std::unique_ptr<LoftOperation> LoftOperation::create(const doc::Document& document, std::vector<doc::LoftSection> sections,
                                                     bool ruled, std::optional<doc::ExtrudeMode> mode)
{
    if (sections.empty())
        return nullptr;
    // The body the first profile drawn on a (shown) body's face lies on.
    std::optional<Uuid> host;
    Vec3 center;
    for (const doc::LoftSection& section : sections) {
        const sketch::Sketch* sk = document.sketch(section.sketchId);
        if (!sk)
            return nullptr;
        center = center + sk->plane().toWorld(section.profile.interiorPoint) / double(sections.size());
        if (!host && sk->hostBody())
            if (const doc::Body* body = document.body(*sk->hostBody()); body && body->isVisible())
                host = sk->hostBody();
    }
    auto op = std::unique_ptr<LoftOperation>(new LoftOperation(host, std::move(sections), ruled, center));
    if (mode && (host || *mode == doc::ExtrudeMode::NewBody)) {
        op->mode_ = *mode;
        op->modeChosen_ = true;
    }
    return op;
}

doc::ExtrudeMode LoftOperation::mode() const
{
    if (!host_)
        return doc::ExtrudeMode::NewBody;
    if (modeChosen_)
        return mode_;
    return autoNewBody_ ? doc::ExtrudeMode::NewBody : doc::ExtrudeMode::Join;
}

void LoftOperation::setRuled(bool ruled, const doc::Document& document)
{
    ruled_ = ruled;
    setValue(value(), document);
}

void LoftOperation::setMode(doc::ExtrudeMode mode, const doc::Document& document)
{
    mode_ = mode;
    modeChosen_ = true;
    setValue(value(), document);
}

Uuid LoftOperation::previewBody() const
{
    return mode() == doc::ExtrudeMode::NewBody ? Uuid() : host_.value_or(Uuid());
}

bool LoftOperation::reconsider(const geom::Shape& result, const doc::Document& document)
{
    // A join that would not touch the body: the user meant a new body.
    if (modeChosen_ || mode() != doc::ExtrudeMode::Join || !joinMissedBody(result, document, host_))
        return false;
    autoNewBody_ = true;
    return true;
}

std::unique_ptr<doc::Feature> LoftOperation::makeFeature(double) const
{
    auto feature = std::make_unique<doc::LoftFeature>();
    feature->sections = sections_;
    feature->ruled = ruled_;
    feature->mode = mode();
    return feature;
}

std::unique_ptr<cmd::Command> LoftOperation::makeCommand(const doc::Document& document) const
{
    if (!host_ || mode() == doc::ExtrudeMode::NewBody)
        return std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), makeFeature(value()));
    return std::make_unique<cmd::AddFeatureCommand>(*host_, makeFeature(value()));
}

// ---- Construct: axes and planes -------------------------------------------------------

std::unique_ptr<DatumOperation> DatumOperation::create(doc::DatumKind kind)
{
    auto op = std::unique_ptr<DatumOperation>(new DatumOperation(kind));
    op->mode_ = kind == doc::DatumKind::Axis ? Mode::Axis : Mode::PlaneOffset;
    return op;
}

std::string DatumOperation::valueLabel() const
{
    return mode_ == Mode::PlaneOffset ? "Distance" : mode_ == Mode::PlaneAngle ? "Angle" : "";
}

std::string DatumOperation::prompt() const
{
    const std::string cancel = " \xC2\xB7 Esc cancels";
    switch (mode_) {
    case Mode::Axis:
        return refs_.empty() ? "Click a hole, a shaft, a circle or a straight edge (or choose Two points or Parallel)" + cancel
                             : std::string();
    case Mode::AxisTwoPoints:
        if (refs_.empty())
            return "Click the first point: an edge near its end (a corner) or a circle (its center)" + cancel;
        return refs_.size() == 1 ? "Click the second point" + cancel : std::string();
    case Mode::AxisParallel:
        return refs_.empty() ? "Click the point it goes through: an edge near its end (a corner) or a circle (its center)" + cancel
                             : std::string();
    case Mode::PlaneOffset:
        return refs_.empty() && originIndex_ < 0 ? "Click a flat face to offset from, or choose an origin plane" + cancel
                                                 : std::string();
    case Mode::PlaneAngle:
        if (refs_.empty())
            return "Click the straight edge the plane goes through" + cancel;
        return refs_.size() == 1 ? "Click the flat face the angle is measured from" + cancel : std::string();
    case Mode::PlaneMidway:
        if (refs_.empty())
            return "Click the first of two parallel flat faces" + cancel;
        return refs_.size() == 1 ? "Click the second face, parallel to the first" + cancel : std::string();
    }
    return {};
}

int DatumOperation::handleCount() const
{
    return mode_ == Mode::PlaneOffset && offsetBase_ ? 1 : 0;
}

LinearManipulator DatumOperation::handle(int index) const
{
    if (index != 0 || !offsetBase_)
        return {};
    return LinearManipulator(offsetBase_->first, offsetBase_->second);
}

std::optional<Vec3> DatumOperation::labelAnchor() const
{
    if (mode_ == Mode::PlaneAngle && !refs_.empty())
        return edgeMiddle_;
    return std::nullopt;
}

void DatumOperation::startOver(const doc::Document& document)
{
    refs_.clear();
    picked_.clear();
    resolvedStale_ = true;
    originIndex_ = -1;
    offsetBase_.reset();
    preview_.reset();
    clearPreview();
    setStoredValue(neutralValue());
    setValue(value(), document);
}

void DatumOperation::setMode(Mode mode, const doc::Document& document)
{
    if (mode == mode_)
        return;
    mode_ = mode;
    startOver(document);
}

void DatumOperation::setParallelTo(int axis, const doc::Document& document)
{
    if (mode_ != Mode::AxisParallel)
        setMode(Mode::AxisParallel, document); // a point picked for it stays when only the axis changes
    originIndex_ = std::clamp(axis, 0, 2);
    setValue(value(), document);
}

void DatumOperation::setOriginPlane(int normalAxis, const doc::Document& document)
{
    if (mode_ != Mode::PlaneOffset)
        setMode(Mode::PlaneOffset, document);
    refs_.clear();
    picked_.clear();
    resolvedStale_ = true;
    originIndex_ = std::clamp(normalAxis, 0, 2);
    offsetBase_ = std::pair{Vec3{}, axisVector(originIndex_)};
    setValue(value(), document); // a typed distance is kept
}

Status DatumOperation::pick(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index,
                            const Vec3& point)
{
    auto refuse = [](const std::string& text) {
        return Status::failure(ErrorCode::InvalidArgument, text, "construct: pick does not fit");
    };
    const doc::Body* body = document.body(bodyId);
    if (!body || body->shape().isNull())
        return refuse("That body no longer exists.");
    const auto face = kind == geom::SubShapeKind::Face ? geom::faceInfo(body->shape(), index) : std::nullopt;
    const auto edge = kind == geom::SubShapeKind::Edge ? geom::edgeInfo(body->shape(), index) : std::nullopt;
    using K = doc::GeometryRef::Kind;
    auto add = [&](K refKind, geom::SubShapeKind pickedKind, int pickedIndex) {
        const auto ref = doc::makeGeometryRef(*body, refKind, pickedIndex, point);
        if (!ref)
            return false;
        refs_.push_back(*ref);
        picked_.push_back({bodyId, pickedKind, pickedIndex});
        return true;
    };
    auto clearPicks = [&] {
        refs_.clear();
        picked_.clear();
    };
    auto pointKind = [&]() { return edge->kind == geom::CurveKind::Circle ? K::Center : K::Vertex; };
    const std::string pointHelp = "Click an edge near its end for a corner, or a circle for its center.";
    bool added = false;
    switch (mode_) {
    case Mode::Axis:
        if (!(face && face->hasAxis()) && !(edge && (edge->kind == geom::CurveKind::Circle || edge->kind == geom::CurveKind::Line)))
            return refuse("An axis goes through a hole, a shaft or a circle, or along a straight edge.");
        clearPicks(); // a new pick re-aims it
        added = add(face ? K::Face : K::Edge, kind, index);
        break;
    case Mode::AxisTwoPoints:
        if (!edge || (edge->kind != geom::CurveKind::Circle && (edge->start - edge->end).length() < 1e-9))
            return refuse(pointHelp);
        if (refs_.size() >= 2) { // re-aims the second point
            refs_.pop_back();
            picked_.pop_back();
        }
        added = add(pointKind(), kind, index);
        break;
    case Mode::AxisParallel:
        if (!edge || (edge->kind != geom::CurveKind::Circle && (edge->start - edge->end).length() < 1e-9))
            return refuse(pointHelp);
        clearPicks();
        added = add(pointKind(), kind, index);
        break;
    case Mode::PlaneOffset:
        if (!face || !face->isPlanar())
            return refuse("Offset a plane from a flat face, or choose an origin plane.");
        clearPicks();
        originIndex_ = -1;
        added = add(K::Face, kind, index);
        if (added)
            offsetBase_ = std::pair{geom::pointOnFace(body->shape(), index, face->centroid).value_or(face->centroid),
                                    face->normal.normalized()};
        break;
    case Mode::PlaneAngle:
        if (edge) {
            if (edge->kind != geom::CurveKind::Line)
                return refuse("An angled plane goes through a straight edge.");
            clearPicks();
            added = add(K::Edge, kind, index);
            edgeMiddle_ = edge->midpoint;
            // The angle is measured from a flat face next to the edge that it
            // runs along: the one facing up the most (a box's top rather than
            // its side), then the larger one; another face can be clicked.
            {
                int best = -1;
                double bestUp = 0, bestArea = 0;
                for (int f : geom::facesOfEdge(body->shape(), index)) {
                    const auto info = geom::faceInfo(body->shape(), f);
                    if (!info || !info->isPlanar() || std::abs(info->normal.normalized().dot(edge->tangent.normalized())) > 1e-6)
                        continue;
                    const double up = info->normal.normalized().z;
                    if (best < 0 || up > bestUp + 1e-9 || (std::abs(up - bestUp) <= 1e-9 && info->area > bestArea + 1e-9)) {
                        best = f;
                        bestUp = up;
                        bestArea = info->area;
                    }
                }
                if (best >= 0)
                    add(K::Face, geom::SubShapeKind::Face, best);
            }
        } else if (face && face->isPlanar()) {
            if (refs_.empty())
                return refuse("Click the straight edge the plane goes through first.");
            if (std::abs(face->normal.normalized().dot(refs_[0].edge.signature.tangent.normalized())) > 1e-6)
                return refuse("The edge must run along the face the angle is measured from.");
            if (refs_.size() >= 2) {
                refs_.pop_back();
                picked_.pop_back();
            }
            added = add(K::Face, kind, index);
        } else {
            return refuse("Click a straight edge, then the flat face the angle is measured from.");
        }
        break;
    case Mode::PlaneMidway:
        if (!face || !face->isPlanar())
            return refuse("A plane midway goes between two parallel flat faces.");
        if (refs_.size() >= 2) {
            refs_.pop_back();
            picked_.pop_back();
        }
        if (refs_.size() == 1
            && std::abs(std::abs(face->normal.normalized().dot(refs_[0].face.signature.normal.normalized())) - 1.0) > 1e-6)
            return refuse("Pick a face parallel to the first one.");
        added = add(K::Face, kind, index);
        break;
    }
    resolvedStale_ = true;
    if (!added)
        return refuse("That cannot be used here.");
    setValue(value(), document);
    return okStatus();
}

bool DatumOperation::dropLastPick(const doc::Document& document)
{
    if (mode_ == Mode::PlaneOffset && originIndex_ >= 0) {
        originIndex_ = -1;
        offsetBase_.reset();
    } else if (!refs_.empty()) {
        refs_.pop_back();
        picked_.pop_back();
        if (mode_ == Mode::PlaneOffset)
            offsetBase_.reset();
    } else {
        return false;
    }
    resolvedStale_ = true;
    setValue(value(), document);
    return true;
}

doc::Datum DatumOperation::datum() const
{
    doc::Datum d;
    d.refs = refs_;
    switch (mode_) {
    case Mode::Axis:
        d.method = !refs_.empty() && refs_[0].kind == doc::GeometryRef::Kind::Edge
                        && refs_[0].edge.signature.kind == geom::CurveKind::Line
                     ? doc::DatumMethod::AxisAlongEdge
                     : doc::DatumMethod::AxisThrough;
        break;
    case Mode::AxisTwoPoints: d.method = doc::DatumMethod::AxisTwoPoints; break;
    case Mode::AxisParallel:
        d.method = doc::DatumMethod::AxisParallel;
        d.originIndex = originIndex_;
        break;
    case Mode::PlaneOffset:
        d.method = doc::DatumMethod::PlaneOffset;
        d.originIndex = refs_.empty() ? originIndex_ : -1;
        d.distance = value();
        break;
    case Mode::PlaneAngle:
        d.method = doc::DatumMethod::PlaneAngle;
        d.angle = value() * kPi / 180.0; // beyond +-180 degrees: refused (refreshPreview)
        break;
    case Mode::PlaneMidway: d.method = doc::DatumMethod::PlaneMidway; break;
    }
    return d;
}

std::string DatumOperation::refreshPreview(double, const doc::Document& document)
{
    preview_.reset();
    const doc::Datum d = datum();
    // The limits of the Model panel's fields and of the file: a typed
    // distance or angle beyond them is refused, never clamped or kept.
    if (const Status values = doc::checkDatumValues(d); !values)
        return values.userMessage();
    const std::size_t needed = mode_ == Mode::AxisTwoPoints || mode_ == Mode::PlaneAngle || mode_ == Mode::PlaneMidway ? 2 : 1;
    const bool complete = mode_ == Mode::PlaneOffset ? (!refs_.empty() || originIndex_ >= 0)
                        : mode_ == Mode::AxisParallel ? (refs_.size() == 1 && originIndex_ >= 0)
                                                      : refs_.size() == needed;
    if (!complete)
        return {}; // still picking: nothing to show, nothing wrong
    // The kernel only for new picks or a changed document; a new distance or
    // angle (a drag step) is plain arithmetic on what was resolved.
    if (resolvedStale_ || resolvedIn_ != &document || resolvedRevision_ != document.revision()) {
        resolved_.clear();
        resolveError_.clear();
        if (auto refs = doc::resolveDatumRefs(d, document.context()))
            resolved_ = std::move(refs.value());
        else
            resolveError_ = refs.userMessage();
        resolvedIn_ = &document;
        resolvedRevision_ = document.revision();
        resolvedStale_ = false;
    }
    if (!resolveError_.empty())
        return resolveError_;
    const auto geometry = doc::datumGeometry(d, resolved_);
    if (!geometry)
        return geometry.userMessage();
    preview_ = geometry.value();
    return {};
}

std::unique_ptr<cmd::Command> DatumOperation::makeCommand(const doc::Document& document) const
{
    doc::Datum d = datum();
    d.setName(document.nextDatumName(kind_));
    if (preview_)
        d.setGeometry(*preview_);
    return std::make_unique<cmd::AddDatumCommand>(std::move(d));
}

} // namespace os::interact
