#pragma once

#include "commands/Command.h"
#include "core/Uuid.h"
#include "document/Feature.h"
#include "geometry/Mesh.h"
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

    const Uuid& bodyId() const { return bodyId_; }
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
    bool canCommit() const { return value_ != 0.0 && error_.empty() && hasPreview(); }

    std::unique_ptr<cmd::Command> makeCommand() const;

    // Where the arrow sits for the current value.
    Vec3 anchor() const { return manipulator_.anchor(displayOffset(value_)); }
    // Maps the operation value to a distance along the manipulator axis.
    virtual double displayOffset(double value) const { return value; }
    virtual double valueFromOffset(double offset) const { return offset; }

protected:
    virtual std::unique_ptr<doc::Feature> makeFeature(double value) const = 0;

private:
    Uuid bodyId_;
    LinearManipulator manipulator_;
    double value_ = 0;
    std::shared_ptr<const geom::Mesh> previewMesh_;
    std::uint64_t previewKey_ = 0;
    std::string error_;
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

} // namespace os::interact
