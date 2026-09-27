// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "core/Uuid.h"
#include "document/Fasteners.h"
#include "geometry/Shape.h"
#include "geometry/TopoSignature.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
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
    Split, SplitPiece, Copy, Holes, Imported, Text
};

// What a feature may consult besides its input shape.
struct EvalContext {
    const Document* document = nullptr;
    // Set while a body is being recomputed: the body and the index of the
    // feature being evaluated (earlier features' results are current).
    const Body* body = nullptr;
    int featureIndex = -1;
    // Set for interactive previews (Document::preview): a refused fillet,
    // chamfer or shell may then spend a few more kernel attempts to name a
    // size that works (geom::SizeAdvice). Not during history recompute.
    bool interactive = false;
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

// An editable string (a Text step's text), edited in the Model panel like
// the scalars.
struct TextParameterInfo {
    std::string key;
    std::string label;
    std::string value; // UTF-8
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
    // Editable strings (none for most steps).
    virtual std::vector<TextParameterInfo> textParameters() const { return {}; }
    virtual Status setTextParameter(std::string_view key, const std::string& value);
    std::optional<std::string> textParameter(std::string_view key) const;

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
    // The step that moves a body by `m` (a rotation when m.angle is not 0).
    void setMotion(const geom::RigidMotion& m);

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
// With keepOriginal off the body becomes its mirror image instead: the last
// step of an independent copy made by Mirror as a separate body.
class MirrorFeature final : public Feature {
public:
    using Feature::Feature;
    Vec3 planeOrigin;
    Vec3 planeNormal{1, 0, 0};
    bool keepOriginal = true; // files without the field: joined

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
// no longer separate or no longer exists. Files only: Split into bodies now
// makes each piece an independent copy of the source's history ending in a
// Split step that keeps that piece (cmd::makeSplitBodyCommand).
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

// The first step of a body made by Mirror or Pattern with "Separate bodies"
// in files from before independent copies: the source body's current shape,
// mirrored across a plane or moved by a rigid motion. It follows every change
// of the source body. Files only: Mirror and Pattern now make independent
// copies (the source's history cloned, then a Mirror or Move step).
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

// How much imported geometry a project holds (the BRep text it stores):
// per Imported step and in all. Imports beyond it are refused, saving it
// fails plainly, and the loader reads no more (io/ProjectFile).
inline constexpr std::uint64_t kMaxImportedBodyBytes = 256ull * 1024 * 1024;
inline constexpr std::uint64_t kMaxImportedGeometryBytes = 512ull * 1024 * 1024;

// The first step of a body imported from a file (STEP): its exact geometry
// as imported (millimeters, placed as in the file). Projects store the
// geometry itself (imports/<feature id>.brep inside the file), so it is this
// step's source of truth, like other steps' parameters; later steps build on
// it as on any other body.
class ImportedFeature final : public Feature {
public:
    using Feature::Feature;
    geom::Shape shape;   // the imported solid
    std::string source;  // the file it came from ("bracket.step"), shown in the Model panel
    double volume = 0;   // mm³ when imported; a loaded file's geometry must still have it

    // Where a project file keeps the geometry: imports/<feature id>.brep.
    std::string entryName() const;
    // The entry a loaded file named in this step's params, and the hash its
    // content must have (read and checked by the loader before parsing).
    const std::string& loadedEntry() const { return loadedEntry_; }
    const std::string& loadedHash() const { return loadedHash_; }
    // `shape` as BRep text without triangulation, made once (shapes never change).
    const std::string& brepText() const;
    // FNV-1a (64 bit) of stored geometry, 16 hex digits: catches a damaged or
    // swapped imports/ entry before the kernel parses it.
    static std::string hashOf(const std::string& text);
    // Sets the geometry (and the volume it must keep).
    void setShape(const geom::Shape& imported);
    // The geometry a project file stored as `brep` (checked by the loader):
    // saving again writes the same text.
    void setLoadedShape(const geom::Shape& loaded, const std::string& brep);

    FeatureKind kind() const override { return FeatureKind::Imported; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new ImportedFeature(*this)); }
    bool isBaseFeature() const override { return true; }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override { return {}; }
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;

private:
    std::string loadedEntry_;
    std::string loadedHash_;
    mutable std::shared_ptr<const std::string> brep_; // shared by clones: same shape
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
    // Side walls tilted by this angle (radians): positive narrows the shape
    // as it goes away from the sketch (both ways when symmetric), negative
    // widens it; 0 = straight walls (steps from older files).
    double draftAngle = 0;

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

// What a Hole step at a rim makes. Plain: a cylinder of `diameter` x `depth`
// (e.g. a heat-set insert's pilot hole; steps from older files). Counterbore:
// the same cylinder as a screw head's seat, refused when it is not wider than
// the hole or would reach through the part. Countersink: a cone of
// `diameter` at the surface and included `angle`, down to the hole.
enum class HoleKind { Plain, Counterbore, Countersink };
std::string_view toString(HoleKind kind);

// Made at the rim of an existing round hole (a circular edge where it meets a
// flat face): the rim gives the center, the flat face the drilling direction.
class HoleFeature final : public Feature {
public:
    using Feature::Feature;
    EdgeRef rim;
    HoleKind holeKind = HoleKind::Plain;
    double diameter = 4.0;       // mm (a countersink's diameter at the surface)
    double depth = 6.0;          // mm (not used by a countersink: its angle sets it)
    double angle = kPi / 2;      // countersink included angle, radians
    std::string preset;          // e.g. "M3 heat-set insert" (informational)

    FeatureKind kind() const override { return FeatureKind::Hole; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new HoleFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Round holes drilled into a flat face at points on it (the Hole tool), all
// alike: a diameter, a depth or through all, and optionally a counterbore or
// countersink for the screw head. The face is found again on every
// recompute; the points are in the face's plane frame (holeFrame: the world
// origin projected onto the face, as for a sketch on it), so the holes ride
// along when an upstream step moves the face. A point that no longer lies on
// the face fails the step with a message.
class HolesFeature final : public Feature {
public:
    using Feature::Feature;
    FaceRef face;
    std::vector<Vec2> positions; // in holeFrame(face) coordinates, mm
    double diameter = 3.4;       // mm
    double depth = 10;           // mm, when not through all
    bool throughAll = true;
    HoleKind head = HoleKind::Plain; // Plain = no head
    double headDiameter = 6.5;   // counterbore / countersink diameter
    double headDepth = 3.4;      // counterbore depth
    double headAngle = kPi / 2;  // countersink included angle, radians
    std::string preset;          // e.g. "M3 normal fit" (informational)

    FeatureKind kind() const override { return FeatureKind::Holes; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new HolesFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// The fonts text can use: ids the app registers with geom::registerFont at
// startup (the bundled Noto Sans, SIL Open Font License). Files store the id.
inline constexpr const char* kTextFontRegular = "NotoSans-Regular";
inline constexpr const char* kTextFontBold = "NotoSans-Bold";

// Text raised from (depth > 0, joined) or cut into (depth < 0) a flat face:
// one line of UTF-8 in a registered font, `size` its capital height,
// centered at `position` in the face's frame (holeFrame, like the Hole
// tool's points, so the text follows the face when an upstream step moves
// it) and turned by `angle` from the frame's x axis. The center must still
// lie on the face.
class TextFeature final : public Feature {
public:
    using Feature::Feature;
    FaceRef face;
    Vec2 position;             // in holeFrame(face) coordinates, mm
    std::string text;          // UTF-8, one line
    double size = 10;          // mm: the height of capital letters
    double depth = 1;          // mm: > 0 raised (emboss), < 0 cut in (deboss)
    double angle = 0;          // radians, counter-clockwise seen from outside the face
    std::string font = kTextFontRegular;

    // A finite angle (radians) as the same direction in [0, 2 pi): what
    // `angle` holds, whatever was typed or read (-90 degrees is 270).
    static double normalizedAngle(double radians);

    FeatureKind kind() const override { return FeatureKind::Text; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new TextFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input, const EvalContext& context) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    std::vector<TextParameterInfo> textParameters() const override;
    Status setTextParameter(std::string_view key, const std::string& value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// The frame holes on a flat face are placed in: on the face's plane, origin
// the world origin projected onto it, x axis horizontal (world X on floors),
// like a sketch started on that face. nullopt for a face that is not flat.
struct HoleFrame {
    Vec3 origin, xAxis, yAxis, normal; // normal: the face's outward normal
    Vec3 toWorld(Vec2 p) const { return origin + xAxis * p.x + yAxis * p.y; }
    Vec2 toLocal(const Vec3& w) const { return {(w - origin).dot(xAxis), (w - origin).dot(yAxis)}; }
};
std::optional<HoleFrame> holeFrame(const geom::Shape& shape, int faceIndex);

// Where a hole at a circular rim starts and which way it goes into the body.
struct HolePlacement {
    Vec3 center;
    Vec3 direction; // unit, into the material
    double rimRadius = 0;
};
std::optional<HolePlacement> holePlacement(const geom::Shape& shape, int edgeIndex);

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
