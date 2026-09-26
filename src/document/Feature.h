// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "core/Uuid.h"
#include "geometry/Shape.h"
#include "geometry/TopoSignature.h"

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace os::sketch {
class Sketch;
}

namespace os::doc {

class Document;
class Body;

enum class FeatureKind {
    Box, PushPull, Fillet, Chamfer, Extrude, Shell, Move, Combine, Revolve, Hole, Mirror, Pattern, DeleteFaces, OffsetFace,
    Split, SplitPiece, Copy
};

// What a feature may consult besides its input shape.
struct EvalContext {
    const Document* document = nullptr;
    // Set while a body is being recomputed: the body and the index of the
    // feature being evaluated (earlier features' results are current).
    const Body* body = nullptr;
    int featureIndex = -1;
    const sketch::Sketch* sketch(const Uuid& id) const;
};

std::string_view toString(FeatureKind kind);
std::optional<FeatureKind> featureKindFromString(std::string_view text);

// Persistent reference to a face of a feature's *input* shape.
// See docs/TOPOLOGICAL_NAMING.md for why this is a hint plus a signature.
struct FaceRef {
    int indexHint = -1;
    geom::FaceSignature signature;
};

struct EdgeRef {
    int indexHint = -1;
    geom::EdgeSignature signature;
};

enum class ParameterKind { Length, Angle, Count };

// A single editable scalar exposed for the history panel / numeric editing.
struct ParameterInfo {
    std::string key;    // stable identifier used by commands and files
    std::string label;  // user-facing
    ParameterKind kind = ParameterKind::Length;
    double value = 0;   // canonical units (mm / radians)
};

// One step of a body's modeling history. A feature is a pure function of its
// parameters and the body's shape before it (the *input*). Features never
// hold kernel state between evaluations; results are cached by Body.
class Feature {
public:
    explicit Feature(Uuid id = Uuid::generate()) : id_(id) {}
    virtual ~Feature() = default;

    const Uuid& id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    bool isSuppressed() const { return suppressed_; }
    void setSuppressed(bool suppressed) { suppressed_ = suppressed; }

    virtual FeatureKind kind() const = 0;
    virtual std::unique_ptr<Feature> clone() const = 0;

    // True for features that create a body from nothing (input is ignored).
    virtual bool isBaseFeature() const { return false; }

    virtual Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const = 0;

    virtual std::vector<ParameterInfo> parameters() const = 0;
    virtual Status setParameter(std::string_view key, double value) = 0;
    std::optional<double> parameter(std::string_view key) const;

    virtual void writeParams(nlohmann::json& out) const = 0;
    virtual Status readParams(const nlohmann::json& in) = 0;

    // Ids of other document objects this feature depends on (beyond its own
    // body's preceding history): sketches, other bodies, their steps.
    virtual std::vector<Uuid> dependencies() const { return {}; }

    // A copy with a fresh id (duplicating a body: ids stay unique).
    std::unique_ptr<Feature> cloneWithNewId() const;
    // Duplicating a body: references to sketches, bodies and steps that were
    // copied along are pointed at their copies (ids found in `copies`).
    // Every feature with dependencies() must implement it.
    virtual void remapReferences(const std::map<Uuid, Uuid>& /*copies*/) {}

protected:
    Feature(const Feature&) = default;

private:
    Uuid id_;
    std::string name_;
    bool suppressed_ = false;
};

std::unique_ptr<Feature> createFeature(FeatureKind kind, Uuid id);

// ---- Concrete features ------------------------------------------------------

class BoxFeature final : public Feature {
public:
    using Feature::Feature;
    Vec3 origin;
    Vec3 size{20, 20, 20};

    FeatureKind kind() const override { return FeatureKind::Box; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new BoxFeature(*this)); }
    bool isBaseFeature() const override { return true; }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Moves a planar face along its normal (extrude / cut by face).
class PushPullFeature final : public Feature {
public:
    using Feature::Feature;
    FaceRef face;
    double distance = 0; // mm, positive = outward
    // Take the face's rounded and bevelled edges along (fillets keep their
    // size) where possible. Off for steps from older files, which recompute
    // exactly as they did (prism + boolean).
    bool keepEdges = false;

    FeatureKind kind() const override { return FeatureKind::PushPull; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new PushPullFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

class EdgeTreatmentFeature : public Feature {
public:
    using Feature::Feature;
    std::vector<EdgeRef> edges;
    double size = 1; // fillet radius or chamfer distance, mm

    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;

protected:
    Result<std::vector<int>> resolveEdges(const geom::Shape& input) const;
    virtual const char* sizeLabel() const = 0;
    EdgeTreatmentFeature(const EdgeTreatmentFeature&) = default;
};

class FilletFeature final : public EdgeTreatmentFeature {
public:
    using EdgeTreatmentFeature::EdgeTreatmentFeature;
    FeatureKind kind() const override { return FeatureKind::Fillet; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new FilletFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;

private:
    const char* sizeLabel() const override { return "Radius"; }
};

class ChamferFeature final : public EdgeTreatmentFeature {
public:
    using EdgeTreatmentFeature::EdgeTreatmentFeature;
    FeatureKind kind() const override { return FeatureKind::Chamfer; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new ChamferFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;

private:
    const char* sizeLabel() const override { return "Distance"; }
};

enum class CombineMode { Union, Subtract, Intersect };
std::string_view toString(CombineMode mode);

// Boolean with another body's current shape. The tool body is a dependency:
// editing it updates this body.
class CombineFeature final : public Feature {
public:
    using Feature::Feature;
    Uuid toolBody;
    CombineMode mode = CombineMode::Union;

    FeatureKind kind() const override { return FeatureKind::Combine; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new CombineFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
    std::vector<Uuid> dependencies() const override { return {toolBody}; }
    void remapReferences(const std::map<Uuid, Uuid>& copies) override;
};

// Moves the body by a translation (a history step, so it stays editable).
class MoveFeature final : public Feature {
public:
    using Feature::Feature;
    Vec3 translation;
    // Optional rotation, applied before the translation (Rotate and Align
    // steps): about the axis through `rotationCenter` along `rotationAxis`.
    bool rotates = false;
    Vec3 rotationCenter;
    Vec3 rotationAxis{0, 0, 1};
    double rotationAngle = 0; // radians

    geom::RigidMotion motion() const;

    FeatureKind kind() const override { return FeatureKind::Move; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new MoveFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Removes faces (holes, fillets, chamfers, bosses) and heals the gap.
class DeleteFacesFeature final : public Feature {
public:
    using Feature::Feature;
    std::vector<FaceRef> faces;

    FeatureKind kind() const override { return FeatureKind::DeleteFaces; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new DeleteFacesFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Moves one face along its normal (positive = the body grows) with its
// neighbours following; the UI edits round faces by diameter.
class OffsetFaceFeature final : public Feature {
public:
    using Feature::Feature;
    FaceRef face;
    double distance = 0; // mm

    FeatureKind kind() const override { return FeatureKind::OffsetFace; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new OffsetFaceFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Keeps the body and adds its mirror image across a plane, joined into one.
// The plane is stored as geometry (from a picked face or an origin plane).
class MirrorFeature final : public Feature {
public:
    using Feature::Feature;
    Vec3 planeOrigin;
    Vec3 planeNormal{1, 0, 0};

    FeatureKind kind() const override { return FeatureKind::Mirror; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new MirrorFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Repeats the body, copies joined: in a row (`count` in total, `spacing`
// apart along `direction`) or around the axis through `axisOrigin` along
// `axis` (`count` in total over `angle`; a full turn spaces them evenly).
class PatternFeature final : public Feature {
public:
    using Feature::Feature;
    enum class Layout { Linear, Circular };
    Layout layout = Layout::Linear;
    int count = 3; // including the original
    Vec3 direction{1, 0, 0};
    double spacing = 10; // mm, linear
    Vec3 axisOrigin;
    Vec3 axis{0, 0, 1};
    double angle = 2 * kPi; // radians, circular

    // The motion of every copy (not the original).
    std::vector<geom::RigidMotion> copies() const;

    FeatureKind kind() const override { return FeatureKind::Pattern; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new PatternFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// The step that split a body into bodies: of the separate pieces its input is
// made of, the body keeps one. `pieces` describes every piece as it was when
// the body was split, pieces[0] being the one kept; the others became bodies
// of their own (SplitPieceFeature). Pieces are found again by where they are
// and how big (geom::matchSolids), so upstream edits carry through. Pieces
// that appear later (a new cut) stay in this body: nothing disappears.
class SplitFeature final : public Feature {
public:
    using Feature::Feature;
    std::vector<geom::SolidSignature> pieces;

    // For each recorded piece, its solid among `solids` (or -1). The body and
    // its split-off pieces all use this, so they agree on who gets what.
    std::vector<int> assign(const std::vector<geom::Shape>& solids) const;

    FeatureKind kind() const override { return FeatureKind::Split; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new SplitFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// The first step of a body split off another: piece `piece` of the source
// body's shape just before its Split step (`splitFeature`). Follows every
// upstream edit of the source; fails with a clear message when the piece is
// no longer separate or no longer exists.
class SplitPieceFeature final : public Feature {
public:
    using Feature::Feature;
    Uuid sourceBody;
    Uuid splitFeature;
    int piece = 1; // index into the split's pieces (0 is the one the source keeps)

    FeatureKind kind() const override { return FeatureKind::SplitPiece; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new SplitPieceFeature(*this)); }
    bool isBaseFeature() const override { return true; }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
    std::vector<Uuid> dependencies() const override { return {sourceBody}; }
    void remapReferences(const std::map<Uuid, Uuid>& copies) override;
};

// The first step of a body made by Mirror or Pattern with "Separate bodies":
// the source body's current shape, mirrored across a plane or moved by a
// rigid motion. It follows every change of the source body.
class CopyFeature final : public Feature {
public:
    using Feature::Feature;
    Uuid sourceBody;
    bool mirror = false;
    Vec3 planeOrigin;             // mirror
    Vec3 planeNormal{1, 0, 0};    // mirror
    geom::RigidMotion motion;     // otherwise

    FeatureKind kind() const override { return FeatureKind::Copy; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new CopyFeature(*this)); }
    bool isBaseFeature() const override { return true; }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
    std::vector<Uuid> dependencies() const override { return {sourceBody}; }
    void remapReferences(const std::map<Uuid, Uuid>& copies) override;
};

// Hollows the body, opening the referenced faces, with walls of `thickness`.
class ShellFeature final : public Feature {
public:
    using Feature::Feature;
    std::vector<FaceRef> faces;
    double thickness = 2; // mm

    FeatureKind kind() const override { return FeatureKind::Shell; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new ShellFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// A reference to one closed region of a sketch, by a point inside it (in
// sketch coordinates) plus its area as a tie-breaker. Survives dimension
// edits that keep the region around that point.
struct ProfileRef {
    Vec2 interiorPoint;
    double area = 0;
};

enum class ExtrudeMode { NewBody, Join, Cut };
std::string_view toString(ExtrudeMode mode);

// Extrudes sketch profiles along the sketch normal: as a new body (base
// feature), or joined to / cut from the body it belongs to.
class ExtrudeFeature final : public Feature {
public:
    using Feature::Feature;
    Uuid sketchId;
    std::vector<ProfileRef> profiles;
    double distance = 10; // mm along the sketch normal (negative = opposite side)
    ExtrudeMode mode = ExtrudeMode::NewBody;
    // Cuts: extend through the whole body in the direction of `distance`,
    // so through-holes stay through when the body gets thicker.
    bool throughAll = false;
    // Centered on the sketch plane: |distance| is the total thickness, half
    // on each side (through-all cuts go through both ways).
    bool symmetric = false;

    FeatureKind kind() const override { return FeatureKind::Extrude; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new ExtrudeFeature(*this)); }
    bool isBaseFeature() const override { return mode == ExtrudeMode::NewBody; }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
    std::vector<Uuid> dependencies() const override { return {sketchId}; }
    void remapReferences(const std::map<Uuid, Uuid>& copies) override;

    // The extruded tool solid alone (before join/cut). `input` sizes
    // through-all cuts.
    Result<geom::Shape> toolSolid(const geom::Shape& input, const EvalContext& context) const;
};

// A cylindrical hole drilled at the rim of an existing circular edge (for
// example to turn a hole into a heat-set insert pilot hole). The rim gives
// the center; the flat face next to it gives the drilling direction.
class HoleFeature final : public Feature {
public:
    using Feature::Feature;
    EdgeRef rim;
    double diameter = 4.0; // mm
    double depth = 6.0;    // mm
    std::string preset;    // e.g. "M3 heat-set insert" (informational)

    FeatureKind kind() const override { return FeatureKind::Hole; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new HoleFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Where a hole at a circular rim starts and which way it goes into the body.
struct HolePlacement {
    Vec3 center;
    Vec3 direction; // unit, into the material
    double rimRadius = 0;
};
std::optional<HolePlacement> holePlacement(const geom::Shape& shape, int edgeIndex);

// Typical pilot holes for brass heat-set inserts (community rules of thumb,
// not a standard: check your insert's datasheet).
struct InsertPreset {
    const char* name;  // "M3"
    double diameter;   // mm
    double depth;      // mm
};
const std::vector<InsertPreset>& heatSetInsertPresets();

// Revolves sketch profiles around the sketch's own X or Y axis (through the
// sketch origin): new body, or joined to / cut from the body it belongs to.
enum class SketchAxis { X, Y };

class RevolveFeature final : public Feature {
public:
    using Feature::Feature;
    Uuid sketchId;
    std::vector<ProfileRef> profiles;
    SketchAxis axis = SketchAxis::Y;
    double angle = 6.283185307179586; // radians
    ExtrudeMode mode = ExtrudeMode::NewBody;

    FeatureKind kind() const override { return FeatureKind::Revolve; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new RevolveFeature(*this)); }
    bool isBaseFeature() const override { return mode == ExtrudeMode::NewBody; }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
    std::vector<Uuid> dependencies() const override { return {sketchId}; }
    void remapReferences(const std::map<Uuid, Uuid>& copies) override;
};

} // namespace os::doc
