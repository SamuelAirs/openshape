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
    double linearDeflection = 0.05;   // mm; scaled by body size (see tessellate)
    double angularDeflection = 0.35;  // radians (~20 degrees)
    bool relative = false;
};

} // namespace os::geom
