// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/ScreenScale.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace os::measure {

namespace {

constexpr double kMmPerInch = 25.4;

bool sameSize(double w, double h, double a, double b)
{
    const double lo = std::min(w, h), hi = std::max(w, h);
    return std::abs(lo - std::min(a, b)) < 0.5 && std::abs(hi - std::max(a, b)) < 0.5;
}

ScaleEstimate fromPointsPerInch(double pointsPerInch, ScaleEstimate::Source source, std::string note)
{
    return {pointsPerInch / kMmPerInch, source, std::move(note)};
}

} // namespace

bool isIphoneDuoInner(double widthPt, double heightPt) { return sameSize(widthPt, heightPt, 951, 669); }
bool isIphoneDuoOuter(double widthPt, double heightPt) { return sameSize(widthPt, heightPt, 466, 678); }

bool plausiblePointsPerMm(double pointsPerMm)
{
    // 50 to 300 points per inch: from a large monitor at 100 % to the
    // densest phone at its smallest point size.
    return std::isfinite(pointsPerMm) && pointsPerMm >= 50.0 / kMmPerInch && pointsPerMm <= 300.0 / kMmPerInch;
}

double pointsPerMmFromSpan(double pt, double mm)
{
    if (!(pt > 0) || !(mm > 0) || !std::isfinite(pt) || !std::isfinite(mm))
        return 0;
    return pt / mm;
}

std::string screenKey(const ScreenDescription& screen)
{
    const long hi = std::lround(std::max(screen.widthPt, screen.heightPt));
    const long lo = std::lround(std::min(screen.widthPt, screen.heightPt));
    char ratio[32];
    std::snprintf(ratio, sizeof ratio, "%g", std::round(screen.devicePixelRatio * 100.0) / 100.0);
    return std::to_string(hi) + "x" + std::to_string(lo) + "@" + ratio;
}

ScaleEstimate estimateScale(const ScreenDescription& screen)
{
    const double w = screen.widthPt, h = screen.heightPt;
    const double dpr = screen.devicePixelRatio;
    switch (screen.kind) {
    case DeviceKind::Phone:
        // The iPhone Duo: 7.6-inch inner and 5.4-inch outer displays, both
        // about 458 pixels per inch at 3x (from the diagonals Apple gives,
        // so an estimate until a card says otherwise).
        if (isIphoneDuoInner(w, h))
            return fromPointsPerInch(458.0 / 3.0, ScaleEstimate::Source::DeviceTable, "iPhone Duo, inner display (estimate)");
        if (isIphoneDuoOuter(w, h))
            return fromPointsPerInch(458.0 / 3.0, ScaleEstimate::Source::DeviceTable, "iPhone Duo, outer display (estimate)");
        // Every 3x iPhone since the X is 458 to 460 pixels per inch (the
        // minis and the Plus models of old draw their points smaller or
        // larger, within about 8 %); the 2x ones are 326.
        if (dpr >= 2.5)
            return fromPointsPerInch(460.0 / 3.0, ScaleEstimate::Source::DeviceTable, "iPhone, 3x display");
        if (dpr >= 1.5)
            return fromPointsPerInch(326.0 / 2.0, ScaleEstimate::Source::DeviceTable, "iPhone, 2x display");
        return fromPointsPerInch(160.0, ScaleEstimate::Source::Fallback, "phone (guess)");
    case DeviceKind::Tablet:
        // iPad mini (744 x 1133 points) is 326 pixels per inch at 2x;
        // every other iPad 264.
        if (sameSize(w, h, 744, 1133))
            return fromPointsPerInch(326.0 / 2.0, ScaleEstimate::Source::DeviceTable, "iPad mini");
        return fromPointsPerInch(264.0 / 2.0, ScaleEstimate::Source::DeviceTable, "iPad");
    case DeviceKind::Desktop:
        if (screen.physicalWidthMm > 0 && screen.physicalHeightMm > 0) {
            const double across = std::max(w, h) / std::max(screen.physicalWidthMm, screen.physicalHeightMm);
            if (plausiblePointsPerMm(across))
                return {across, ScaleEstimate::Source::System, "the size the monitor reports"};
        }
        return fromPointsPerInch(96.0, ScaleEstimate::Source::Fallback, "a typical monitor (guess)");
    }
    return fromPointsPerInch(160.0, ScaleEstimate::Source::Fallback, "guess");
}

} // namespace os::measure
