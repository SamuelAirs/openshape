// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string>

// How many logical pixels (points on iOS) make a millimeter on a screen:
// the ruler drawn at true size, the design shown at 1:1, a stroke traced
// around a part lying on the glass. iOS does not say how big its screens
// are, so FoldForm keeps a small table of devices (by their size in points
// and pixel ratio) and lets the owner check it against a card of known size.
namespace os::measure {

// A card of known size: the scale check, the fold's caliper and the photo's
// reference are built around one, a playing card.
struct CardSize {
    double longMm;
    double shortMm;
    double cornerRadiusMm;
    const char* name;
};
// A poker-size playing card (Bicycle and the like), the card FoldForm is
// built around (the owner's choice, 2026-11-04): in most homes, and nothing
// on it to keep out of a photo (a debit or credit card was the first choice,
// and asking people to photograph one costs trust). Makers cut 2.5 x 3.5 in;
// no standard publishes a tolerance, and quoted sizes differ by up to half a
// millimetre (docs/MEASURE.md, "Scale", TD-176). The corner radius only
// draws the outline.
inline constexpr CardSize kPlayingCard{88.9, 63.5, 3.5, "Playing card"};

enum class DeviceKind { Phone, Tablet, Desktop };

struct ScreenDescription {
    double widthPt = 0;  // the screen's logical size (either way round)
    double heightPt = 0;
    double devicePixelRatio = 1.0;
    DeviceKind kind = DeviceKind::Desktop;
    // What the system reports about the physical size (desktop monitors
    // say it through EDID); 0 when unknown.
    double physicalWidthMm = 0;
    double physicalHeightMm = 0;
};

struct ScaleEstimate {
    enum class Source {
        Calibrated,  // the owner matched a card (or a ruler) on this screen
        DeviceTable, // a known device's pixel density
        System,      // the size the system reports (desktop monitors)
        Fallback     // a guess: 96 logical px per inch on a desktop, 160 points on a phone
    };
    double pointsPerMm = 0;
    Source source = Source::Fallback;
    // Why this value, in a few words ("iPhone Duo, inner display (estimate)").
    std::string note;
};

// The best scale without calibration.
ScaleEstimate estimateScale(const ScreenDescription& screen);

// The key a calibration is stored under: the screen's size in points, long
// side first, and its pixel ratio ("951x669@3"). Turning the device keeps
// the key; the iPhone Duo's two displays get one each.
std::string screenKey(const ScreenDescription& screen);

// Points per millimeter from a calibration: a span of `pt` points on the
// screen matched `mm` millimeters of a real object (a card's long side,
// two marks on a ruler). nullopt-like 0 when the numbers make no sense.
double pointsPerMmFromSpan(double pt, double mm);

// Whether a scale is believable for a screen people hold or sit at (between
// about 50 and 300 points per inch); a calibration outside it is refused.
bool plausiblePointsPerMm(double pointsPerMm);

// Whether a point size is one of the iPhone Duo's two displays (466 x 678
// outer, 951 x 669 inner, either way round).
bool isIphoneDuoInner(double widthPt, double heightPt);
bool isIphoneDuoOuter(double widthPt, double heightPt);

} // namespace os::measure
