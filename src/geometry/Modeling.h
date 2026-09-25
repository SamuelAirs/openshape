#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

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

Result<Shape> filletEdges(const Shape& shape, const std::vector<int>& edgeIndices, double radius);
Result<Shape> chamferEdges(const Shape& shape, const std::vector<int>& edgeIndices, double distance);

// Hollows the solid, removing the given faces (openings) and keeping walls of
// `thickness` inside the original boundary.
Result<Shape> shell(const Shape& shape, const std::vector<int>& openFaces, double thickness);

enum class BooleanKind { Union, Subtract, Intersect };
Result<Shape> booleanOp(const Shape& a, const Shape& b, BooleanKind kind);

Result<Shape> translated(const Shape& shape, const Vec3& offset);
Result<Shape> rotated(const Shape& shape, const Vec3& axisOrigin, const Vec3& axisDirection, double angleRadians);

// ---- Queries ----------------------------------------------------------------
double volume(const Shape& shape);
double surfaceArea(const Shape& shape);
// Tight (optimal) axis-aligned bounding box of the exact geometry.
BoundingBox boundingBox(const Shape& shape);
bool isValid(const Shape& shape);

std::optional<FaceInfo> faceInfo(const Shape& shape, int faceIndex);
std::optional<EdgeInfo> edgeInfo(const Shape& shape, int edgeIndex);
// Indices of the faces adjacent to an edge (1 for seam/boundary, usually 2).
std::vector<int> facesOfEdge(const Shape& shape, int edgeIndex);

// ---- Measurement -------------------------------------------------------------
enum class SubShapeKind { Face, Edge, Vertex, Whole };
struct SubShapeRef {
    const Shape* shape = nullptr;
    SubShapeKind kind = SubShapeKind::Whole;
    int index = -1;
};

struct Measurement {
    double distance = 0;             // exact minimum distance (0 when touching)
    Vec3 pointA, pointB;             // closest points
    std::optional<double> angle;     // radians, between planar faces or straight edges
    std::optional<double> parallelGap; // distance between parallel planar faces
};

std::optional<Measurement> measure(const SubShapeRef& a, const SubShapeRef& b);

// ---- Serialization ----------------------------------------------------------
// Native OCCT BRep text. Used only as a geometry *cache* inside project files;
// the parametric document is the source of truth.
std::string toBrepString(const Shape& shape);
Result<Shape> fromBrepString(const std::string& text);

} // namespace os::geom
