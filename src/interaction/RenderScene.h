#pragma once

#include "core/Camera.h"
#include "core/Uuid.h"
#include "geometry/Mesh.h"
#include "interaction/Manipulator.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace os::interact {

// Immutable description of what to draw, produced by the interaction layer
// and consumed by the renderer. Plain data: safe to copy to a render thread.
struct RenderBody {
    Uuid id;
    std::shared_ptr<const geom::Mesh> mesh;
    std::uint64_t meshKey = 0;   // changes whenever `mesh` content changes
    bool isPreview = false;      // showing an uncommitted operation result
    bool hovered = false;        // whole-body hover (body selection mode)
    bool selected = false;       // whole-body selection
    int hoverFace = -1;
    int hoverEdge = -1;
    std::vector<int> selectedFaces;
    std::vector<int> selectedEdges;
};

struct RenderArrow {
    Vec3 anchor;
    Vec3 direction;
    HandleState state = HandleState::Normal;
};

struct RenderGrid {
    bool visible = true;
    double minorStep = 1.0;  // mm
    double majorStep = 10.0; // mm
    Vec3 center;             // grid is drawn around this point on z = 0
    int halfLines = 60;      // minor lines on each side of the center
};

struct RenderScene {
    Camera camera;
    std::vector<RenderBody> bodies;
    std::vector<RenderArrow> arrows;
    ArrowStyle arrowStyle;
    RenderGrid grid;
};

} // namespace os::interact
