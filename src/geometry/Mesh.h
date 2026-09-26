// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <cstdint>
#include <vector>

namespace os::geom {

// Display/picking mesh derived from an exact Shape. Never authoritative:
// it is regenerated whenever the shape changes.
struct Mesh {
    // Interleaved-free arrays; one entry per vertex.
    std::vector<float> positions; // xyz
    std::vector<float> normals;   // xyz
    std::vector<std::uint32_t> indices; // 3 per triangle
    std::vector<std::uint32_t> triangleFace; // topology face index per triangle
    // Triangles of face f are [faceTriangleOffset[f], faceTriangleOffset[f+1]).
    // Faces are emitted contiguously, so highlights can draw index sub-ranges.
    std::vector<std::uint32_t> faceTriangleOffset;

    struct EdgePolyline {
        int edgeIndex = -1;
        std::vector<float> points; // xyz, consecutive points form segments
    };
    std::vector<EdgePolyline> edges;

    int faceCount() const { return faceTriangleOffset.empty() ? 0 : static_cast<int>(faceTriangleOffset.size()) - 1; }
    std::size_t vertexCount() const { return positions.size() / 3; }
    std::size_t triangleCount() const { return indices.size() / 3; }
    Vec3 vertex(std::size_t i) const { return {positions[3 * i], positions[3 * i + 1], positions[3 * i + 2]}; }
};

struct TessellationParams {
    double linearDeflection = 0.05;   // mm (ignored when `adaptive`)
    double angularDeflection = 0.35;  // radians (~20 degrees)
    bool relative = false;
    // Display meshes pick the deflection from the body size; exports use a
    // fixed, fine `linearDeflection` instead.
    bool adaptive = true;
    // Mesh a copy of the shape's topology (geometry and existing meshes are
    // shared, not copied), so the shape - and the shapes it shares faces and
    // edges with - get no triangulations or edge polygons. For previews: a
    // preview result shares most faces and edges with the document's body,
    // and meshing it in place piled up polygons on the body's edges with
    // every preview (40 previews of a pushed face made a filleted cube's
    // BRep text 14 times larger, and the project file's geometry cache with it).
    bool isolated = false;
};

} // namespace os::geom
