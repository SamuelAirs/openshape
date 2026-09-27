// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "document/Datum.h"
#include "core/Uuid.h"
#include "document/Feature.h"
#include "geometry/Holes.h"
#include "geometry/Mesh.h"
#include "geometry/Modeling.h"
#include "interaction/Manipulator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace os::doc {
class Document;
}

namespace os::interact {

class Operation;

// A preview computed away from the operation (on the preview worker).
struct PreviewOutcome {
    std::uint64_t operation = 0; // Operation::instance() it belongs to
    std::uint64_t serial = 0;    // the request it answers
    double value = 0;
    std::shared_ptr<const geom::Mesh> mesh; // null when the preview failed
    Uuid meshBody;                          // the body the mesh stands in for (nil: a new body)
    std::string error;                      // user message when it failed
    std::string developerMessage;
    // The mesh's box (world coordinates), measured where it was computed.
    geom::BoundingBox bounds;
    // The operation copy that computed it; carries the automatic choices it
    // made (e.g. a join that missed the body became a new body).
    std::shared_ptr<const Operation> computedBy;
    double milliseconds = 0; // on the worker: kernel + meshing
};

// Where an operation's previews are computed when not on the calling thread
// (InteractionController, with its PreviewWorker).
class PreviewScheduler {
public:
    virtual ~PreviewScheduler() = default;
    // A copy of `document` the worker reads while the caller keeps changing it.
    virtual std::shared_ptr<const doc::Document> previewSnapshot(const doc::Document& document) = 0;
    // Runs `compute` on the worker; replaces a job still waiting to start.
    virtual void schedulePreview(std::function<PreviewOutcome()> compute) = 0;
    // Drops a job still waiting to start.
    virtual void dropScheduledPreview() = 0;
};

// An in-progress, previewable modeling operation driven by a manipulator
// and/or typed values. Nothing touches the document until commit, which
// produces a Command for the undo stack.
//
// Previews: setValue() computes the preview synchronously, or - with a
// PreviewScheduler (the app) - hands a self-contained job to the preview
// worker: a clone() of the operation and a snapshot of the document. The
// last preview stays shown until the new one arrives (acceptPreview); a
// newer value supersedes an older one.
class Operation {
public:
    Operation(Uuid bodyId, LinearManipulator manipulator);
    virtual ~Operation() = default;

    virtual std::string title() const = 0;
    virtual std::string valueLabel() const = 0;
    virtual bool allowsNegative() const = 0;
    virtual doc::FeatureKind featureKind() const = 0;
    // Angle-valued operations take degrees in text input and show "\xC2\xB0".
    virtual bool isAngle() const { return false; }

    const Uuid& bodyId() const { return bodyId_; }
    // Body whose display the preview replaces; nil when the result is a new body.
    virtual Uuid previewBody() const { return bodyId_; }
    const LinearManipulator& manipulator() const { return manipulator_; }
    LinearManipulator& manipulator() { return manipulator_; }

    double value() const { return value_; }
    // What a new preview request changes. Only a new value (a drag step, a
    // typed value) lets a finished preview of an earlier value show while the
    // newest one computes: it keeps up with the drag. After any other change
    // (a mode, a count, a target, Through all: the setters, which call
    // setValue with the default) an earlier request's preview is wrong
    // geometry, and it is not shown.
    enum class Change { Parameters, ValueOnly };
    // Sets the value and recomputes the preview. Invalid values leave an
    // error message and no preview; the previous document state is untouched.
    // With a preview scheduler the preview is computed on the worker: this
    // returns at once, previewPending() until acceptPreview() gets it.
    void setValue(double value, const doc::Document& document, Change change = Change::Parameters);
    // The command refused the value while its preview was still computing
    // (Enter does not wait for it): that refusal is the verdict, shown as a
    // refused preview would be (the message, no preview), and the preview is
    // no longer awaited (the commit dropped it if it had not started).
    void refusePendingValue(std::string message);

    bool hasPreview() const { return previewMesh_ != nullptr; }
    const std::shared_ptr<const geom::Mesh>& previewMesh() const { return previewMesh_; }
    std::uint64_t previewKey() const { return previewKey_; }
    // The body the shown preview mesh stands in for (nil: a new body). While
    // a newer preview is pending, previewBody() may already differ.
    const Uuid& previewMeshBody() const { return previewMeshBody_; }

    // ---- Where the operation takes what it works on ----
    // The value chip keeps clear of the selection also where the operation
    // has taken it (InteractionController::keepClearRect). `shifts`: the
    // selection moved by (to - from), each: a pushed face, a moved body (all
    // axes of a Move), a pattern's last copy, both sides of a symmetric
    // extrusion. `wholePreview`: the shown preview is the thing being made or
    // moved, kept clear as a whole (a moved, turned or aligned body, a mirror
    // or pattern with its copies, a new body). From the operation's state and
    // the preview's mesh: no kernel call.
    struct Shift {
        Vec3 from;
        Vec3 to;
    };
    struct Carry {
        std::vector<Shift> shifts;
        bool wholePreview = false;
    };
    // By default the active arrow's travel (its base to its tip), and the
    // whole preview when it is a new body.
    virtual Carry carriedSelection() const;
    // The same for the shown preview, as the operation that computed it had
    // it (the worker's copy): while a newer value computes, the preview on
    // screen is of an earlier one (behind the arrow, or ahead of it after a
    // drag back). nullptr without a shown preview.
    const Carry* previewCarry() const { return previewMesh_ ? &previewCarry_ : nullptr; }
    // The shown preview mesh's box (world coordinates); nullopt without one.
    std::optional<geom::BoundingBox> previewBounds() const
    {
        return previewMesh_ && previewBounds_.valid ? std::optional<geom::BoundingBox>(previewBounds_) : std::nullopt;
    }
    // The error of the latest finished preview (or of a value refused at once).
    const std::string& error() const { return error_; }
    // A pending preview counts as committable: the command computes the step
    // again anyway (and reports a failure itself).
    virtual bool canCommit() const { return value_ != 0.0 && previewUsable(); }

    // ---- Previews off the calling thread ----
    // From now on previews go to `scheduler` (nullptr: synchronous). Operations
    // created while a PreviewSchedulerScope exists start with its scheduler.
    void setPreviewScheduler(PreviewScheduler* scheduler) { scheduler_ = scheduler; }
    PreviewScheduler* previewScheduler() const { return scheduler_; }
    // A preview for the current state is being computed.
    bool previewPending() const { return pendingSerial_ != 0; }
    // A preview from the worker arrives (on the operation's thread). Shows it
    // and returns true, or drops it when stale: for another operation, older
    // than what is shown, from before the preview was reset, or from before a
    // change of anything but the value. A result for a value the user has
    // since left is still shown if it succeeded (the preview keeps up while
    // dragging), but its error is not.
    bool acceptPreview(const PreviewOutcome& outcome);
    // Identifies this operation (its clones share it).
    std::uint64_t instance() const { return instance_; }
    // A copy that computes previews on another thread (the preview worker);
    // nullptr keeps this operation's previews synchronous.
    virtual std::unique_ptr<Operation> clone() const { return nullptr; }
    // Commit waits for a pending preview when an automatic choice depends on
    // it (an extrusion that becomes a new body when it misses its body).
    virtual bool commitNeedsPreview() const { return false; }
    // Instruction while the operation needs another pick (e.g. Align's target); "" otherwise.
    virtual std::string prompt() const { return {}; }
    // The value that means "no change" (Esc returns to it): 0 for most, the
    // current diameter when a hole is resized, the current thickness when a
    // push/pull measures it.
    virtual double neutralValue() const { return 0.0; }
    // Typed text starting with + or - changes the value relative to this
    // (push/pull showing the thickness: "+5" = 5 mm thicker); nullopt: typed
    // values are taken as they are.
    virtual std::optional<double> relativeBase() const { return std::nullopt; }
    // Where the value editor goes when there is no arrow or ring (e.g. a
    // circular pattern's angle); nullopt = no value editor.
    virtual std::optional<Vec3> labelAnchor() const { return std::nullopt; }

    // Operations with several handles (Move: one arrow per axis). The active
    // handle receives drags and typed values; value() is its value.
    virtual int handleCount() const { return 1; }
    virtual LinearManipulator handle(int index) const { return index == 0 ? manipulator_ : LinearManipulator(); }
    virtual double handleOffset(int index) const { return index == 0 ? displayOffset(value_) : 0.0; }
    // Axis color for a handle: -1 = accent, 0/1/2 = X/Y/Z.
    virtual int handleAxis(int) const { return -1; }
    // Rotation rings (Rotate). A grabbed ring becomes the active handle and
    // value() is its angle in degrees.
    virtual int ringCount() const { return 0; }
    virtual RingManipulator ring(int /*index*/) const { return {}; }
    int activeHandle() const { return activeHandle_; }
    virtual void setActiveHandle(int index) { activeHandle_ = index; }

    virtual std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const;

    // Where the arrow sits for the current value.
    Vec3 anchor() const { return manipulator_.anchor(displayOffset(value_)); }
    // Maps the operation value to a distance along the manipulator axis.
    virtual double displayOffset(double value) const { return value; }
    virtual double valueFromOffset(double offset) const { return offset; }

protected:
    Operation(const Operation&) = default;
    // canCommit()'s preview condition: a preview without error, or one pending.
    bool previewUsable() const { return previewPending() || (error_.empty() && hasPreview()); }
    // Takes the automatic choices a clone of this operation made while it
    // computed the preview that is now shown.
    virtual void adoptAutomaticChoices(const Operation& /*from*/) {}
    virtual std::unique_ptr<doc::Feature> makeFeature(double value) const = 0;
    // The previewed result for `value`: by default makeFeature evaluated on
    // the preview body (Mirror/Pattern with separate bodies show the copies instead).
    virtual Result<geom::Shape> computePreview(double value, const doc::Document& document) const;
    // Changes the stored value without recomputing the preview.
    void setStoredValue(double value) { value_ = value; }
    // Drops the preview and error (e.g. when a needed pick is undone).
    void clearPreview()
    {
        dropPreviews();
        error_.clear();
    }
    // Whether the neutral value means "no change" (no preview). Align previews at 0.
    virtual bool neutralIsIdentity() const { return true; }
    // Operations whose preview is not a body's shape (construction axes and
    // planes) work it out here instead of computePreview: "" when it could be
    // shown, else why not (the error).
    virtual bool previewsShape() const { return true; }
    virtual std::string refreshPreview(double /*value*/, const doc::Document& /*document*/) { return {}; }
    // Why `value` cannot be previewed at all (e.g. a thickness of zero); "" = fine.
    virtual std::string checkValue(double /*value*/) const { return {}; }
    // Automatic choices (e.g. join vs. new body) start over for every value...
    virtual void resetAutomaticChoices() {}
    // ...and may be revised once the preview result is known; returning true
    // recomputes the preview with the revised choice.
    virtual bool reconsider(const geom::Shape& /*result*/, const doc::Document& /*document*/) { return false; }
    // ...or once the preview was refused (e.g. an automatic cut that misses
    // the body: ErrorCode::NoEffect).
    virtual bool reconsiderRefusal(ErrorCode /*code*/) { return false; }

private:
    // The preview for `value`, automatic choices revised (on this object).
    PreviewOutcome computeOutcome(double value, const doc::Document& document);
    void showOutcome(const PreviewOutcome& outcome);
    // No preview, and none of the requests so far will be shown.
    void dropPreviews();

    Uuid bodyId_;
    LinearManipulator manipulator_;
    double value_ = 0;
    int activeHandle_ = 0;
    std::shared_ptr<const geom::Mesh> previewMesh_;
    Uuid previewMeshBody_;
    Carry previewCarry_;
    geom::BoundingBox previewBounds_;
    std::uint64_t previewKey_ = 0;
    std::string error_;
    std::uint64_t instance_ = 0;
    PreviewScheduler* scheduler_ = nullptr;
    std::uint64_t pendingSerial_ = 0;  // the request whose preview is awaited (0: none)
    std::uint64_t resolvedSerial_ = 0; // the newest request whose result was applied
    std::uint64_t floorSerial_ = 0;    // results of older requests are stale (a reset, a parameter change)
};

// While one exists (on this thread), new operations use `scheduler` for
// their previews, including those computed as they are created (Pattern,
// the heat-set insert).
class PreviewSchedulerScope {
public:
    explicit PreviewSchedulerScope(PreviewScheduler* scheduler);
    ~PreviewSchedulerScope();
    PreviewSchedulerScope(const PreviewSchedulerScope&) = delete;
    PreviewSchedulerScope& operator=(const PreviewSchedulerScope&) = delete;

private:
    PreviewScheduler* previous_;
};

// The origin's axes, planes and point, as targets (Align) and references.
enum class OriginTarget { XAxis, YAxis, ZAxis, XYPlane, XZPlane, YZPlane, Point };

// Align: moves a body so one of its faces or edges (the source) meets a face
// or edge of another body (the target) — flat faces touching, edges collinear,
// circles/holes/shafts concentric. The target can also be an origin axis, an
// origin plane or the origin itself (and construction axes and planes):
// endless, so the source lands at its nearest point on them. Waits for the
// target after creation; then value() is an offset along the target (the
// arrow) and Flip reverses it. Commits as a Move step with a rotation, named
// "Align".
class AlignOperation final : public Operation {
public:
    static std::unique_ptr<AlignOperation> create(const doc::Document& document, const Uuid& bodyId,
                                                  geom::SubShapeKind kind, int index);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new AlignOperation(*this)); }
    std::string title() const override { return "Align"; }
    std::string valueLabel() const override { return "Offset"; }
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Move; }
    std::string prompt() const override;
    bool canCommit() const override { return target_.has_value() && previewUsable(); }

    bool hasTarget() const { return target_.has_value(); }
    // Picks the target on another body; recomputes the preview.
    Status setTarget(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index);
    // The ground (XY plane): lays a flat source face down on it where it is.
    Status setGroundTarget(const doc::Document& document);
    // A construction axis or plane: endless, like the origin's.
    Status setDatumTarget(const doc::Datum& datum, const doc::Document& document);
    // The construction axis or plane aimed at (nil otherwise).
    const Uuid& datumTarget() const { return datumTarget_; }
    // An origin axis (the source becomes parallel to it, its point on it), an
    // origin plane (a flat face touches it from the side the body is on; Flip
    // turns it over) or the origin (the source's point moves there, no turn).
    Status setOriginTarget(OriginTarget target, const doc::Document& document);
    void clearTarget();
    bool flipped() const { return flip_; }
    void setFlipped(bool flip, const doc::Document& document);
    // A flat source face can be laid onto the ground plane.
    bool canUseGround() const { return source_.sided; }
    bool targetIsGround() const { return targetKindOf_ == TargetOf::Ground; }
    std::optional<OriginTarget> originTarget() const
    {
        return targetKindOf_ == TargetOf::Origin ? std::optional<OriginTarget>(origin_) : std::nullopt;
    }

    // Target body and sub-shape, for highlighting (nil body for the ground).
    const Uuid& targetBody() const { return targetBody_; }
    geom::SubShapeKind targetKind() const { return targetKind_; }
    int targetIndex() const { return targetIndex_; }

    int handleCount() const override { return target_ ? 1 : 0; }
    LinearManipulator handle(int index) const override;
    double handleOffset(int index) const override { return index == 0 ? value() : 0.0; }
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;
    // The arrow slides along the target; the body goes where the preview has it.
    Carry carriedSelection() const override { return {{}, true}; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    bool neutralIsIdentity() const override { return false; }

private:
    AlignOperation(Uuid bodyId, geom::AlignFrame source, Vec3 bodyCenter)
        : Operation(bodyId, LinearManipulator(source.point, source.direction)), source_(source), bodyCenter_(bodyCenter) {}
    enum class TargetOf { None, Body, Ground, Origin, Datum };
    // An endless line or plane through `point` along/across `direction`, the
    // arrow placed where the source lands. Planes face the side the body is on.
    void setEndlessTarget(Vec3 point, Vec3 direction, bool plane, const doc::Document& document);
    void forgetTarget();
    geom::AlignFrame source_;
    Vec3 bodyCenter_;
    std::optional<geom::AlignFrame> target_;
    TargetOf targetKindOf_ = TargetOf::None;
    Uuid targetBody_;
    geom::SubShapeKind targetKind_ = geom::SubShapeKind::Whole;
    int targetIndex_ = -1;
    OriginTarget origin_ = OriginTarget::Point;
    Uuid datumTarget_;
    bool flip_ = false;
};

// Push/pull of one planar face along its normal. When a parallel flat face
// lies straight behind it, the value is the part's thickness between the two
// (a cube's top face shows its height: type 35 to make it 35 mm tall);
// otherwise it is the distance the face moves.
class PushPullOperation final : public Operation {
public:
    static std::unique_ptr<PushPullOperation> create(const doc::Document& document, const Uuid& bodyId, int faceIndex);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new PushPullOperation(*this)); }
    std::string title() const override { return "Push/Pull"; }
    std::string valueLabel() const override { return thickness_ ? thicknessLabel_ : "Distance"; }
    bool allowsNegative() const override { return !thickness_; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::PushPull; }
    int faceIndex() const { return face_.indexHint; }

    // The measured thickness (at the arrow), if the face has a parallel opposite.
    const std::optional<geom::FaceThickness>& thickness() const { return thickness_; }
    double neutralValue() const override { return thickness_ ? thickness_->distance : 0.0; }
    std::optional<double> relativeBase() const override
    {
        return thickness_ ? std::optional<double>(thickness_->distance) : std::nullopt;
    }
    bool canCommit() const override { return std::abs(value() - neutralValue()) > 1e-9 && previewUsable(); }
    double displayOffset(double value) const override { return value - neutralValue(); }
    double valueFromOffset(double offset) const override { return neutralValue() + offset; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    std::string checkValue(double value) const override;

private:
    PushPullOperation(Uuid bodyId, LinearManipulator m, doc::FaceRef face)
        : Operation(bodyId, std::move(m)), face_(std::move(face)) {}
    doc::FaceRef face_;
    std::optional<geom::FaceThickness> thickness_;
    std::string thicknessLabel_; // "Height", "Width", "Depth" or "Thickness"
};

// Fillet or chamfer on a set of edges of one body. The handle starts at the
// first edge and points into the material; dragging inward grows the size.
class EdgeOperation final : public Operation {
public:
    static std::unique_ptr<EdgeOperation> create(const doc::Document& document, const Uuid& bodyId,
                                                 const std::vector<int>& edgeIndices, doc::FeatureKind kind);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new EdgeOperation(*this)); }
    std::string title() const override { return kind_ == doc::FeatureKind::Fillet ? "Fillet" : "Chamfer"; }
    std::string valueLabel() const override { return kind_ == doc::FeatureKind::Fillet ? "Radius" : "Distance"; }
    bool allowsNegative() const override { return false; }
    doc::FeatureKind featureKind() const override { return kind_; }

    // The fillet surface sits ~0.3r from the original edge for 90-degree
    // edges; moving the handle at that rate keeps it near the geometry.
    double displayOffset(double value) const override { return value * kHandleRatio; }
    double valueFromOffset(double offset) const override { return offset / kHandleRatio; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    static constexpr double kHandleRatio = 0.5;
    EdgeOperation(Uuid bodyId, LinearManipulator m, std::vector<doc::EdgeRef> edges, doc::FeatureKind kind)
        : Operation(bodyId, std::move(m)), edges_(std::move(edges)), kind_(kind) {}
    std::vector<doc::EdgeRef> edges_;
    doc::FeatureKind kind_;
};

// Offset face: moves one face along its normal with its neighbours following.
// Round faces (holes, shafts, bosses) take the new diameter; other faces a
// distance (positive = the body grows).
class OffsetFaceOperation final : public Operation {
public:
    static std::unique_ptr<OffsetFaceOperation> create(const doc::Document& document, const Uuid& bodyId, int faceIndex);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new OffsetFaceOperation(*this)); }
    std::string title() const override { return "Offset"; }
    std::string valueLabel() const override { return round_ ? "Diameter" : "Offset"; }
    bool allowsNegative() const override { return !round_; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::OffsetFace; }
    double neutralValue() const override { return round_ ? diameter_ : 0.0; }
    bool canCommit() const override { return std::abs(value() - neutralValue()) > 1e-9 && previewUsable(); }
    double displayOffset(double value) const override { return round_ ? (value - diameter_) / 2 : value; }
    double valueFromOffset(double offset) const override { return round_ ? diameter_ + 2 * offset : offset; }
    bool round() const { return round_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    OffsetFaceOperation(Uuid bodyId, LinearManipulator m, doc::FaceRef face)
        : Operation(bodyId, std::move(m)), face_(std::move(face)) {}
    doc::FaceRef face_;
    bool round_ = false;
    double diameter_ = 0; // round faces: the current diameter
    double outward_ = 1;  // +1 when growing the diameter adds material (a boss), -1 for a hole
};

// Mirror: keeps the body and joins its mirror image. The plane comes from a
// clicked flat face (on any body) or an origin plane (across YZ, XZ or XY).
// An image that would not touch the body becomes a separate, independent
// body instead (as in Shapr3D); the "Separate bodies" toggle overrides that.
class MirrorOperation final : public Operation {
public:
    static std::unique_ptr<MirrorOperation> create(const doc::Document& document, const Uuid& bodyId);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new MirrorOperation(*this)); }
    std::string title() const override { return "Mirror"; }
    std::string valueLabel() const override { return {}; }
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Mirror; }
    std::string prompt() const override;
    bool canCommit() const override { return plane_.has_value() && previewUsable(); }
    int handleCount() const override { return 0; }
    // The body and its image, as the preview shows them.
    Carry carriedSelection() const override { return {{}, true}; }

    bool hasPlane() const { return plane_.has_value(); }
    // -1 when the plane came from a face.
    int originPlane() const { return originAxis_; }
    Status setPlaneFromFace(const doc::Document& document, const Uuid& bodyId, int faceIndex);
    // Across any plane (a construction plane).
    void setPlane(const Vec3& origin, const Vec3& normal, const doc::Document& document);
    // normalAxis 0: across YZ (flips X), 1: across XZ (flips Y), 2: across XY (flips Z).
    void setOriginPlane(int normalAxis, const doc::Document& document);
    // "Separate bodies": the mirror image becomes an independent body (a copy
    // of this body's history ending in a Mirror step that keeps only the
    // image) instead of joining it. Chosen automatically when the image would
    // not touch the body; setSeparate is the user's choice and wins.
    bool separate() const { return separateChoice_.value_or(autoSeparate_); }
    // Separate because the image would not touch the body (not chosen).
    bool separateIsAutomatic() const { return !separateChoice_ && autoSeparate_; }
    void setSeparate(bool separate, const doc::Document& document);
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;
    // The automatic choice (separate or joined) comes from the preview.
    bool commitNeedsPreview() const override { return plane_.has_value() && !separateChoice_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    Result<geom::Shape> computePreview(double value, const doc::Document& document) const override;
    bool neutralIsIdentity() const override { return false; }
    void resetAutomaticChoices() override { autoSeparate_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;
    // The preview worker decides on a clone: take its choice with its preview.
    void adoptAutomaticChoices(const Operation& from) override
    {
        if (const auto* other = dynamic_cast<const MirrorOperation*>(&from))
            autoSeparate_ = other->autoSeparate_;
    }

private:
    std::vector<std::unique_ptr<doc::Feature>> makeCopySteps() const;
    std::optional<bool> separateChoice_; // the toggle, once used
    bool autoSeparate_ = false;          // the joined image would be a separate piece
    MirrorOperation(Uuid bodyId, const Vec3& center) : Operation(bodyId, LinearManipulator(center, {0, 0, 1})) {}
    struct Plane {
        Vec3 origin, normal;
    };
    std::optional<Plane> plane_;
    int originAxis_ = -1;
};

// Pattern: repeats the body, copies joined. Linear: `count` copies along X/Y/Z
// (or a clicked straight edge), the arrow sets the spacing (it sits on the last
// copy). Circular: copies turn around X/Y/Z through the body center (or a
// clicked hole/shaft/circle), value() = total angle in degrees. Copies that
// would not touch the body become separate, independent bodies instead (as
// in Shapr3D; up to 100 copies); the "Separate bodies" toggle overrides that.
class PatternOperation final : public Operation {
public:
    static std::unique_ptr<PatternOperation> create(const doc::Document& document, const Uuid& bodyId);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new PatternOperation(*this)); }
    std::string title() const override { return "Pattern"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return !circular_; }
    bool isAngle() const override { return circular_; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Pattern; }
    bool canCommit() const override { return count_ >= 2 && value() != 0.0 && previewUsable(); }

    bool circular() const { return circular_; }
    int count() const { return count_; }
    // 0/1/2 = X/Y/Z, -1 = picked edge or axis.
    int axisIndex() const { return axisIndex_; }
    void setCircular(bool circular, const doc::Document& document);
    void setAxisIndex(int axis, const doc::Document& document);
    void setCount(int count, const doc::Document& document);
    // A straight edge (linear direction) or a round face/edge (circular axis).
    Status setAxisFrom(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index);
    // Along / around a line (a construction axis).
    void setAxisLine(const Vec3& point, const Vec3& direction, const doc::Document& document);
    // "Separate bodies": every copy becomes an independent body (a copy of
    // this body's history ending in a Move step) instead of joining it (at
    // most 100 copies). Chosen automatically when the copies would not touch
    // the body; setSeparate is the user's choice and wins.
    bool separate() const { return separateChoice_.value_or(autoSeparate_); }
    // Separate because the copies would not touch the body (not chosen).
    bool separateIsAutomatic() const { return !separateChoice_ && autoSeparate_; }
    void setSeparate(bool separate, const doc::Document& document);
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;
    // The automatic choice (separate or joined) comes from the preview.
    bool commitNeedsPreview() const override { return !separateChoice_; }

    int handleCount() const override { return circular_ ? 0 : 1; }
    LinearManipulator handle(int index) const override;
    double displayOffset(double value) const override { return value * std::max(count_ - 1, 1); }
    double valueFromOffset(double offset) const override { return offset / std::max(count_ - 1, 1); }
    std::optional<Vec3> labelAnchor() const override { return center_; }
    // The last copy where the arrow has it, and all the copies as the preview shows them.
    Carry carriedSelection() const override;

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    Result<geom::Shape> computePreview(double value, const doc::Document& document) const override;
    void resetAutomaticChoices() override { autoSeparate_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;
    // The preview worker decides on a clone: take its choice with its preview.
    void adoptAutomaticChoices(const Operation& from) override
    {
        if (const auto* other = dynamic_cast<const PatternOperation*>(&from))
            autoSeparate_ = other->autoSeparate_;
    }

private:
    std::vector<std::unique_ptr<doc::Feature>> makeCopySteps(double value) const;
    std::optional<bool> separateChoice_; // the toggle, once used
    bool autoSeparate_ = false;          // the joined copies would be separate pieces
    PatternOperation(Uuid bodyId, const Vec3& center, const Vec3& size)
        : Operation(bodyId, LinearManipulator(center, {1, 0, 0})), center_(center), size_(size) {}
    Vec3 axisVectorFor() const;
    double defaultSpacing() const;
    Vec3 center_;
    Vec3 size_;
    bool circular_ = false;
    int axisIndex_ = 0;
    int count_ = 3;
    Vec3 customOrigin_;
    Vec3 customAxis_{0, 0, 1};
};

// Rotate: X/Y/Z rings through the body's center. Drag a ring or type an
// angle (degrees) for the active ring; one axis per step (switching rings
// starts over). A clicked straight edge becomes the axis (one ring around
// it); a clicked corner or circle moves the rings' pivot there. Commits as a
// Move step with a rotation, named "Rotate".
class RotateOperation final : public Operation {
public:
    static std::unique_ptr<RotateOperation> create(const doc::Document& document, const Uuid& bodyId);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new RotateOperation(*this)); }
    std::string title() const override { return "Rotate"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return true; }
    bool isAngle() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Move; }

    int handleCount() const override { return 0; }
    int ringCount() const override { return axis_ ? 1 : 3; }
    RingManipulator ring(int index) const override;
    // Ring colors: X/Y/Z, or the accent for a picked axis along none of them.
    int handleAxis(int index) const override;
    void setActiveHandle(int index) override;
    // The pivot (the rings' center, a point on the axis).
    const Vec3& center() const { return center_; }
    // A picked axis (unit; its largest component positive), if any.
    const std::optional<Vec3>& axis() const { return axis_; }

    // Turn about the line through `point` along `direction` (a picked
    // straight edge). The typed or dragged angle is kept.
    void setAxis(const Vec3& point, const Vec3& direction, const doc::Document& document);
    // X/Y/Z rings through `point` (a picked corner or circle center).
    void setPivot(const Vec3& point, const doc::Document& document);
    // Back to X/Y/Z rings through the body's center.
    void resetPivot(const doc::Document& document);
    bool hasCustomPivot() const { return axis_.has_value() || (center_ - bodyCenter_).length() > 1e-12; }
    // The turned body, as the preview shows it (a long part reaches out of
    // its box and past the rings).
    Carry carriedSelection() const override { return {{}, true}; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double degrees) const override;

private:
    RotateOperation(Uuid bodyId, const Vec3& center)
        : Operation(bodyId, LinearManipulator(center, {0, 0, 1})), center_(center), bodyCenter_(center) {}
    Vec3 center_;
    Vec3 bodyCenter_;
    std::optional<Vec3> axis_;
};

// Move: X/Y/Z arrows at the body's center. Drag any arrow or type a value
// for the active axis; the translation accumulates across axes.
class MoveOperation final : public Operation {
public:
    static std::unique_ptr<MoveOperation> create(const doc::Document& document, const Uuid& bodyId);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new MoveOperation(*this)); }
    std::string title() const override { return "Move"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Move; }
    bool canCommit() const override;

    int handleCount() const override { return 3; }
    LinearManipulator handle(int index) const override;
    double handleOffset(int index) const override;
    int handleAxis(int index) const override { return index; }
    void setActiveHandle(int index) override;
    Vec3 translation() const;
    // The body moved on all axes (an arrow's base already includes the
    // other axes' travel), and as the preview shows it.
    Carry carriedSelection() const override;

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    MoveOperation(Uuid bodyId, const Vec3& center)
        : Operation(bodyId, LinearManipulator(center, {1, 0, 0})), center_(center) {}
    Vec3 center_;
    Vec3 offset_; // committed per-axis values; the active axis lives in value()
};

// Revolve: sweeps profiles around the sketch's Y (or X) axis. value() is in
// degrees. The arrow is tangent to the sweep at the profile, so dragging it
// sweeps the profile around (arc length -> angle).
class RevolveOperation final : public Operation {
public:
    static std::unique_ptr<RevolveOperation> create(const doc::Document& document, const Uuid& sketchId,
                                                    std::vector<doc::ProfileRef> profiles, const Vec3& anchor,
                                                    doc::SketchAxis axis);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new RevolveOperation(*this)); }
    std::string title() const override { return "Revolve"; }
    std::string valueLabel() const override { return "Angle"; }
    bool allowsNegative() const override { return false; }
    bool isAngle() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Revolve; }
    Uuid previewBody() const override;
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;
    double displayOffset(double degrees) const override { return degrees * kPi / 180.0 * radius_; }
    double valueFromOffset(double offset) const override { return std::min(offset / radius_ * 180.0 / kPi, 360.0); }

    // The handle rides on the swept arc: it sits where the profile point
    // ends up at the current angle and points along the local tangent.
    LinearManipulator handle(int index) const override;
    // The arrow's travel is along the arc, not a shift of the profile: a new
    // body is kept clear as the preview shows it (a join: the arrow and the
    // profile only).
    Carry carriedSelection() const override { return {{}, previewBody().isNil()}; }

    doc::ExtrudeMode mode() const { return !modeChosen_ && autoNewBody_ ? doc::ExtrudeMode::NewBody : mode_; }
    void setMode(doc::ExtrudeMode mode)
    {
        mode_ = mode;
        modeChosen_ = true;
    }
    bool hasHost() const { return host_.has_value(); }
    doc::SketchAxis axis() const { return axis_; }
    const Uuid& sketchId() const { return sketchId_; }
    const std::vector<doc::ProfileRef>& profiles() const { return profiles_; }
    bool commitNeedsPreview() const override { return host_.has_value() && !modeChosen_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    void resetAutomaticChoices() override { autoNewBody_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;
    void adoptAutomaticChoices(const Operation& from) override
    {
        if (const auto* other = dynamic_cast<const RevolveOperation*>(&from))
            autoNewBody_ = other->autoNewBody_;
    }

private:
    RevolveOperation(Uuid sketchId, std::optional<Uuid> host, LinearManipulator m, std::vector<doc::ProfileRef> profiles,
                     doc::SketchAxis axis, double radius, Vec3 axisOrigin, Vec3 axisDirection, Vec3 start)
        : Operation(host.value_or(Uuid()), std::move(m)), sketchId_(sketchId), host_(host), profiles_(std::move(profiles)),
          axis_(axis), radius_(radius), mode_(host ? doc::ExtrudeMode::Join : doc::ExtrudeMode::NewBody),
          axisOrigin_(axisOrigin), axisDirection_(axisDirection), start_(start) {}
    Uuid sketchId_;
    std::optional<Uuid> host_;
    std::vector<doc::ProfileRef> profiles_;
    doc::SketchAxis axis_;
    double radius_;
    doc::ExtrudeMode mode_;
    bool modeChosen_ = false;  // the user picked New body / Join / Cut
    bool autoNewBody_ = false; // a join that would not touch the body becomes a new body
    Vec3 axisOrigin_;
    Vec3 axisDirection_;
    Vec3 start_; // profile point the handle starts on
};

// Heat-set insert: drills a preset pilot hole at a circular rim. The arrow
// points into the material; its value is the depth. Presets set both the
// diameter and a typical depth.
class InsertOperation final : public Operation {
public:
    static std::unique_ptr<InsertOperation> create(const doc::Document& document, const Uuid& bodyId, int rimEdge,
                                                   std::size_t presetIndex);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new InsertOperation(*this)); }
    std::string title() const override { return "Heat-set insert " + preset().name; }
    std::string valueLabel() const override { return "Depth"; }
    bool allowsNegative() const override { return false; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Hole; }

    std::size_t presetIndex() const { return presetIndex_; }
    void setPreset(std::size_t index, const doc::Document& document);
    double diameter() const { return diameter_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    struct Preset {
        std::string name;
        double diameter, depth;
    };
    Preset preset() const;
    InsertOperation(Uuid bodyId, LinearManipulator m, doc::EdgeRef rim, std::size_t presetIndex)
        : Operation(bodyId, std::move(m)), rim_(std::move(rim)), presetIndex_(presetIndex) {}
    doc::EdgeRef rim_;
    std::size_t presetIndex_;
    double diameter_ = 0;
};

// Counterbore or countersink for a screw head on an existing round hole (at
// its rim). Screw presets (M2-M6, doc::metricScrews) set the sizes, their
// diameters plus the print allowance (doc::kDefaultHoleAllowance); the
// radial arrow (handle 0) sets the diameter, a counterbore's arrow into the
// hole (handle 1) its depth. Typed values go to the active arrow.
class HeadOperation final : public Operation {
public:
    static std::unique_ptr<HeadOperation> create(const doc::Document& document, const Uuid& bodyId, int rimEdge,
                                                 doc::HoleKind kind, std::size_t presetIndex,
                                                 double allowance = doc::kDefaultHoleAllowance);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new HeadOperation(*this)); }
    std::string title() const override;
    std::string valueLabel() const override { return activeHandle() == 1 ? "Depth" : "Diameter"; }
    bool allowsNegative() const override { return false; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Hole; }
    doc::HoleKind holeKind() const { return kind_; }

    int handleCount() const override { return kind_ == doc::HoleKind::Counterbore ? 2 : 1; }
    LinearManipulator handle(int index) const override;
    double handleOffset(int index) const override;
    void setActiveHandle(int index) override;
    double displayOffset(double value) const override { return activeHandle() == 1 ? value : value / 2; }
    double valueFromOffset(double offset) const override { return activeHandle() == 1 ? offset : 2 * offset; }
    // The depth arrow takes the rim down; the diameter arrow widens it around
    // its center (the rim and the arrow are kept clear), it does not shift it.
    Carry carriedSelection() const override { return activeHandle() == 1 ? Operation::carriedSelection() : Carry{}; }

    // The screw preset the sizes came from (none once a size is typed or dragged).
    std::optional<std::size_t> presetIndex() const;
    void setPreset(std::size_t index, const doc::Document& document);
    double diameter() const { return activeHandle() == 1 ? diameter_ : value(); }
    double depth() const { return activeHandle() == 1 ? value() : depth_; }
    // The print allowance changed (Preferences): sizes from a preset follow it.
    void setAllowance(double allowance, const doc::Document& document);
    // The preset's head diameter, allowance included.
    double presetDiameter(std::size_t index) const;

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    HeadOperation(Uuid bodyId, LinearManipulator m, doc::EdgeRef rim, doc::HolePlacement placement, doc::HoleKind kind)
        : Operation(bodyId, std::move(m)), rim_(std::move(rim)), placement_(placement), kind_(kind) {}
    double allowance_ = doc::kDefaultHoleAllowance;
    doc::EdgeRef rim_;
    doc::HolePlacement placement_;
    doc::HoleKind kind_;
    Vec3 radial_; // the diameter arrow's direction
    std::size_t presetIndex_ = doc::kDefaultScrew;
    double diameter_ = 0; // the value of whichever handle is not active
    double depth_ = 0;
};

// What the Hole tool remembers between uses (a new face starts with these).
struct HoleSettings {
    std::size_t screw = doc::kDefaultScrew;   // doc::metricScrews() row
    doc::HoleFit fit = doc::HoleFit::Normal;  // clearance (ISO 273) or tap drill
    bool throughAll = true;
    double depth = 10;                        // mm, when not through all
    doc::HoleKind head = doc::HoleKind::Plain; // Plain = no counterbore / countersink
    // The print allowance (a preference, set by the controller): added to
    // clearance and head diameters from presets, not to tap drills.
    double allowance = doc::kDefaultHoleAllowance;
};

// The Hole tool: round holes drilled into one flat face where the user
// clicks or taps (each click adds one; all of them are one step). Clicks
// snap to the face's center (of its outline's bounding rectangle) and the
// middles of its straight edges, and otherwise line up (in X or Y) with
// those, with circles on the face and with the holes placed so far; a snap
// that would put the hole off the face is not taken. The value chip edits
// one field at a time: the diameter (screw size x fit presets), the depth,
// or the current hole's X / Y offset from the face's reference corner (the
// outline's minimum corner in the face frame) or from the hole placed before
// it. Clicking a placed hole makes it the current one; Remove hole drops it.
class HoleOperation final : public Operation {
public:
    enum class Field { Diameter, Depth, X, Y };
    static std::unique_ptr<HoleOperation> create(const doc::Document& document, const Uuid& bodyId, int faceIndex,
                                                 const HoleSettings& settings);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new HoleOperation(*this)); }
    std::string title() const override { return "Hole"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return field_ == Field::X || field_ == Field::Y; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Holes; }
    std::string prompt() const override;
    bool canCommit() const override { return !positions_.empty() && previewUsable(); }
    // Apply waits for the preview: it says which hole is off the face (the
    // step's own refusal is worded for upstream changes).
    bool commitNeedsPreview() const override { return true; }
    // Esc leaves the tool (there is no value to fall back to).
    double neutralValue() const override { return value(); }
    int handleCount() const override { return 0; }
    std::optional<Vec3> labelAnchor() const override;

    int faceIndex() const { return face_.indexHint; }
    const doc::HoleFrame& frame() const { return frame_; }
    const std::vector<Vec2>& positions() const { return positions_; }
    int current() const { return current_; }
    Field field() const { return field_; }
    const HoleSettings& settings() const { return settings_; }
    double diameter() const { return field_ == Field::Diameter ? value() : diameter_; }
    // The point a click at `world` (on the face) would use, and what it
    // snapped to ("center", "midpoint", "aligned" or "").
    std::pair<Vec2, std::string> snap(const Vec3& world, double snapDistance) const;
    // Adds a hole at the snapped point, or makes a placed hole under the
    // point the current one. Returns what it snapped to.
    std::string placeAt(const Vec3& world, double snapDistance, const doc::Document& document);
    // Hover feedback: where a click would place the next hole (nullopt: none).
    void setHover(std::optional<Vec2> point) { hover_ = point; }
    const std::optional<Vec2>& hover() const { return hover_; }
    // Where the current hole's X / Y are measured from (face frame).
    Vec2 reference() const;

    void setField(Field field, const doc::Document& document);
    void nextField(const doc::Document& document);
    void setScrew(std::size_t index, const doc::Document& document);
    void setFit(doc::HoleFit fit, const doc::Document& document);
    // The print allowance changed (Preferences): a preset diameter follows it.
    void setAllowance(double allowance, const doc::Document& document);
    // The diameter the screw size and fit give (the presets, allowance included).
    double presetDiameter() const;
    // The counterbore / countersink diameter the screw size gives (allowance included).
    double headDiameter() const;
    void setThroughAll(bool throughAll, const doc::Document& document);
    void setHead(doc::HoleKind head, const doc::Document& document);
    bool fromLastHole() const { return fromLastHole_; }
    void setFromLastHole(bool on, const doc::Document& document);
    // Removes the current hole; the one placed before it (or the next) becomes current.
    void removeCurrent(const doc::Document& document);

    // The holes' positions with the active field's `value` applied.
    std::vector<Vec2> livePositions(double value) const;

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    // Before the first hole: the body as it is (no error, nothing to apply
    // yet). A hole off the face (typed there) fails with a message.
    Result<geom::Shape> computePreview(double value, const doc::Document& document) const override;
    bool neutralIsIdentity() const override { return false; }

private:
    HoleOperation(Uuid bodyId, doc::FaceRef face, doc::HoleFrame frame)
        : Operation(bodyId, LinearManipulator(frame.origin, frame.normal)), face_(std::move(face)), frame_(frame) {}
    // The active field's value written to where it belongs.
    void storeValue();
    double fieldValue(Field field) const;
    // Applies the screw preset to the diameter (and the head sizes).
    void applyPreset();
    // Whether a hole centered at `p` lies on the face: exactly (the kernel;
    // the preview's check, on the worker) or by the face's outline (snapping,
    // on the GUI thread while hovering: no kernel call, so no wait for the
    // worker's).
    bool onFace(Vec2 p) const;
    bool onOutline(Vec2 p) const;
    geom::Shape shape_; // the body as the tool started (previews never change it)
    doc::FaceRef face_;
    doc::HoleFrame frame_;
    geom::FaceOutline outline_;
    Vec3 facePoint_; // a point on the face (where the value chip sits before the first hole)
    HoleSettings settings_;
    Field field_ = Field::Diameter;
    double diameter_ = 3.4; // the fields that are not active
    double depth_ = 10;
    std::vector<Vec2> positions_;
    int current_ = -1;
    bool fromLastHole_ = false;
    std::optional<Vec2> hover_;
};

// What the Text tool remembers between uses (a new face starts with these).
struct TextSettings {
    std::string text;         // the last text typed
    double size = 10;         // mm: the height of capital letters
    double depth = 1;         // mm: > 0 raised (emboss), < 0 cut in (deboss)
    double angleDegrees = 0;  // counter-clockwise, seen from outside the face
    bool bold = false;        // Noto Sans Bold instead of Regular
};

// The Text tool: one line of text raised from or cut into a flat face (one
// Text step). The text starts at the face's center; a click or tap on the
// face moves it there, snapping like the Hole tool (the face's center and
// the middles of its straight edges, else lined up with them). The arrow at
// the text's center sets the depth: outward raises the letters (emboss),
// inward cuts them in (deboss). The value chip edits one field at a time:
// the depth, the size (the capital height) or the angle; the text itself is
// typed in the chip's text field (setText).
class TextOperation final : public Operation {
public:
    enum class Field { Depth, Size, Angle };
    static std::unique_ptr<TextOperation> create(const doc::Document& document, const Uuid& bodyId, int faceIndex,
                                                 const TextSettings& settings);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new TextOperation(*this)); }
    std::string title() const override { return "Text"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return field_ != Field::Size; }
    bool isAngle() const override { return field_ == Field::Angle; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Text; }
    std::string prompt() const override;
    bool canCommit() const override;
    // Apply waits for the preview: its refusal is worded for what is being
    // done ("The text's center is off the face"; the step's own is worded
    // for upstream changes).
    bool commitNeedsPreview() const override { return true; }
    // Esc leaves the tool (there is no value to fall back to).
    double neutralValue() const override { return value(); }
    LinearManipulator handle(int index) const override;
    double handleOffset(int index) const override { return index == 0 ? depth() : 0.0; }
    // Grabbing the arrow edits the depth.
    void setActiveHandle(int index) override;

    int faceIndex() const { return face_.indexHint; }
    const doc::HoleFrame& frame() const { return frame_; }
    Vec2 position() const { return position_; }
    Vec3 center() const { return frame_.toWorld(position_); }
    Field field() const { return field_; }
    double depth() const { return field_ == Field::Depth ? value() : depth_; }
    double size() const { return field_ == Field::Size ? value() : size_; }
    double angleDegrees() const { return field_ == Field::Angle ? value() : angleDegrees_; }
    const std::string& text() const { return text_; }
    bool bold() const { return bold_; }
    // The settings to remember (with the fields' current values; for a value
    // refused in the active field, its last accepted one).
    TextSettings settings() const;
    // Whether the user did anything in this use of the tool (typed, placed,
    // changed a value or option). A stray click elsewhere applies only then:
    // remembered text alone is applied by Enter or Apply, not by a tap that
    // was meant to leave the tool.
    bool edited() const;
    void markEdited() { edited_ = true; }
    // Whether the words were typed (or erased, or confirmed) in this use of
    // the tool. Until then they are the remembered ones, which the first key
    // typed replaces (they start selected), also after a click placed them.
    bool wordsTyped() const { return wordsTyped_; }

    void setText(const std::string& text, const doc::Document& document);
    void setField(Field field, const doc::Document& document);
    void nextField(const doc::Document& document);
    void setAngleDegrees(double degrees, const doc::Document& document);
    // Emboss (raised) or deboss (cut in): the depth's sign.
    void setRaised(bool raised, const doc::Document& document);
    void setBold(bool bold, const doc::Document& document);
    // The point a click at `world` (on the face) would use, and what it
    // snapped to ("center", "midpoint", "aligned" or "").
    std::pair<Vec2, std::string> snap(const Vec3& world, double snapDistance) const;
    // Moves the text to the snapped point; returns what it snapped to.
    std::string placeAt(const Vec3& world, double snapDistance, const doc::Document& document);
    void setHover(std::optional<Vec2> point) { hover_ = point; }
    const std::optional<Vec2>& hover() const { return hover_; }
    // The corners of the letters' box on the face (world), so the value chip
    // can sit beside the text rather than on it; empty while nothing is typed.
    // No kernel call (the GUI thread asks while the worker computes): the
    // letters' extent comes with the preview, so while a preview for new
    // words computes, the box is the one of the words shown.
    std::vector<Vec3> textCorners() const;

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    // Nothing typed yet: the body as it is (no error). The center off the
    // face (a click there is not taken, but an upstream change could): says so.
    Result<geom::Shape> computePreview(double value, const doc::Document& document) const override;
    bool neutralIsIdentity() const override { return false; }
    std::string checkValue(double value) const override;
    // The letters' extent the worker's copy measured with the preview shown.
    void adoptAutomaticChoices(const Operation& from) override;

private:
    TextOperation(Uuid bodyId, doc::FaceRef face, doc::HoleFrame frame)
        : Operation(bodyId, LinearManipulator(frame.origin, frame.normal)), face_(std::move(face)), frame_(frame) {}
    TextOperation(const TextOperation&) = default;
    // The active field's value written to where it belongs (only an
    // accepted one: a refused value is never carried into later previews).
    void storeValue();
    double fieldValue(Field field) const;
    // Whether the text's center `p` lies on the face: exactly (the kernel;
    // the preview's check, on the worker) or by the face's outline (snapping
    // and placing, on the GUI thread: no kernel call, so no wait for the
    // worker's preview).
    bool onFace(Vec2 p) const;
    bool onOutline(Vec2 p) const;
    // Measures the letters' extent for textCorners when the words, size or
    // font changed (the kernel: with the preview, on the worker).
    void measureExtent() const;
    geom::Shape shape_; // the body as the tool started (previews never change it)
    doc::FaceRef face_;
    doc::HoleFrame frame_;
    geom::FaceOutline outline_;
    Vec2 position_;
    std::string text_;
    Field field_ = Field::Depth;
    double depth_ = 1, size_ = 10, angleDegrees_ = 0; // the fields that are not active
    bool bold_ = false;
    bool edited_ = false;
    bool wordsTyped_ = false;
    TextSettings initial_; // as the tool opened (edited() compares)
    std::optional<Vec2> hover_;
    // The letters' extent for textCorners (in the text's own frame), made
    // again only when the words, size or font change (measureExtent).
    struct Extent {
        std::string text;
        double size = 0;
        bool bold = false;
        bool valid = false;
        double minX = 0, maxX = 0, minY = 0, maxY = 0;
    };
    mutable Extent extent_;
};

// Shell: hollows the body through the selected faces. The arrow starts on the
// first face and points into the material; its length is the wall thickness.
class ShellOperation final : public Operation {
public:
    static std::unique_ptr<ShellOperation> create(const doc::Document& document, const Uuid& bodyId,
                                                  const std::vector<int>& faceIndices);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new ShellOperation(*this)); }
    std::string title() const override { return "Shell"; }
    std::string valueLabel() const override { return "Wall"; }
    bool allowsNegative() const override { return false; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Shell; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    ShellOperation(Uuid bodyId, LinearManipulator m, std::vector<doc::FaceRef> faces)
        : Operation(bodyId, std::move(m)), faces_(std::move(faces)) {}
    std::vector<doc::FaceRef> faces_;
};

// Extrudes selected sketch profiles along the sketch normal. Without an
// explicit mode it creates a new body, or - for sketches placed on a body -
// joins when pulled outward and cuts when pushed inward.
class ExtrudeOperation final : public Operation {
public:
    static std::unique_ptr<ExtrudeOperation> create(const doc::Document& document, const Uuid& sketchId,
                                                    std::vector<doc::ProfileRef> profiles, const Vec3& anchor);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new ExtrudeOperation(*this)); }
    std::string title() const override { return "Extrude"; }
    // Symmetric: the value is the total thickness, centered on the sketch.
    std::string valueLabel() const override
    {
        return editingDraft() ? "Draft" : symmetric_ ? "Thickness" : "Distance";
    }
    bool allowsNegative() const override { return editingDraft() || !symmetric_; }
    bool isAngle() const override { return editingDraft(); }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Extrude; }
    // The draft is a second field of the value chip (Draft action; no arrow
    // of its own): value() is then the angle in degrees, positive narrowing
    // away from the sketch. Grabbing the arrow goes back to the distance.
    bool editingDraft() const { return activeHandle() == 1; }
    void setActiveHandle(int index) override;
    double distance() const { return editingDraft() ? distance_ : value(); }
    double draftDegrees() const { return editingDraft() ? value() : draftDegrees_; }
    LinearManipulator handle(int) const override { return manipulator(); }
    double handleOffset(int) const override { return displayOffset(distance()); }
    bool canCommit() const override { return distance() != 0.0 && previewUsable(); }
    Uuid previewBody() const override;
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;
    double displayOffset(double value) const override { return symmetric_ ? value / 2 : value; }
    double valueFromOffset(double offset) const override { return symmetric_ ? 2 * offset : offset; }
    std::string prompt() const override;

    bool symmetric() const { return symmetric_; }
    void setSymmetric(bool symmetric, const doc::Document& document);
    // The profile at the far end (both ends when symmetric), also while the
    // draft is edited; a new body as the preview shows it.
    Carry carriedSelection() const override;
    // "Up to face": the next face click sets the distance (see extendToFace).
    bool pickingTarget() const { return pickingTarget_; }
    void setPickingTarget(bool picking) { pickingTarget_ = picking; }
    // Sets the distance so the extrusion ends on a flat face parallel to the
    // sketch (any body). A one-time measurement, like Align: the step stores
    // the distance, not a link to the face.
    Status extendToFace(const doc::Document& document, const Uuid& bodyId, int faceIndex);

    doc::ExtrudeMode mode() const;
    void setModeOverride(std::optional<doc::ExtrudeMode> mode) { modeOverride_ = mode; }
    const std::optional<doc::ExtrudeMode>& modeOverride() const { return modeOverride_; }
    bool hasHost() const { return host_.has_value(); }
    bool throughAll() const { return throughAll_; }
    void setThroughAll(bool throughAll) { throughAll_ = throughAll; }
    const Uuid& sketchId() const { return sketchId_; }
    bool commitNeedsPreview() const override { return host_.has_value() && !modeOverride_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    void resetAutomaticChoices() override { autoNewBody_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;
    bool reconsiderRefusal(ErrorCode code) override;
    void adoptAutomaticChoices(const Operation& from) override
    {
        if (const auto* other = dynamic_cast<const ExtrudeOperation*>(&from))
            autoNewBody_ = other->autoNewBody_;
    }
    // A draft of 0 still previews the extrusion.
    bool neutralIsIdentity() const override { return !editingDraft(); }

private:
    ExtrudeOperation(Uuid sketchId, std::optional<Uuid> host, LinearManipulator m, std::vector<doc::ProfileRef> profiles)
        : Operation(host.value_or(Uuid()), std::move(m)), sketchId_(sketchId), host_(host), profiles_(std::move(profiles)) {}
    Uuid sketchId_;
    std::optional<Uuid> host_;
    std::vector<doc::ProfileRef> profiles_;
    std::optional<doc::ExtrudeMode> modeOverride_;
    bool autoNewBody_ = false; // an automatic join that would not touch the body becomes a new body
    bool throughAll_ = false;
    bool symmetric_ = false;
    bool pickingTarget_ = false;
    double distance_ = 0;     // while the draft is being edited
    double draftDegrees_ = 0; // while the distance is being edited
};

// Loft: joins closed profiles on different planes (of different sketches),
// in the order they were selected, into a solid. It has no value: the
// preview comes at once, and Smooth / Straight and New body / Join / Cut are
// its options. Like Extrude, a loft with a profile on a body's face (the
// first such sketch's body) joins that body, or becomes a new body when it
// would not touch it; Cut only when chosen. Without such a sketch it is a
// new body.
class LoftOperation final : public Operation {
public:
    // `mode`: the user's choice so far (nullopt: automatic).
    static std::unique_ptr<LoftOperation> create(const doc::Document& document, std::vector<doc::LoftSection> sections,
                                                 bool ruled, std::optional<doc::ExtrudeMode> mode);

    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new LoftOperation(*this)); }
    std::string title() const override { return "Loft"; }
    std::string valueLabel() const override { return {}; }
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Loft; }
    bool canCommit() const override { return previewUsable(); }
    int handleCount() const override { return 0; }
    Uuid previewBody() const override;
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;
    // A new body as the preview shows it (a join: the profiles only).
    Carry carriedSelection() const override { return {{}, previewBody().isNil()}; }

    const std::vector<doc::LoftSection>& sections() const { return sections_; }
    bool ruled() const { return ruled_; }
    void setRuled(bool ruled, const doc::Document& document);
    doc::ExtrudeMode mode() const;
    void setMode(doc::ExtrudeMode mode, const doc::Document& document);
    bool modeChosen() const { return modeChosen_; }
    bool hasHost() const { return host_.has_value(); }
    // The automatic choice (join or new body) comes from the preview.
    bool commitNeedsPreview() const override { return host_.has_value() && !modeChosen_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    bool neutralIsIdentity() const override { return false; }
    void resetAutomaticChoices() override { autoNewBody_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;
    // The preview worker decides on a clone: take its choice with its preview.
    void adoptAutomaticChoices(const Operation& from) override
    {
        if (const auto* other = dynamic_cast<const LoftOperation*>(&from))
            autoNewBody_ = other->autoNewBody_;
    }

private:
    LoftOperation(std::optional<Uuid> host, std::vector<doc::LoftSection> sections, bool ruled, const Vec3& center)
        : Operation(host.value_or(Uuid()), LinearManipulator(center, {0, 0, 1})), host_(host), sections_(std::move(sections)),
          ruled_(ruled) {}
    std::optional<Uuid> host_;
    std::vector<doc::LoftSection> sections_;
    bool ruled_ = false;
    doc::ExtrudeMode mode_ = doc::ExtrudeMode::NewBody; // when chosen
    bool modeChosen_ = false;
    bool autoNewBody_ = false; // an automatic join that would not touch the body becomes a new body
};

// Construct: a construction axis or plane (a datum) from picked geometry.
// Axis: through a hole or shaft (its wall or rim circle) or along a straight
// edge (whichever is clicked), through two points, or parallel to X / Y / Z
// through a point (points: an edge clicked near its end, or a circle for its
// center). Plane: offset from a flat face or an origin plane (value = the
// distance, an arrow along the normal), through a straight edge at an angle
// to a flat face it runs along (value = the angle; the face next to the edge
// is taken until another one is clicked), or midway between two parallel
// flat faces. Commits an AddDatumCommand; nothing touches the document before.
// Its preview is where the datum would be, not a body's shape: the picks are
// resolved when they are made (and again after the document changes), and a
// new distance or angle only recomputes the position without the kernel, so
// it never goes to the preview worker and dragging its arrow is instant.
class DatumOperation final : public Operation {
public:
    enum class Mode { Axis, AxisTwoPoints, AxisParallel, PlaneOffset, PlaneAngle, PlaneMidway };
    static std::unique_ptr<DatumOperation> create(doc::DatumKind kind);

    // Like the others (previewsShape() is false: setValue never schedules it).
    std::unique_ptr<Operation> clone() const override { return std::unique_ptr<Operation>(new DatumOperation(*this)); }

    std::string title() const override { return kind_ == doc::DatumKind::Axis ? "Axis" : "Plane"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return true; }
    bool isAngle() const override { return mode_ == Mode::PlaneAngle; }
    // Not a step (nothing reads it: the controller handles datums first).
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Box; }
    std::string prompt() const override;
    bool canCommit() const override { return preview_.has_value() && error().empty(); }
    double neutralValue() const override { return mode_ == Mode::PlaneAngle ? 45.0 : 0.0; }
    int handleCount() const override;
    LinearManipulator handle(int index) const override;
    double handleOffset(int index) const override { return index == 0 ? value() : 0.0; }
    std::optional<Vec3> labelAnchor() const override;
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;

    doc::DatumKind kind() const { return kind_; }
    Mode mode() const { return mode_; }
    // Starts over with another way of making it (the picks are dropped).
    void setMode(Mode mode, const doc::Document& document);
    // Parallel to X / Y / Z (AxisParallel), or the origin plane an offset
    // plane starts from (PlaneOffset: 0 = YZ, 1 = XZ, 2 = XY), -1 = none.
    int originIndex() const { return originIndex_; }
    void setParallelTo(int axis, const doc::Document& document);
    void setOriginPlane(int normalAxis, const doc::Document& document);
    // A clicked face or edge of a body (`point`: where it was clicked, which
    // picks the end of an edge for a point). Says what is wrong with a pick
    // that does not fit.
    Status pick(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index, const Vec3& point);
    // Drops the last pick (Esc steps back one pick); false when there was none.
    bool dropLastPick(const doc::Document& document);
    bool hasPicks() const { return !refs_.empty() || originIndex_ >= 0; }
    // What was picked, for highlighting (body, face or edge, index).
    struct Picked {
        Uuid body;
        geom::SubShapeKind kind = geom::SubShapeKind::Face;
        int index = -1;
    };
    const std::vector<Picked>& picked() const { return picked_; }
    // The datum as it would be made now, and where it is.
    doc::Datum datum() const;
    const std::optional<doc::DatumGeometry>& preview() const { return preview_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double) const override { return nullptr; }
    bool previewsShape() const override { return false; }
    std::string refreshPreview(double value, const doc::Document& document) override;
    bool neutralIsIdentity() const override { return false; }

private:
    explicit DatumOperation(doc::DatumKind kind) : Operation(Uuid(), LinearManipulator()), kind_(kind) {}
    void startOver(const doc::Document& document);
    // Where the arrow of an offset plane starts (the face or origin plane).
    std::optional<std::pair<Vec3, Vec3>> offsetBase_; // point, normal
    doc::DatumKind kind_;
    Mode mode_ = Mode::Axis;
    int originIndex_ = -1;
    std::vector<doc::GeometryRef> refs_;
    std::vector<Picked> picked_;
    std::optional<doc::DatumGeometry> preview_;
    Vec3 edgeMiddle_; // PlaneAngle: where the angle's value sits
    // refs_ resolved (the kernel work), for the document revision they were
    // resolved in; picks and document changes make them stale.
    std::vector<doc::ResolvedRef> resolved_;
    std::string resolveError_;
    const doc::Document* resolvedIn_ = nullptr;
    std::uint64_t resolvedRevision_ = 0;
    bool resolvedStale_ = true;
};

} // namespace os::interact
