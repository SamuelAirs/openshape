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
