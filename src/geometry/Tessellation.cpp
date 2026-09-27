// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Tessellation.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/Modeling.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <algorithm>

namespace os::geom {

namespace {

void appendEdgeFromCurve(const TopoDS_Edge& edge, double deflection, double angular, Mesh::EdgePolyline& out)
{
    BRepAdaptor_Curve curve(edge);
    GCPnts_TangentialDeflection sampler(curve, angular, deflection, 2);
    for (int i = 1; i <= sampler.NbPoints(); ++i) {
        const gp_Pnt p = sampler.Value(i);
        out.points.insert(out.points.end(), {float(p.X()), float(p.Y()), float(p.Z())});
    }
}

} // namespace

Mesh tessellate(const Shape& shape, const TessellationParams& params)
{
    Mesh mesh;
    if (shape.isNull())
        return mesh;
    if (params.isolated) {
        // A copy has the same sub-shapes in the same order, so the mesh's face
        // and edge ids are the original's. Its faces start with the
        // original's triangulations (shared, never modified: meshing replaces
        // them), which the mesher reuses where they are fine enough.
        Shape copy;
        try {
            OS_KERNEL_SIGNALS_TO_EXCEPTIONS
            BRepBuilderAPI_Copy copier(occ(shape), false, true);
            copy = makeShape(copier.Shape());
        } catch (const Standard_Failure& failure) {
            OS_LOG(Error, Kernel) << "tessellation copy failed: " << failure.DynamicType()->Name();
            return mesh;
        }
        TessellationParams inPlace = params; // (the copy's box, so the deflection, is the original's)
        inPlace.isolated = false;
        return tessellate(copy, inPlace);
    }

    ScopedTimer timer("tessellate");
    const TopoDS_Shape& occShape = occ(shape);

    double deflection = params.linearDeflection;
    if (!params.relative && params.adaptive) {
        // The cheap conservative box is plenty to pick a resolution; the tight
        // one used to dominate the cost of tessellating every preview.
        const BoundingBox box = approximateBoundingBox(shape);
        if (box.valid) {
            const double diagonal = box.size().length();
            // 0.1% of the diagonal, never finer than 2 microns.
            deflection = std::max(0.002, diagonal * 0.001);
        }
    }

    try {
        OS_KERNEL_SIGNALS_TO_EXCEPTIONS
        // Faces are meshed in parallel inside this one call (OCCT synchronizes it).
        BRepMesh_IncrementalMesh mesher(occShape, deflection, params.relative, params.angularDeflection, true);
        (void)mesher;

        const auto& faces = shape.data()->faces;
        mesh.faceTriangleOffset.assign(static_cast<std::size_t>(faces.Extent()) + 1, 0);
        for (int fi = 1; fi <= faces.Extent(); ++fi) {
            mesh.faceTriangleOffset[static_cast<std::size_t>(fi - 1)] = static_cast<std::uint32_t>(mesh.triangleCount());
            mesh.faceTriangleOffset[static_cast<std::size_t>(fi)] = static_cast<std::uint32_t>(mesh.triangleCount());
            const TopoDS_Face face = TopoDS::Face(faces.FindKey(fi));
            TopLoc_Location location;
            Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
            if (triangulation.IsNull()) {
                OS_LOG(Warning, Geometry) << "face " << (fi - 1) << " has no triangulation";
                continue;
            }
            if (!triangulation->HasNormals())
                BRepLib_ToolTriangulatedShape::ComputeNormals(face, triangulation);

            const gp_Trsf trsf = location.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertexCount());
            const std::uint32_t faceId = static_cast<std::uint32_t>(fi - 1);

            for (int i = 1; i <= triangulation->NbNodes(); ++i) {
                const gp_Pnt p = triangulation->Node(i).Transformed(trsf);
                gp_Dir n = triangulation->Normal(i);
                n.Transform(trsf);
                if (reversed)
                    n.Reverse();
                mesh.positions.insert(mesh.positions.end(), {float(p.X()), float(p.Y()), float(p.Z())});
                mesh.normals.insert(mesh.normals.end(), {float(n.X()), float(n.Y()), float(n.Z())});
            }
            for (int t = 1; t <= triangulation->NbTriangles(); ++t) {
                int a, b, c;
                triangulation->Triangle(t).Get(a, b, c);
                if (reversed)
                    std::swap(b, c);
                mesh.indices.insert(mesh.indices.end(), {base + a - 1, base + b - 1, base + c - 1});
                mesh.triangleFace.push_back(faceId);
            }
            mesh.faceTriangleOffset[static_cast<std::size_t>(fi)] = static_cast<std::uint32_t>(mesh.triangleCount());
        }

        // Edges: prefer the polygon on the adjacent face's triangulation so edge
        // lines sit exactly on mesh vertices; fall back to curve sampling.
        TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
        TopExp::MapShapesAndAncestors(occShape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        const auto& edges = shape.data()->edges;
        for (int ei = 1; ei <= edges.Extent(); ++ei) {
            const TopoDS_Edge edge = TopoDS::Edge(edges.FindKey(ei));
            if (BRep_Tool::Degenerated(edge))
                continue;
            // Seams (where a periodic face such as a cylinder wraps onto
            // itself) are not real edges to the user: don't draw or pick them.
            if (edgeFaces.Contains(edge)) {
                const auto& owners = edgeFaces.FindFromKey(edge);
                bool seam = false;
                for (const TopoDS_Shape& f : owners)
                    seam = seam || BRep_Tool::IsClosed(edge, TopoDS::Face(f));
                if (seam)
                    continue;
            }
            Mesh::EdgePolyline polyline;
            polyline.edgeIndex = ei - 1;

            if (edgeFaces.Contains(edge) && !edgeFaces.FindFromKey(edge).IsEmpty()) {
                const TopoDS_Face face = TopoDS::Face(edgeFaces.FindFromKey(edge).First());
                TopLoc_Location location;
                Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
                if (!triangulation.IsNull()) {
                    Handle(Poly_PolygonOnTriangulation) polygon =
                        BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
                    if (!polygon.IsNull()) {
                        const gp_Trsf trsf = location.Transformation();
                        for (int i = 1; i <= polygon->NbNodes(); ++i) {
                            const gp_Pnt p = triangulation->Node(polygon->Node(i)).Transformed(trsf);
                            polyline.points.insert(polyline.points.end(), {float(p.X()), float(p.Y()), float(p.Z())});
                        }
                    }
                }
            }
            if (polyline.points.empty())
                appendEdgeFromCurve(edge, deflection, params.angularDeflection, polyline);
            if (polyline.points.size() >= 6)
                mesh.edges.push_back(std::move(polyline));
        }
    } catch (const Standard_Failure& failure) {
        OS_LOG(Error, Kernel) << "tessellation failed: " << failure.DynamicType()->Name() << " "
                              << (failure.GetMessageString() ? failure.GetMessageString() : "");
    }

    OS_LOG(Debug, Render) << "tessellated " << mesh.triangleCount() << " triangles, " << mesh.edges.size() << " edges";
    return mesh;
}

} // namespace os::geom
