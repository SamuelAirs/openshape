// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/Fasteners.h"

namespace os::doc {

// Sources (millimeters):
// - Clearance holes: ISO 273 (fine = "close fit", medium = "normal fit").
// - Tap drills: ISO 2306 / the usual d - P for coarse threads (M2.5: 2.05,
//   rounded to 2.1 as drilled); also a good pilot for screws that cut their
//   own thread in printed plastic.
// - Counterbores: socket head cap screws ISO 4762 (head dk max 3.8 / 4.5 /
//   5.5 / 7 / 8.5 / 10, height k = d); counterbore diameter from DIN 974-1,
//   row 1 (ISO 4762 without washer); depth = k + 0.4 mm so the head ends
//   just below the surface.
// - Countersinks: hexagon socket countersunk head screws ISO 10642 (90
//   degrees, theoretical head diameter dk 6.72 / 8.96 / 11.20 / 13.44 for
//   M3-M6); ISO 10642 starts at M3, so M2 and M2.5 use the same head form
//   from ISO 7046-1 (dk 4.4 / 5.5).
const std::vector<ScrewSize>& metricScrews()
{
    static const std::vector<ScrewSize> sizes{
        // name, d, close, normal, tap, counterbore d, counterbore depth, countersink d
        {"M2", 2.0, 2.2, 2.4, 1.6, 4.4, 2.4, 4.4},
        {"M2.5", 2.5, 2.7, 2.9, 2.1, 5.5, 2.9, 5.5},
        {"M3", 3.0, 3.2, 3.4, 2.5, 6.5, 3.4, 6.72},
        {"M4", 4.0, 4.3, 4.5, 3.3, 8.0, 4.4, 8.96},
        {"M5", 5.0, 5.3, 5.5, 4.2, 10.0, 5.4, 11.2},
        {"M6", 6.0, 6.4, 6.6, 5.0, 11.0, 6.4, 13.44},
    };
    return sizes;
}

// Heat-set inserts: community rules of thumb for brass inserts in printed
// parts (pilot diameter, depth), not a standard; check the insert's datasheet.
const std::vector<InsertPreset>& heatSetInsertPresets()
{
    static const std::vector<InsertPreset> presets{
        {"M2", 3.2, 4.0}, {"M2.5", 3.6, 5.0}, {"M3", 4.0, 6.0}, {"M4", 5.6, 8.5}, {"M5", 6.4, 10.0}};
    return presets;
}

double holeDiameterFor(const ScrewSize& screw, HoleFit fit)
{
    switch (fit) {
    case HoleFit::Close: return screw.clearanceClose;
    case HoleFit::Normal: return screw.clearanceNormal;
    case HoleFit::Tap: return screw.tapDrill;
    }
    return screw.clearanceNormal;
}

std::string holeFitLabel(const ScrewSize& screw, HoleFit fit)
{
    switch (fit) {
    case HoleFit::Close: return std::string(screw.name) + " close fit";
    case HoleFit::Normal: return std::string(screw.name) + " normal fit";
    case HoleFit::Tap: return std::string(screw.name) + " tap";
    }
    return screw.name;
}

} // namespace os::doc
