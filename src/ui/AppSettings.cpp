// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/AppSettings.h"

#include <QtCore/QSettings>

#include <algorithm>

namespace os::ui {

namespace {
const QString kDefaultUnit = QStringLiteral("preferences/defaultUnit");
const QString kGridSnap = QStringLiteral("preferences/sketchGridSnap");
const QString kRecoveryInterval = QStringLiteral("preferences/recoveryIntervalSeconds");
const QString kRecentFiles = QStringLiteral("recentFiles");
const QString kWindowFrame = QStringLiteral("window/frame");
const QString kWindowClient = QStringLiteral("window/client");
const QString kWindowMaximized = QStringLiteral("window/maximized");
} // namespace

Preferences loadPreferences(QSettings& settings)
{
    Preferences p;
    const QString unit = settings.value(kDefaultUnit).toString();
    if (unit == QLatin1String("in"))
        p.defaultUnit = LengthUnit::Inch;
    const QVariant snap = settings.value(kGridSnap);
    if (snap.isValid())
        p.sketchGridSnap = snap.toBool();
    bool ok = false;
    const int interval = settings.value(kRecoveryInterval).toInt(&ok);
    if (ok && std::find(kRecoveryIntervals.begin(), kRecoveryIntervals.end(), interval) != kRecoveryIntervals.end())
        p.recoveryIntervalSeconds = interval;
    return p;
}

void savePreferences(QSettings& settings, const Preferences& p)
{
    settings.setValue(kDefaultUnit, p.defaultUnit == LengthUnit::Inch ? QStringLiteral("in") : QStringLiteral("mm"));
    settings.setValue(kGridSnap, p.sketchGridSnap);
    settings.setValue(kRecoveryInterval, p.recoveryIntervalSeconds);
}

QStringList loadRecentFiles(QSettings& settings)
{
    QStringList files = settings.value(kRecentFiles).toStringList();
    files.removeAll(QString());
    return files;
}

void saveRecentFiles(QSettings& settings, const QStringList& files)
{
    settings.setValue(kRecentFiles, files);
}

std::optional<WindowPlacement> loadWindowPlacement(QSettings& settings)
{
    const QRect frame = settings.value(kWindowFrame).toRect();
    const QSize client = settings.value(kWindowClient).toSize();
    if (!frame.isValid() || !client.isValid() || client.width() > frame.width() || client.height() > frame.height())
        return std::nullopt;
    return WindowPlacement{frame, client, settings.value(kWindowMaximized).toBool()};
}

void saveWindowPlacement(QSettings& settings, const WindowPlacement& placement)
{
    settings.setValue(kWindowFrame, placement.frame);
    settings.setValue(kWindowClient, placement.client);
    settings.setValue(kWindowMaximized, placement.maximized);
}

QRect fitToScreens(const QRect& frame, const QList<QRect>& availableScreens, QSize minimum)
{
    if (!frame.isValid() || availableScreens.isEmpty())
        return {};
    QRect screen = availableScreens.front();
    long long best = 0;
    for (const QRect& s : availableScreens) {
        const QRect overlap = s.intersected(frame);
        const long long area = overlap.isValid() ? 1LL * overlap.width() * overlap.height() : 0;
        if (area > best) {
            best = area;
            screen = s;
        }
    }
    QSize size = frame.size().expandedTo(minimum).boundedTo(screen.size());
    QRect fitted(frame.topLeft(), size);
    fitted.moveLeft(std::clamp(fitted.left(), screen.left(), screen.right() - size.width() + 1));
    fitted.moveTop(std::clamp(fitted.top(), screen.top(), screen.bottom() - size.height() + 1));
    return fitted;
}

} // namespace os::ui
