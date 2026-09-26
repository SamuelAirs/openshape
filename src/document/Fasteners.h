// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Screw and insert dimensions, all in one place (the sources are named in
// Fasteners.cpp). Millimeters.
namespace os::doc {

// A metric screw size and the holes that go with it.
struct ScrewSize {
    const char* name;           // "M3"
    double nominal;             // 3.0
    double clearanceClose;      // ISO 273 fine series
    double clearanceNormal;     // ISO 273 medium series
    double tapDrill;            // ISO 2306 (d - P): cut threads, or self-tap into printed plastic
    double counterboreDiameter; // DIN 974-1 row 1, for ISO 4762 socket head cap screws
    double counterboreDepth;    // ISO 4762 head height k + 0.4 mm: the head sits just below the surface
    double countersinkDiameter; // ISO 10642 head diameter (theoretical dk; M2/M2.5 from ISO 7046-1)
};
// M2, M2.5, M3, M4, M5, M6 (in that order).
const std::vector<ScrewSize>& metricScrews();
// Index of the M3 row (the default everywhere).
constexpr std::size_t kDefaultScrew = 2;
// Countersunk heads of metric screws (ISO 10642, ISO 7046): 90 degrees.
constexpr double kCountersinkAngleDegrees = 90.0;

// Typical pilot holes for brass heat-set inserts (community rules of thumb,
// not a standard: check your insert's datasheet).
struct InsertPreset {
    const char* name; // "M3"
    double diameter;  // mm
    double depth;     // mm
};
const std::vector<InsertPreset>& heatSetInsertPresets();

// What a hole made for a screw is for (the Hole tool's size presets).
enum class HoleFit { Close, Normal, Tap };
double holeDiameterFor(const ScrewSize& screw, HoleFit fit);
// "M3 close fit", "M3 normal fit", "M3 tap".
std::string holeFitLabel(const ScrewSize& screw, HoleFit fit);

} // namespace os::doc
