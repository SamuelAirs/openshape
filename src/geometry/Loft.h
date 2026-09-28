// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <vector>

// Loft: a solid through closed profiles on different planes.
namespace os::geom {

// Joins flat profiles (region faces, e.g. from findRegions) in the order
// given into one solid whose end faces are the first and the last profile
// (BRepOffsetAPI_ThruSections). `ruled` = Straight: flat or ruled faces from
// each profile to the next, with a crease at every profile in between;
// otherwise Smooth: one smooth surface through all of them (with two
// profiles it is the straight one). Profiles may have different numbers of
// edges (a square to a circle). Holes are lofted through when every profile
// has the same number of them (each paired with the nearest one, measured
// from its profile's middle, in the profile before).
//
// Refused with a plain message: fewer than two profiles, a profile that is
// not flat or is not one piece, two consecutive profiles in the same plane,
// different numbers of holes, and a result that is not a sound solid: one
// whose surface crosses itself (a twist, profiles that cut through each
// other), whose end faces are not exactly the first and last profile, or
// where a hole's loft breaks through the outside.
Result<Shape> loftFaces(const std::vector<Shape>& profiles, bool ruled);

} // namespace os::geom
