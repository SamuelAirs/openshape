#pragma once

#include "commands/Command.h"
#include "core/Uuid.h"
#include "document/Feature.h"
#include "geometry/Mesh.h"
#include "geometry/Modeling.h"
#include "interaction/Manipulator.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace os::doc {
class Document;
}

namespace os::interact {

// An in-progress, previewable modeling operation driven by a manipulator
// and/or typed values. Nothing touches the document until commit, which
// produces a Command for the undo stack.
class Operation {
public:
    Operation(Uuid bodyId, LinearManipulator manipulator) : bodyId_(bodyId), manipulator_(std::move(manipulator)) {}
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
    // Sets the value and recomputes the preview. Invalid values leave an
    // error message and no preview; the previous document state is untouched.
    void setValue(double value, const doc::Document& document);

    bool hasPreview() const { return previewMesh_ != nullptr; }
    const std::shared_ptr<const geom::Mesh>& previewMesh() const { return previewMesh_; }
    std::uint64_t previewKey() const { return previewKey_; }
    const std::string& error() const { return error_; }
    virtual bool canCommit() const { return value_ != 0.0 && error_.empty() && hasPreview(); }
    // Instruction while the operation needs another pick (e.g. Align's target); "" otherwise.
    virtual std::string prompt() const { return {}; }

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
    virtual std::unique_ptr<doc::Feature> makeFeature(double value) const = 0;
    // Changes the stored value without recomputing the preview.
    void setStoredValue(double value) { value_ = value; }
    // Drops the preview and error (e.g. when a needed pick is undone).
    void clearPreview()
    {
        previewMesh_.reset();
        error_.clear();
    }
    // Whether value 0 means "no change" (no preview). Align previews at 0.
    virtual bool zeroIsIdentity() const { return true; }
    // Automatic choices (e.g. join vs. new body) start over for every value...
    virtual void resetAutomaticChoices() {}
    // ...and may be revised once the preview result is known; returning true
    // recomputes the preview with the revised choice.
    virtual bool reconsider(const geom::Shape& /*result*/, const doc::Document& /*document*/) { return false; }

private:
    Uuid bodyId_;
    LinearManipulator manipulator_;
    double value_ = 0;
    int activeHandle_ = 0;
    std::shared_ptr<const geom::Mesh> previewMesh_;
    std::uint64_t previewKey_ = 0;
    std::string error_;
};

// Align: moves a body so one of its faces or edges (the source) meets a face
// or edge of another body (the target) — flat faces touching, edges collinear,
// circles/holes/shafts concentric. Waits for the target after creation; then
// value() is an offset along the target (the arrow) and Flip reverses it.
// Commits as a Move step with a rotation, named "Align".
class AlignOperation final : public Operation {
public:
    static std::unique_ptr<AlignOperation> create(const doc::Document& document, const Uuid& bodyId,
                                                  geom::SubShapeKind kind, int index);

    std::string title() const override { return "Align"; }
    std::string valueLabel() const override { return "Offset"; }
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Move; }
    std::string prompt() const override;
    bool canCommit() const override { return target_.has_value() && error().empty() && hasPreview(); }

    bool hasTarget() const { return target_.has_value(); }
    // Picks the target on another body; recomputes the preview.
    Status setTarget(const doc::Document& document, const Uuid& bodyId, geom::SubShapeKind kind, int index);
    // The ground (XY plane): lays a flat source face down on it where it is.
    Status setGroundTarget(const doc::Document& document);
    void clearTarget();
    bool flipped() const { return flip_; }
    void setFlipped(bool flip, const doc::Document& document);
    // A flat source face can be laid onto the ground plane.
    bool canUseGround() const { return source_.sided; }
    bool targetIsGround() const { return target_.has_value() && targetBody_.isNil(); }
    // Target body and sub-shape, for highlighting (nil body for the ground).
    const Uuid& targetBody() const { return targetBody_; }
    geom::SubShapeKind targetKind() const { return targetKind_; }
    int targetIndex() const { return targetIndex_; }

    int handleCount() const override { return target_ ? 1 : 0; }
    LinearManipulator handle(int index) const override;
    double handleOffset(int index) const override { return index == 0 ? value() : 0.0; }
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    bool zeroIsIdentity() const override { return false; }

private:
    AlignOperation(Uuid bodyId, geom::AlignFrame source)
        : Operation(bodyId, LinearManipulator(source.point, source.direction)), source_(source) {}
    geom::AlignFrame source_;
    std::optional<geom::AlignFrame> target_;
    Uuid targetBody_;
    geom::SubShapeKind targetKind_ = geom::SubShapeKind::Whole;
    int targetIndex_ = -1;
    bool flip_ = false;
};

// Push/pull of one planar face along its normal.
class PushPullOperation final : public Operation {
public:
    static std::unique_ptr<PushPullOperation> create(const doc::Document& document, const Uuid& bodyId, int faceIndex);

    std::string title() const override { return "Push/Pull"; }
    std::string valueLabel() const override { return "Distance"; }
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::PushPull; }
    int faceIndex() const { return face_.indexHint; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;

private:
    PushPullOperation(Uuid bodyId, LinearManipulator m, doc::FaceRef face)
        : Operation(bodyId, std::move(m)), face_(std::move(face)) {}
    doc::FaceRef face_;
};

// Fillet or chamfer on a set of edges of one body. The handle starts at the
// first edge and points into the material; dragging inward grows the size.
class EdgeOperation final : public Operation {
public:
    static std::unique_ptr<EdgeOperation> create(const doc::Document& document, const Uuid& bodyId,
                                                 const std::vector<int>& edgeIndices, doc::FeatureKind kind);

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

// Rotate: X/Y/Z rings through the body's center. Drag a ring or type an
// angle (degrees) for the active ring; one axis per step (switching rings
// starts over). Commits as a Move step with a rotation, named "Rotate".
class RotateOperation final : public Operation {
public:
    static std::unique_ptr<RotateOperation> create(const doc::Document& document, const Uuid& bodyId);

    std::string title() const override { return "Rotate"; }
    std::string valueLabel() const override;
    bool allowsNegative() const override { return true; }
    bool isAngle() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Move; }

    int handleCount() const override { return 0; }
    int ringCount() const override { return 3; }
    RingManipulator ring(int index) const override;
    int handleAxis(int index) const override { return index; }
    void setActiveHandle(int index) override;
    const Vec3& center() const { return center_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double degrees) const override;

private:
    RotateOperation(Uuid bodyId, const Vec3& center)
        : Operation(bodyId, LinearManipulator(center, {0, 0, 1})), center_(center) {}
    Vec3 center_;
};

// Move: X/Y/Z arrows at the body's center. Drag any arrow or type a value
// for the active axis; the translation accumulates across axes.
class MoveOperation final : public Operation {
public:
    static std::unique_ptr<MoveOperation> create(const doc::Document& document, const Uuid& bodyId);

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

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    void resetAutomaticChoices() override { autoNewBody_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;

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

// Shell: hollows the body through the selected faces. The arrow starts on the
// first face and points into the material; its length is the wall thickness.
class ShellOperation final : public Operation {
public:
    static std::unique_ptr<ShellOperation> create(const doc::Document& document, const Uuid& bodyId,
                                                  const std::vector<int>& faceIndices);

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

    std::string title() const override { return "Extrude"; }
    std::string valueLabel() const override { return "Distance"; }
    bool allowsNegative() const override { return true; }
    doc::FeatureKind featureKind() const override { return doc::FeatureKind::Extrude; }
    Uuid previewBody() const override;
    std::unique_ptr<cmd::Command> makeCommand(const doc::Document& document) const override;

    doc::ExtrudeMode mode() const;
    void setModeOverride(std::optional<doc::ExtrudeMode> mode) { modeOverride_ = mode; }
    const std::optional<doc::ExtrudeMode>& modeOverride() const { return modeOverride_; }
    bool hasHost() const { return host_.has_value(); }
    bool throughAll() const { return throughAll_; }
    void setThroughAll(bool throughAll) { throughAll_ = throughAll; }
    const Uuid& sketchId() const { return sketchId_; }

protected:
    std::unique_ptr<doc::Feature> makeFeature(double value) const override;
    void resetAutomaticChoices() override { autoNewBody_ = false; }
    bool reconsider(const geom::Shape& result, const doc::Document& document) override;

private:
    ExtrudeOperation(Uuid sketchId, std::optional<Uuid> host, LinearManipulator m, std::vector<doc::ProfileRef> profiles)
        : Operation(host.value_or(Uuid()), std::move(m)), sketchId_(sketchId), host_(host), profiles_(std::move(profiles)) {}
    Uuid sketchId_;
    std::optional<Uuid> host_;
    std::vector<doc::ProfileRef> profiles_;
    std::optional<doc::ExtrudeMode> modeOverride_;
    bool autoNewBody_ = false; // an automatic join that would not touch the body becomes a new body
    bool throughAll_ = false;
};

} // namespace os::interact
