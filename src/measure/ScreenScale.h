// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string>

// How many logical pixels (points on iOS) make a millimeter on a screen:
// the ruler drawn at true size, the design shown at 1:1, a stroke traced
// around a part lying on the glass. iOS does not say how big its screens
// are, so FoldForm keeps a small table of devices (by their size in points
// and pixel ratio) and lets the owner check it against a bank card, the one
// object of a standard size (ISO/IEC 7810 ID-1) nearly everyone carries.
namespace os::measure {

// ISO/IEC 7810 ID-1: bank, credit and ID cards.
inline constexpr double kCardWidthMm = 85.60;
inline constexpr double kCardHeightMm = 53.98;
inline constexpr double kCardCornerRadiusMm = 3.18;

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
