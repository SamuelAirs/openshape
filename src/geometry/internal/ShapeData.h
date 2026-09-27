// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// Private to the geometry library. Do not include from other layers.

#include "geometry/Shape.h"

#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Shape.hxx>


namespace os::geom {

struct ShapeData {
    TopoDS_Shape shape;
    // Topology index maps (OCCT maps are 1-based; our public indices are 0-based).
    TopTools_IndexedMapOfShape faces;
    TopTools_IndexedMapOfShape edges;
    TopTools_IndexedMapOfShape vertices;
    TopTools_IndexedMapOfShape solids;
    // Tight bounding box, computed on first use (the shape never changes);
    // guarded by the kernel lock (boundingBox()).
    mutable bool tightBoxDone = false;
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
