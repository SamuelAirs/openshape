// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Camera.h"
#include "core/Uuid.h"
#include "geometry/Mesh.h"
#include "interaction/Manipulator.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
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
    std::vector<int> highlightFaces; // faces of the step highlighted in the model panel
};

struct RenderArrow {
    Vec3 anchor;
    Vec3 direction;
    HandleState state = HandleState::Normal;
    int axis = -1; // -1 accent color, 0/1/2 = X/Y/Z colors
};

// A rotation ring, as a closed polyline in world space (sized for the camera).
struct RenderRing {
    std::vector<Vec3> points;
    Vec3 marker; // on the ring at the current angle
    HandleState state = HandleState::Normal;
    int axis = 2; // 0/1/2 = X/Y/Z colors
};

// The ground grid on z = 0, drawn per pixel (render/shaders/grid.frag): its
// lines fade out between half the radius and the radius, so it has no hard
// border; the X/Y/Z axes fade out further away.
struct RenderGrid {
    bool visible = true;
    double minorStep = 1.0;   // mm
    double majorStep = 10.0;  // mm
    Vec3 center;              // on a major line, on z = 0
    double radius = 60.0;     // mm from the center
    double axisRadius = 90.0; // mm from the center
    // Perspective: the grid also fades with distance from the eye, between
    // these distances (0 = not at all).
    double eyeFadeStart = 0;
    double eyeFadeEnd = 0;
};

// The X (0), Y (1) or Z (2) axis line through the origin as drawn with
// `grid`: where it crosses the axes' disc around the grid's center (the Z
// axis as high and as deep as the others reach). None when the disc misses
// it. Shared by the renderer and by picking the origin axes.
inline std::optional<std::pair<Vec3, Vec3>> originAxisSegment(const RenderGrid& grid, int axis)
{
    const double r = grid.axisRadius;
    const Vec3 c = grid.center;
    const double off = axis == 0 ? std::abs(c.y) : axis == 1 ? std::abs(c.x) : std::hypot(c.x, c.y);
    if (off >= r)
        return std::nullopt;
    const double half = std::sqrt(r * r - off * off);
    if (axis == 0)
        return std::pair{Vec3{c.x - half, 0, 0}, Vec3{c.x + half, 0, 0}};
    if (axis == 1)
        return std::pair{Vec3{0, c.y - half, 0}, Vec3{0, c.y + half, 0}};
    return std::pair{Vec3{0, 0, -half}, Vec3{0, 0, half}};
}

// ---- Sketches ----
enum class SketchStyle {
    Normal,       // under-constrained geometry
    Defined,      // fully constrained geometry
    Construction,
    Hovered,
    Selected,
    Preview,      // rubber band of the active drawing tool
    Guide,        // inference / alignment guide
    Dimension,    // dimension and extension lines
    Measure,      // a size measured in 3D (push/pull thickness), drawn with the arrow
    Conflict,     // over-constrained
    Reference,    // construction axes and planes
};

struct RenderSketchLine {
    Vec3 a, b;
    SketchStyle style = SketchStyle::Normal;
};

struct RenderSketchPoint {
    Vec3 position;
    SketchStyle style = SketchStyle::Normal;
};

// A closed profile region, drawn as a translucent fill.
struct RenderRegion {
    std::shared_ptr<const geom::Mesh> mesh;
    std::uint64_t meshKey = 0;
    SketchStyle style = SketchStyle::Normal; // Normal, Hovered or Selected
    bool reference = false; // a construction plane's fill (its own color)
};

struct RenderSketch {
    bool editing = false; // the sketch being edited draws on top of everything
    std::vector<RenderSketchLine> lines;
    std::vector<RenderSketchPoint> points;
    std::vector<RenderRegion> regions;
};

struct RenderScene {
    Camera camera;
    std::vector<RenderBody> bodies;
    std::vector<RenderSketch> sketches;
    std::vector<RenderArrow> arrows;
    std::vector<RenderRing> rings;
    ArrowStyle arrowStyle;
    RenderGrid grid;
};

} // namespace os::interact
