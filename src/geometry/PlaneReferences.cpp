// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/PlaneReferences.h"

#include "core/Log.h"
#include "geometry/Modeling.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>

namespace os::geom {

namespace {

constexpr double kTwoPi = 6.283185307179586;

} // namespace

PlaneReferences planeReferences(const Shape& shape, const PlaneFrame& plane, double tolerance)
{
    PlaneReferences out;
    if (shape.isNull())
        return out;
    const Vec3 n = plane.normal().normalized();
    auto distance = [&](const Vec3& p) { return (p - plane.origin).dot(n); };
    auto inPlane = [&](const Vec3& p) { return std::abs(distance(p)) <= tolerance; };

    // A body the plane misses has nothing in it (microseconds per body).
    const BoundingBox box = approximateBoundingBox(shape);
    if (!box.valid)
        return out;
    double lo = 1e300, hi = -1e300;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 p{corner & 1 ? box.max.x : box.min.x, corner & 2 ? box.max.y : box.min.y,
                     corner & 4 ? box.max.z : box.min.z};
        lo = std::min(lo, distance(p));
        hi = std::max(hi, distance(p));
    }
    if (lo > tolerance || hi < -tolerance)
        return out;

    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        const ShapeData& data = *shape.data();
        // Flat faces in the plane: their centers.
        for (int i = 1; i <= data.faces.Extent(); ++i) {
            const TopoDS_Face face = TopoDS::Face(data.faces.FindKey(i));
            BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane)
                continue;
            const gp_Pln pln = surface.Plane();
            if (std::abs(detail::fromDir(pln.Axis().Direction()).dot(n)) < 1 - 1e-9
                || !inPlane(detail::fromPnt(pln.Location())))
                continue;
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            if (props.Mass() > 1e-12)
                out.faceCenters.push_back(detail::fromPnt(props.CentreOfMass()));
        }
        // Edges in the plane: a few samples along each must lie in it.
        for (int i = 1; i <= data.edges.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(data.edges.FindKey(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            BRepAdaptor_Curve curve(edge);
            const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
            bool flat = true;
            for (int k = 0; k <= 4 && flat; ++k)
                flat = inPlane(detail::fromPnt(curve.Value(t0 + (t1 - t0) * k / 4.0)));
            if (!flat)
                continue;
            PlaneEdge e;
            const Vec3 a = detail::fromPnt(curve.Value(t0)), b = detail::fromPnt(curve.Value(t1));
            switch (curve.GetType()) {
            case GeomAbs_Line:
                if ((b - a).length() < detail::kMinLength)
                    continue;
                e.kind = PlaneEdge::Kind::Line;
                e.start = a;
                e.end = b;
                break;
            case GeomAbs_Circle: {
                const gp_Circ circle = curve.Circle();
                e.center = detail::fromPnt(circle.Location());
                e.radius = circle.Radius();
                if (std::abs(t1 - t0) >= kTwoPi - 1e-9 || (b - a).length() < detail::kMinLength) {
                    e.kind = PlaneEdge::Kind::Circle;
                } else {
                    // Counterclockwise about the plane normal: the circle's own
                    // axis may point the other way.
                    const bool along = detail::fromDir(circle.Axis().Direction()).dot(n) > 0;
                    e.kind = PlaneEdge::Kind::Arc;
                    e.start = along ? a : b;
                    e.end = along ? b : a;
                }
                break;
            }
            default: {
                e.kind = PlaneEdge::Kind::Polyline;
                constexpr int kSamples = 32;
                for (int k = 0; k <= kSamples; ++k)
                    e.points.push_back(detail::fromPnt(curve.Value(t0 + (t1 - t0) * k / double(kSamples))));
                e.start = a;
                e.end = b;
                break;
            }
            }
            out.edges.push_back(std::move(e));
        }
        // Vertices in the plane, but not the seam point of a whole circle (a
        // hole's rim has one vertex, which is no corner) unless an open edge
        // ends there too.
        auto closeTo = [](const std::vector<Vec3>& list, const Vec3& p) {
            return std::any_of(list.begin(), list.end(), [&](const Vec3& q) { return (q - p).length() < 1e-7; });
        };
        std::vector<Vec3> seams, ends;
        for (const auto& e : out.edges) {
            if (e.kind == PlaneEdge::Kind::Circle)
                continue;
            ends.push_back(e.start);
            ends.push_back(e.end);
        }
        for (int i = 1; i <= data.edges.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(data.edges.FindKey(i));
            if (BRep_Tool::Degenerated(edge))
                continue;
            BRepAdaptor_Curve curve(edge);
            if (curve.GetType() != GeomAbs_Circle || std::abs(curve.LastParameter() - curve.FirstParameter()) < kTwoPi - 1e-9)
                continue;
            const Vec3 seam = detail::fromPnt(curve.Value(curve.FirstParameter()));
            if (inPlane(seam))
                seams.push_back(seam);
        }
        for (int i = 1; i <= data.vertices.Extent(); ++i) {
            const Vec3 p = detail::fromPnt(BRep_Tool::Pnt(TopoDS::Vertex(data.vertices.FindKey(i))));
            if (inPlane(p) && (!closeTo(seams, p) || closeTo(ends, p)))
                out.vertices.push_back(p);
        }
    } catch (const Standard_Failure& failure) {
        OS_LOG(Warning, Kernel) << "planeReferences failed: " << detail::describeFailure(failure);
        return {};
    }
    return out;
}

} // namespace os::geom
