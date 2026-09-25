// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "geometry/Mesh.h"
#include "geometry/Shape.h"

namespace os::geom {

// Builds a display mesh. The linear deflection is clamped relative to the
// shape's bounding box so tiny parts stay smooth and huge parts stay cheap.
Mesh tessellate(const Shape& shape, const TessellationParams& params = {});

} // namespace os::geom
