// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "sketch/Sketch.h"

#include <optional>
#include <vector>

// Edits behind the sketch tools that change existing geometry (slot, corner
// fillet, trim). Pure functions of the sketch: they add, move and remove
// entities and constraints but never solve; the caller solves and commits.
namespace os::sketch {

struct SlotIds {
    EntityId centers[2]{};
    EntityId lines[2]{}; // along +side, along -side of the axis
    EntityId arcs[2]{};  // around centers[0], around centers[1]
};
// A slot (a stadium): two lines tangent to two equal arcs around the centers
// `a` and `b`. Existing center points can be reused.
SlotIds addSlot(Sketch& sketch, Vec2 a, Vec2 b, double radius, EntityId reuseA = kNoEntity, EntityId reuseB = kNoEntity);

struct CenterRectangleIds {
    RectangleIds rectangle; // corners[0] is opposite `corner`, corners[2] is `corner`
    EntityId center = kNoEntity;
    EntityId diagonal = kNoEntity; // construction line corners[0] -> corners[2]
};
// An axis-aligned rectangle around `center` with one corner at `corner`. The
// center stays the midpoint of a construction diagonal, so the rectangle
// stays centered when its width or height changes. An existing center point
// (e.g. the origin) can be reused.
CenterRectangleIds addCenterRectangle(Sketch& sketch, Vec2 center, Vec2 corner, EntityId reuseCenter = kNoEntity);

struct PolygonIds {
    EntityId center = kNoEntity;
    EntityId outer = kNoEntity; // construction circle through the corners
    EntityId inner = kNoEntity; // construction circle touching the sides: its diameter is the size across flats
    std::vector<EntityId> corners;
    std::vector<EntityId> sides; // sides[0] has its middle at `sideMiddle`
};
// A regular polygon around `center` with the middle of one side at
// `sideMiddle` (counterclockwise corners). It stays regular under edits: the
// corners lie on the outer construction circle and all sides are equal. The
// inner circle touches the first side, so a diameter on it sizes the polygon
// across flats. An existing center point can be reused.
inline constexpr int kMinPolygonSides = 3;
inline constexpr int kMaxPolygonSides = 64;
PolygonIds addPolygon(Sketch& sketch, Vec2 center, Vec2 sideMiddle, int sides, EntityId reuseCenter = kNoEntity);
// The corners addPolygon would create (for previews).
std::vector<Vec2> polygonCorners(Vec2 center, Vec2 sideMiddle, int sides);

// Rounds the corner where exactly two lines meet at `corner` with a tangent
// arc. The corner point stays, on both lines' extensions (the "virtual
// sharp"), so dimensions to it keep working. Returns the new arc.
Result<EntityId> filletCorner(Sketch& sketch, EntityId corner, double radius);
// A friendly default radius for a corner: about a quarter of the shorter line.
std::optional<double> suggestedFilletRadius(const Sketch& sketch, EntityId corner);

// Removes the piece of a line, circle or arc around `at` that lies between
// its nearest crossings with other curves (or its own ends): a circle
// becomes an arc, a line or arc may split in two. New ends are kept on the
// curves they meet (point-on-line / point-on-circle constraints).
Status trimAt(Sketch& sketch, EntityId curve, Vec2 at);
// The piece trimAt would remove, as a polyline (empty when it cannot trim).
std::vector<Vec2> trimPreview(const Sketch& sketch, EntityId curve, Vec2 at);

} // namespace os::sketch
