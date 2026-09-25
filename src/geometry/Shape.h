#pragma once

#include "core/Math.h"

#include <memory>

namespace os::geom {

struct ShapeData; // defined in geometry/internal/ShapeData.h (OCCT types)

// Opaque, immutable handle to an exact B-rep shape owned by the kernel.
// Cheap to copy (shared). Code outside the geometry library never sees OCCT
// types; it refers to sub-shapes by *topology index* (0-based, valid only for
// this exact Shape instance) or by a persistent reference (see TopoRef.h).
class Shape {
public:
    Shape() = default;
    explicit Shape(std::shared_ptr<const ShapeData> data) : data_(std::move(data)) {}

    bool isNull() const;

    int faceCount() const;
    int edgeCount() const;
    int vertexCount() const;
    int solidCount() const;

    // Identity of this particular kernel result. Two Shapes compare equal
    // only if they share the same underlying data.
    bool sameAs(const Shape& other) const { return data_ == other.data_; }

    const ShapeData* data() const { return data_.get(); }

private:
    std::shared_ptr<const ShapeData> data_;
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
    bool valid = false;
    Vec3 size() const { return max - min; }
    Vec3 center() const { return (min + max) * 0.5; }
};

// A rigid motion: rotation about the axis through `center` along `axis` by
// `angle` radians, followed by `translation`.
struct RigidMotion {
    Vec3 center;
    Vec3 axis{0, 0, 1};
    double angle = 0;
    Vec3 translation;
    Vec3 apply(const Vec3& p) const;
    bool isIdentity() const { return std::abs(angle) < 1e-12 && translation.length() < 1e-12; }
};

enum class SurfaceKind { Plane, Cylinder, Cone, Sphere, Torus, BSpline, Other };
enum class CurveKind { Line, Circle, Ellipse, BSpline, Other };

struct FaceInfo {
    SurfaceKind kind = SurfaceKind::Other;
    Vec3 centroid;   // area centroid (may lie off the face, e.g. on a cylinder's axis)
    Vec3 normal;     // outward normal at `point` (exact for planes)
    Vec3 point;      // a point on the face (mid parameters), where `normal` is taken
    double area = 0;
    // Plane axis origin for planar faces (a point on the plane).
    Vec3 planeOrigin;
    // Cylinders and cones: the axis (origin nearest the face centroid) and radius.
    Vec3 axisOrigin;
    Vec3 axisDirection;
    double radius = 0;
    bool isPlanar() const { return kind == SurfaceKind::Plane; }
    bool hasAxis() const { return kind == SurfaceKind::Cylinder || kind == SurfaceKind::Cone; }
};

struct EdgeInfo {
    CurveKind kind = CurveKind::Other;
    Vec3 start;
    Vec3 end;
    Vec3 midpoint;       // point at mid parameter
    Vec3 tangent;        // unit tangent at mid parameter
    double length = 0;
    double radius = 0;   // for circles
    Vec3 center;         // for circles
    Vec3 axis;           // for circles (unit normal of the circle's plane)
};

} // namespace os::geom
