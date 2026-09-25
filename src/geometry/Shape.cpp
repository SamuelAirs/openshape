// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/Shape.h"
#include "geometry/internal/ShapeData.h"

#include <TopExp.hxx>
#include <TopAbs_ShapeEnum.hxx>

namespace os::geom {

Shape makeShape(const TopoDS_Shape& shape)
{
    auto data = std::make_shared<ShapeData>();
    data->shape = shape;
    if (!shape.IsNull()) {
        TopExp::MapShapes(shape, TopAbs_FACE, data->faces);
        TopExp::MapShapes(shape, TopAbs_EDGE, data->edges);
        TopExp::MapShapes(shape, TopAbs_VERTEX, data->vertices);
        TopExp::MapShapes(shape, TopAbs_SOLID, data->solids);
    }
    return Shape(std::move(data));
}

bool Shape::isNull() const
{
    return !data_ || data_->shape.IsNull();
}

int Shape::faceCount() const { return data_ ? data_->faces.Extent() : 0; }
int Shape::edgeCount() const { return data_ ? data_->edges.Extent() : 0; }
int Shape::vertexCount() const { return data_ ? data_->vertices.Extent() : 0; }
int Shape::solidCount() const { return data_ ? data_->solids.Extent() : 0; }

} // namespace os::geom
