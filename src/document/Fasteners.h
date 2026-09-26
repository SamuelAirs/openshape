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

// FDM print allowance: a printed hole comes out smaller than drawn (the
// outer perimeter bulges inward), so presets for holes a screw passes
// through, and for its head's seat, add this to their diameter
// (Preferences, "Hole allowance for 3D printing"; mm, 0 = the standard
// values). Tap drills and heat-set insert pilots already assume printed
// plastic and never get it; typed diameters are used as typed.
inline constexpr double kDefaultHoleAllowance = 0.2;
inline constexpr double kMaxHoleAllowance = 1.0;
// A stored or typed allowance within [0, kMaxHoleAllowance]; anything else
// (negative, too large, not a number) is the default.
double validHoleAllowance(double mm);

// What a hole made for a screw is for (the Hole tool's size presets).
enum class HoleFit { Close, Normal, Tap };
// The preset diameter: the ISO 273 clearance plus `allowance` (close /
// normal fit), or the tap drill as it is.
double holeDiameterFor(const ScrewSize& screw, HoleFit fit, double allowance);
// Screw head seats, the allowance added to their diameter.
double counterboreDiameterFor(const ScrewSize& screw, double allowance);
double countersinkDiameterFor(const ScrewSize& screw, double allowance);
// "M3 close fit", "M3 normal fit", "M3 tap"; with an allowance the
// clearance fits say so ("M3 close fit +0.2 mm").
std::string holeFitLabel(const ScrewSize& screw, HoleFit fit, double allowance = 0.0);
// " +0.2 mm" (nothing for 0): how a preset name mentions the allowance.
std::string allowanceSuffix(double allowance);

} // namespace os::doc
