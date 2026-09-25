// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "document/Feature.h"
#include "geometry/Profiles.h"
#include "sketch/Sketch.h"

#include <optional>
#include <vector>

// Bridges sketches (2D model) and profile geometry (3D kernel regions).
namespace os::doc {

geom::PlaneFrame planeFrame(const sketch::Plane& plane);

// Non-construction sketch curves in world coordinates, on `plane`.
std::vector<geom::PlanarCurve> worldCurves(const sketch::Sketch& sketch, const sketch::Plane& plane);

// Closed regions of a sketch, largest first (on its stored plane, or on `plane`).
Result<std::vector<geom::Region>> sketchRegions(const sketch::Sketch& sketch);
Result<std::vector<geom::Region>> sketchRegions(const sketch::Sketch& sketch, const sketch::Plane& plane);

// Region index a reference points at: the region containing its interior point.
std::optional<int> resolveProfile(const std::vector<geom::Region>& regions, const sketch::Sketch& sketch,
                                  const ProfileRef& ref);
std::optional<int> resolveProfile(const std::vector<geom::Region>& regions, const sketch::Plane& plane,
                                  const ProfileRef& ref);

// The plane a sketch lies on right now: for a sketch attached to a face, the
// face's current plane (when it still resolves), otherwise the stored plane.
// During a body recompute only features before the one being evaluated count.
sketch::Plane effectivePlane(const sketch::Sketch& sketch, const EvalContext& context);

// Captures an attachment to face `faceIndex` of the output of `featureId`.
std::optional<sketch::Attachment> makeAttachment(const Body& body, const Uuid& featureId, int faceIndex);

ProfileRef makeProfileRef(const geom::Region& region, const sketch::Sketch& sketch);

} // namespace os::doc
