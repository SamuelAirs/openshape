// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/WindowMetrics.h"

#include <algorithm>
#include <cmath>

namespace os::ui {
namespace {

// A number as short as it can be: 84, 2.5, 0.33.
QString shortNumber(double value)
{
    return QString::number(value, 'g', 6);
}

bool same(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

} // namespace

QStringList windowMetricsChanges(const WindowMetrics& before, const WindowMetrics& after)
{
    QStringList changes;
    if (before.width != after.width || before.height != after.height)
        changes << QStringLiteral("size");
    if (!same(before.devicePixelRatio, after.devicePixelRatio))
        changes << QStringLiteral("pixel ratio");
    if (!same(before.safeTop, after.safeTop) || !same(before.safeRight, after.safeRight) || !same(before.safeBottom, after.safeBottom)
        || !same(before.safeLeft, after.safeLeft) || before.safeAreaSimulated != after.safeAreaSimulated)
        changes << QStringLiteral("safe area");
    if (before.layout != after.layout)
        changes << QStringLiteral("layout");
    if (before.screenName != after.screenName || before.screenWidth != after.screenWidth || before.screenHeight != after.screenHeight
        || !same(before.screenDevicePixelRatio, after.screenDevicePixelRatio))
        changes << QStringLiteral("screen");
    return changes;
}

QString describeWindowMetrics(const WindowMetrics& m, const QStringList& changes)
{
    const double usableWidth = std::max(0.0, m.width - m.safeLeft - m.safeRight);
    const double usableHeight = std::max(0.0, m.height - m.safeTop - m.safeBottom);
    QString line = QStringLiteral("window %1x%2 pt at %3x (%4x%5 px), %6safe area top %7 right %8 bottom %9 left %10, usable %11x%12 pt")
                       .arg(m.width)
                       .arg(m.height)
                       .arg(shortNumber(m.devicePixelRatio))
                       .arg(std::lround(m.width * m.devicePixelRatio))
                       .arg(std::lround(m.height * m.devicePixelRatio))
                       .arg(m.safeAreaSimulated ? QStringLiteral("simulated ") : QString())
                       .arg(shortNumber(m.safeTop), shortNumber(m.safeRight), shortNumber(m.safeBottom), shortNumber(m.safeLeft))
                       .arg(shortNumber(usableWidth), shortNumber(usableHeight));
    if (!m.layout.isEmpty())
        line += QStringLiteral(", %1 layout").arg(m.layout);
    line += QStringLiteral("; screen \"%1\" %2x%3 pt at %4x")
                .arg(m.screenName)
                .arg(m.screenWidth)
                .arg(m.screenHeight)
                .arg(shortNumber(m.screenDevicePixelRatio));
    if (!changes.isEmpty())
        line += QStringLiteral(" (changed: %1)").arg(changes.join(QStringLiteral(", ")));
    return line;
}

} // namespace os::ui
