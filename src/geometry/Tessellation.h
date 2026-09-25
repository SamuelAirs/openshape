#pragma once

#include "geometry/Mesh.h"
#include "geometry/Shape.h"

namespace os::geom {

// Builds a display mesh. The linear deflection is clamped relative to the
// shape's bounding box so tiny parts stay smooth and huge parts stay cheap.
Mesh tessellate(const Shape& shape, const TessellationParams& params = {});

} // namespace os::geom
