// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "core/Units.h"
#include "geometry/Shape.h"
#include "geometry/TopoSignature.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// Kernel-facing modeling API. Every function here:
//   * catches all kernel exceptions,
//   * validates the result shape,
//   * returns a Result with a user message and a developer message on failure.
// All lengths are millimeters, all angles radians.
namespace os::geom {

Result<Shape> makeBox(const Vec3& origin, const Vec3& size);
Result<Shape> makeCylinder(const Vec3& baseCenter, const Vec3& axis, double radius, double height);

// Moves a planar face along its outward normal by `distance` (positive adds
// material, negative removes it). Coplanar faces are merged afterwards so a
// pushed box face stays a single face.
Result<Shape> pushPullFace(const Shape& shape, int faceIndex, double distance);
// Push/pull that takes the face's rounded and bevelled edges along (fillets
// and chamfers keep their size, the walls below them extend or shorten): the
// part is split on a plane just below those edges, the top piece moves, and
// the cross-section fills the gap (or a slab of it is taken out). Fails with
// ErrorCode::Unsupported when the face has no such edges or the region that
// moves is not straight walls; callers then use pushPullFace.
Result<Shape> pushPullFaceKeepingEdges(const Shape& shape, int faceIndex, double distance);

// How a refused fillet, chamfer or shell explains itself. By default the
// message says what went wrong and what to try ("Try a smaller radius.").
// With `suggest`, the kernel is asked a few more times (a bisection bounded
// in attempts and time) for the largest size that still works, and the
// message names it in `unit` ("Try 2.9 mm or less."). Interactive previews
// ask for it; history recompute and file loading do not, so a step that keeps
// failing does not cost extra kernel attempts on every rebuild.
struct SizeAdvice {
    bool suggest = false;
    LengthUnit unit = LengthUnit::Millimeter;
};

namespace detail {
// The search behind SizeAdvice, exposed for tests: the largest size below
// `failed` for which `works` holds. Feasibility is close to monotonic in the
// size for these operations. A tiny size is tried first (kTinySize, or 2% of
// `failed` if smaller): when it fails too, no practical size works and the
// answer is 0. Otherwise the bounds [works, fails] are narrowed by bisection,
// geometric while they are far apart (a value typed 100x too large) and
// arithmetic after. At most kMaxSizeAttempts attempts are made, and none
// that would end past kSizeBudget, judged by the slowest attempt so far
// (`firstAttempt`: how long the refused call took). Returns nullopt when the
// search stopped before the bounds were within a factor of 1.5: a size far
// below the limit would mislead, so the caller keeps its general wording.
inline constexpr double kTinySize = 0.1; // mm
inline constexpr int kMaxSizeAttempts = 10;
inline constexpr std::chrono::milliseconds kSizeBudget{600};
std::optional<double> largestWorkingSize(double failed, std::chrono::steady_clock::duration firstAttempt,
                                         const std::function<bool(double)>& works);
} // namespace detail

Result<Shape> filletEdges(const Shape& shape, const std::vector<int>& edgeIndices, double radius,
                          const SizeAdvice& advice = {});
Result<Shape> chamferEdges(const Shape& shape, const std::vector<int>& edgeIndices, double distance,
                           const SizeAdvice& advice = {});

// Hollows the solid, removing the given faces (openings) and keeping walls of
// `thickness` inside the original boundary.
Result<Shape> shell(const Shape& shape, const std::vector<int>& openFaces, double thickness, const SizeAdvice& advice = {});
// Removes faces and closes the gap by extending their neighbours: holes,
// fillets, chamfers and bosses disappear (OCCT defeaturing).
Result<Shape> deleteFaces(const Shape& shape, const std::vector<int>& faceIndices);
// Moves one face along its own normal by `distance` (positive = outward, the
// body grows) while its neighbours extend or shrink to meet it. A hole's wall
// moved outward (into the hole) makes the hole smaller. Refused when the
// neighbours cannot follow (e.g. tangent fillets): the volume change must be
// close to face area x distance.
Result<Shape> offsetFace(const Shape& shape, int faceIndex, double distance);

enum class BooleanKind { Union, Subtract, Intersect };
Result<Shape> booleanOp(const Shape& a, const Shape& b, BooleanKind kind);

Result<Shape> translated(const Shape& shape, const Vec3& offset);
Result<Shape> rotated(const Shape& shape, const Vec3& axisOrigin, const Vec3& axisDirection, double angleRadians);
// Applies a rigid motion (see RigidMotion in Shape.h).
Result<Shape> transformed(const Shape& shape, const RigidMotion& motion);
// The mirror image across the plane through `planeOrigin` with `planeNormal`.
Result<Shape> mirrored(const Shape& shape, const Vec3& planeOrigin, const Vec3& planeNormal);
// The shape together with its mirror image, fused into one (halves that
// touch the plane merge; a plane away from the body leaves two pieces).
Result<Shape> mirrorJoined(const Shape& shape, const Vec3& planeOrigin, const Vec3& planeNormal);
// The shape and one copy per motion, fused in a single boolean pass.
Result<Shape> repeatJoined(const Shape& shape, const std::vector<RigidMotion>& copies);

// ---- Separate pieces -----------------------------------------------------------
// The separate solids a shape is made of, each as its own shape (in a stable
// order for a given shape). Empty for a null shape.
std::vector<Shape> solids(const Shape& shape);
// Where a solid is and how big (for matchSolids, TopoSignature.h).
SolidSignature solidSignature(const Shape& solid);
// One shape made of these solids, not fused (one solid stays a plain solid;
// several make a multi-piece shape with the usual "separate pieces" warning).
Result<Shape> gatherSolids(const std::vector<Shape>& solids);


// ---- Queries ----------------------------------------------------------------
double volume(const Shape& shape);
double surfaceArea(const Shape& shape);
// Tight (optimal) axis-aligned bounding box of the exact geometry. Costs tens
// of milliseconds on curved parts the first time; cached per Shape after that.
// Use it for sizes shown to the user.
BoundingBox boundingBox(const Shape& shape);
// Conservative box from the geometry's own bounds (never smaller than the
// shape; curved or B-spline faces may make it slightly larger). Microseconds.
// Use it for camera fitting, mesh resolution and scale normalization.
BoundingBox approximateBoundingBox(const Shape& shape);
bool isValid(const Shape& shape);

// Faces of `current` that a step created or modified: present in the step's
// output `after` but not in its input `before`, compared by kernel identity
// (faces a step leaves untouched keep their identity). With a null `before`
// (a base feature) every face of `after` counts. Faces changed again by later
// steps are not in `current` any more and are not reported.
std::vector<int> facesChangedBy(const Shape& before, const Shape& after, const Shape& current);
// Like facesChangedBy, but only faces on new surface geometry (a fillet, a
// chamfer, the walls of an extrusion or cut), not neighbours a step merely
// trimmed (those keep their original surface). Empty for pure moves.
std::vector<int> facesCreatedBy(const Shape& before, const Shape& after, const Shape& current);

std::optional<FaceInfo> faceInfo(const Shape& shape, int faceIndex);
std::optional<EdgeInfo> edgeInfo(const Shape& shape, int edgeIndex);
// Indices of the faces adjacent to an edge (1 for seam/boundary, usually 2).
std::vector<int> facesOfEdge(const Shape& shape, int edgeIndex);

// A point inside a flat face (not in a hole, not beside an L-shaped outline),
// preferring `preferred` (e.g. the centroid, which can lie off the face), else the
// most interior point of a sample grid. nullopt for curved faces.
std::optional<Vec3> pointOnFace(const Shape& shape, int faceIndex, const Vec3& preferred);

// How thick the part is behind a flat face: from `point` (on the face)
// straight into the material to where it leaves again, when it leaves
// through a parallel flat face (then the distance is the gap between the two
// planes). nullopt when the other side is slanted, curved or missing.
struct FaceThickness {
    double distance = 0; // mm
    Vec3 from;           // the measuring point on the face
    Vec3 to;             // where the line leaves, on the opposite face
    int oppositeFace = -1;
};
std::optional<FaceThickness> faceThickness(const Shape& shape, int faceIndex, const Vec3& point);

// ---- Measurement -------------------------------------------------------------
enum class SubShapeKind { Face, Edge, Vertex, Whole };
struct SubShapeRef {
    const Shape* shape = nullptr;
    SubShapeKind kind = SubShapeKind::Whole;
    int index = -1;
};

// Where a face or edge sits and which way it points, for aligning bodies:
// flat faces give centroid + outward normal and are "sided" (two flat faces
// align touching, facing each other); straight edges give midpoint +
// direction; circles, cylinders and cones give center + axis.
struct AlignFrame {
    // Finite: a face or edge of a body; `point` is where it sits. Line and
    // Plane: an axis or a plane without ends (X/Y/Z, the origin planes,
    // construction axes and planes); `point` is any point on it and a source
    // lands at its nearest point there. Point: the origin; `direction` is
    // only where an offset goes.
    enum class Extent { Finite, Line, Plane, Point };
    Vec3 point;
    Vec3 direction;
    bool sided = false;
    Extent extent = Extent::Finite;
};
std::optional<AlignFrame> alignFrame(const Shape& shape, SubShapeKind kind, int index);
// The motion that brings `source` onto `target`: directions opposite for two
// sided frames (flush with `flip`), otherwise parallel with the smaller
// rotation (reversed with `flip`); points coincide (for a line or plane
// target: the source point moves to its nearest point on it), then `offset`
// along the target direction. A point target only moves (no rotation).
RigidMotion alignMotion(const AlignFrame& source, const AlignFrame& target, bool flip, double offset);

struct Measurement {
    double distance = 0;             // exact minimum distance (0 when touching)
    Vec3 pointA, pointB;             // closest points
    std::optional<double> angle;     // radians, between planar faces or straight edges
    std::optional<double> parallelGap; // distance between parallel planar faces
};

std::optional<Measurement> measure(const SubShapeRef& a, const SubShapeRef& b);

// ---- Serialization ----------------------------------------------------------
// Native OCCT BRep text: the geometry cache inside project files, and the
// exact geometry of imported bodies (their source of truth). Without
// triangulation, only the exact geometry is written (the display mesh the
// view stored in the shape is left out).
std::string toBrepString(const Shape& shape, bool withTriangulation = true);
Result<Shape> fromBrepString(const std::string& text);

} // namespace os::geom
