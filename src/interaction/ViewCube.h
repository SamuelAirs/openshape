// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions adapted from OpenShape3D (https://github.com/laanlabs/openshape3d,
// commit be3f2fc), Copyright (c) 2026 Laan Labs and the openshape3d
// contributors, MIT License (third_party/openshape3d/LICENSE).

#pragma once

#include "core/Camera.h"
#include "core/Math.h"

#include <array>

namespace os::interact {

// The view cube in a corner of the view: a small cube that turns with the
// camera (rotation only, orthographic, whatever the main projection), its
// faces named after the standard views. Tapping a face looks at it head-on
// (the standard view of that name), tapping a corner looks down that corner's
// diagonal (an isometric view), and a drag that starts on it orbits the view.
//
// Pure layout and hit math, shared by drawing (AxisTriad.qml) and taps.
// Positions are in "box" coordinates: the cube's square on screen spans
// [-1, 1] in both directions, u to the right and v down (the item's center is
// 0, 0). The cube is [-1, 1]^3 in world orientation (Z up); the box shows
// [-kViewHalfExtent, kViewHalfExtent]: the outline head-on and in the
// isometric views (corners sqrt(8/3) from the center) fits, and the
// farthest a corner ever gets (sqrt(3), looking square across a diagonal)
// overshoots the box by 5 % of its half size (2 px; the controls around it
// keep 8 px away).
struct ViewCubeFace {
    StandardView view = StandardView::Front; // Front, Back, Left, Right, Top or Bottom
    const char* name = "";                   // "Front", ... (the View menu's names)
    Vec3 normal;
    // Turned toward the viewer (normal . camera backward); drawn when > 0.
    double facing = 0;
    // The face as the image of its label square (reading upright from its
    // standard view): the square's top-left corner, and where its top edge
    // (left to right) and left edge (top to bottom) go. Orthographic, so this
    // affine map is exact for the whole face.
    Vec2 origin, across, down;
    // The label fades in as the face turns toward the viewer: 0 (hidden)
    // below kLabelMinFacing, full from about two thirds of the way to head-on.
    double labelOpacity = 0;
    // The ViewCubeTarget ids of the face's corners, in the label square's
    // order: top-left, top-right, bottom-right, bottom-left.
    std::array<int, 4> cornerIds{};
};

// One of the twelve edges, tinted by the world axis it runs along.
struct ViewCubeEdge {
    int axis = 0; // 0 X, 1 Y, 2 Z
    Vec2 from, to;
    bool visible = false; // one of its two faces is turned toward the viewer
};

struct ViewCubeLayout {
    std::array<ViewCubeFace, 6> faces; // in StandardView order (Front .. Bottom)
    std::array<ViewCubeEdge, 12> edges;
};

// What a tap on the cube reaches.
struct ViewCubeTarget {
    enum class Kind { None, Face, Corner };
    Kind kind = Kind::None;
    StandardView face = StandardView::Front; // Kind::Face
    Vec3 corner;                             // Kind::Corner: (+-1, +-1, +-1)
    // The view it asks for: unit, from the target towards the eye.
    Vec3 direction;

    // -1 none; 0..5 a face (its StandardView); 6..13 a corner,
    // 6 + (x > 0) + 2 (y > 0) + 4 (z > 0).
    int id() const;
};

class ViewCube {
public:
    static constexpr double kHalfExtent = 1.0;
    static constexpr double kViewHalfExtent = 1.65;
    // A hit whose three coordinates are all beyond this fraction of the half
    // extent is a corner: the corner squares are 0.225 of a face's side.
    static constexpr double kCornerFraction = 0.55;
    // A face must face the viewer at least this much (about 75 degrees off
    // head-on) to be named: the default isometric's faces face it 0.58.
    static constexpr double kLabelMinFacing = 0.25;

    static ViewCubeLayout layout(const Camera& camera);
    // Where a point of the cube (world orientation) appears, in box coordinates.
    static Vec2 project(const Camera& camera, const Vec3& cubePoint);
    // The face or corner under `box` (box coordinates); Kind::None off the cube.
    static ViewCubeTarget hit(const Camera& camera, Vec2 box);
    // The corner id (6..13) of the cube corner with these signs.
    static int cornerId(const Vec3& corner);
};

} // namespace os::interact
