// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "core/Result.h"
#include "core/Uuid.h"
#include "document/Feature.h"
#include "sketch/Sketch.h"

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <vector>

// Construction axes and planes ("datums"): reference geometry the user
// creates to model against (rotate about, pattern around, mirror across,
// align onto, sketch on). They are made from faces, edges and points of
// bodies and follow them: every reference names the step whose output holds
// the geometry (like a sketch attached to a face), so an upstream edit moves
// the datum, and a datum whose geometry is gone fails with a plain message
// and keeps its last position.
namespace os::doc {

// A face, edge or point of the output of one step of a body. Points are the
// end of an edge nearest `point` (a corner) or a circle's center.
struct GeometryRef {
    enum class Kind { Face, Edge, Vertex, Center };
    Kind kind = Kind::Face;
    Uuid body;
    Uuid feature;
    FaceRef face; // Face
    EdgeRef edge; // Edge, Vertex, Center
    Vec3 point;   // Vertex: where the corner was when picked (which end of the edge)
};

// Captures a reference to face / edge `index` of the body's current (shown)
// shape: the output of its last step that has one. nullopt when the index is
// not valid, or a Vertex / Center that the edge does not have.
std::optional<GeometryRef> makeGeometryRef(const Body& body, GeometryRef::Kind kind, int index, const Vec3& near = {});

enum class DatumKind { Axis, Plane };

// How a datum is made (stored by name, see toString).
enum class DatumMethod {
    AxisThrough,   // the axis of a hole or shaft (round face) or of a circle (circular edge): refs[0]
    AxisAlongEdge, // along a straight edge: refs[0]
    AxisTwoPoints, // through two points (corners, circle centers): refs[0], refs[1]
    AxisParallel,  // parallel to X / Y / Z (`originIndex` 0/1/2) through a point: refs[0]
    PlaneOffset,   // `distance` from a flat face (refs[0]) along its outward normal, or from an
                   // origin plane (no refs; `originIndex` = its normal: 0 YZ, 1 XZ, 2 XY)
    PlaneAngle,    // through a straight edge (refs[0]) at `angle` to a flat face (refs[1]) it lies on
    PlaneMidway,   // midway between two parallel flat faces: refs[0], refs[1]
};
std::string_view toString(DatumMethod method);
std::optional<DatumMethod> datumMethodFromString(std::string_view text);
DatumKind kindOf(DatumMethod method);

// Where a datum is. Axes: through `origin` along `direction`. Planes: through
// `origin` with normal `direction`, `xAxis` an in-plane direction. `center`
// is where the drawing is centered and `size` a suggested half size (0 = let
// the view decide), both for drawing only.
struct DatumGeometry {
    Vec3 origin;
    Vec3 direction{0, 0, 1};
    Vec3 xAxis{1, 0, 0};
    Vec3 center;
    double size = 0;
};

class Datum;

// The largest offset a plane may have (mm): what a project file holds
// (Datum::fromJson refuses more).
inline constexpr double kMaxDatumDistance = 1e6;

// Whether a datum's values can be kept and read back from a file: an offset
// within +-kMaxDatumDistance, an angle within +-180 degrees, both finite.
// Fails with the plain message the Model panel's fields show (the Plane
// tool's value and AddDatumCommand / EditDatumCommand check it too).
Status checkDatumValues(const Datum& datum);

class Datum {
public:
    explicit Datum(Uuid id = Uuid::generate()) : id_(id) {}
    // The same datum under another id (a hidden copy that goes with a copied
    // body, DuplicateBodyCommand).
    Datum copyWithId(Uuid id) const
    {
        Datum copy = *this;
        copy.id_ = id;
        return copy;
    }

    const Uuid& id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    DatumKind kind() const { return kindOf(method); }

    DatumMethod method = DatumMethod::PlaneOffset;
    std::vector<GeometryRef> refs;
    int originIndex = -1; // AxisParallel: X/Y/Z; PlaneOffset without a face: the origin plane's normal
    double distance = 0;  // PlaneOffset, mm
    double angle = 0;     // PlaneAngle, radians

    // The last resolved position (kept when the references no longer
    // resolve, like a body's last good shape), and why it failed then.
    const DatumGeometry& geometry() const { return geometry_; }
    void setGeometry(const DatumGeometry& geometry) { geometry_ = geometry; }
    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    void setError(std::string error) { error_ = std::move(error); }

    // Editable values for the Model panel: an offset plane's distance, an
    // angled plane's angle.
    std::vector<ParameterInfo> parameters() const;
    Status setParameter(std::string_view key, double value);
    // The bodies it is made from.
    std::vector<Uuid> bodies() const;

    nlohmann::json toJson() const;
    static Result<Datum> fromJson(const nlohmann::json& json);

private:
    Uuid id_;
    std::string name_;
    bool visible_ = true;
    DatumGeometry geometry_;
    std::string error_;
};

// Where the datum is now, from its references in the document (during a body
// recompute only the steps before the one evaluated count). Fails with a
// plain message when a reference is gone or no longer fits (a face no longer
// flat, two faces no longer parallel). The two halves below, in one call.
Result<DatumGeometry> resolveDatum(const Datum& datum, const EvalContext& context);

// What a datum's references resolved to: the facts of a face or edge, and
// for a corner or a circle's center the point. Resolving is the kernel work
// (finding the face or edge again); the datum's geometry follows from these
// alone (datumGeometry), so the Plane tool previews any distance or angle
// while its arrow is dragged without calling the kernel.
struct ResolvedRef {
    GeometryRef::Kind kind = GeometryRef::Kind::Face;
    geom::FaceInfo face; // Face
    geom::EdgeInfo edge; // Edge, Vertex, Center
    Vec3 point;          // Vertex, Center
};
Result<std::vector<ResolvedRef>> resolveDatumRefs(const Datum& datum, const EvalContext& context);
// No kernel calls: `refs` are datum.refs resolved.
Result<DatumGeometry> datumGeometry(const Datum& datum, const std::vector<ResolvedRef>& refs);

// A sketch plane on a datum plane: the world origin projected onto it (as for
// a sketch on a face), x kept horizontal where possible.
sketch::Plane sketchPlaneOn(const DatumGeometry& plane);

} // namespace os::doc
