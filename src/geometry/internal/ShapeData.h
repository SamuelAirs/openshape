#pragma once

// Private to the geometry library. Do not include from other layers.

#include "geometry/Shape.h"

#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>

#include <mutex>

namespace os::geom {

struct ShapeData {
    TopoDS_Shape shape;
    // Topology index maps (OCCT maps are 1-based; our public indices are 0-based).
    TopTools_IndexedMapOfShape faces;
    TopTools_IndexedMapOfShape edges;
    TopTools_IndexedMapOfShape vertices;
    TopTools_IndexedMapOfShape solids;
    // Tight bounding box, computed on first use (the shape never changes).
    mutable std::once_flag tightBoxOnce;
    mutable BoundingBox tightBox;
};

// Wraps a kernel shape, building the topology index maps once.
Shape makeShape(const TopoDS_Shape& shape);

inline const TopoDS_Shape& occ(const Shape& s)
{
    static const TopoDS_Shape nullShape;
    return s.data() ? s.data()->shape : nullShape;
}

} // namespace os::geom
