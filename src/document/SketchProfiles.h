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

// Non-construction sketch curves in world coordinates.
std::vector<geom::PlanarCurve> worldCurves(const sketch::Sketch& sketch);

// Closed regions of a sketch, largest first.
Result<std::vector<geom::Region>> sketchRegions(const sketch::Sketch& sketch);

// Region index a reference points at: the region containing its interior
// point (smallest such region if nested), else nullopt.
std::optional<int> resolveProfile(const std::vector<geom::Region>& regions, const sketch::Sketch& sketch,
                                  const ProfileRef& ref);

ProfileRef makeProfileRef(const geom::Region& region, const sketch::Sketch& sketch);

} // namespace os::doc
