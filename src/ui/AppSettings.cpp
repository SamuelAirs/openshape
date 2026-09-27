// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/AppSettings.h"

#include <QtCore/QMargins>
#include <QtCore/QSettings>

#include <algorithm>

namespace os::ui {

namespace {
const QString kDefaultUnit = QStringLiteral("preferences/defaultUnit");
const QString kGridSnap = QStringLiteral("preferences/sketchGridSnap");
const QString kRecoveryInterval = QStringLiteral("preferences/recoveryIntervalSeconds");
const QString kHoleAllowance = QStringLiteral("preferences/holeAllowanceMm");
const QString kPerspective = QStringLiteral("view/perspective");
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
    ok = false;
    const double allowance = settings.value(kHoleAllowance).toDouble(&ok);
    if (ok)
        p.holeAllowance = doc::validHoleAllowance(allowance);
    return p;
}

void savePreferences(QSettings& settings, const Preferences& p)
{
    settings.setValue(kDefaultUnit, p.defaultUnit == LengthUnit::Inch ? QStringLiteral("in") : QStringLiteral("mm"));
    settings.setValue(kGridSnap, p.sketchGridSnap);
    settings.setValue(kRecoveryInterval, p.recoveryIntervalSeconds);
    settings.setValue(kHoleAllowance, doc::validHoleAllowance(p.holeAllowance));
}

bool loadPerspective(QSettings& settings)
{
    // Only a stored "false" (or 0) means orthographic; anything else is the default.
    const QString value = settings.value(kPerspective).toString();
    return value != QLatin1String("false") && value != QLatin1String("0");
}

void savePerspective(QSettings& settings, bool perspective)
{
    settings.setValue(kPerspective, perspective);
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
    const QRect client = settings.value(kWindowClient).toRect();
    if (!frame.isValid() || !client.isValid() || !frame.contains(client))
        return std::nullopt;
    return WindowPlacement{frame, client, settings.value(kWindowMaximized).toBool()};
}

void saveWindowPlacement(QSettings& settings, const WindowPlacement& placement)
{
    settings.setValue(kWindowFrame, placement.frame);
    settings.setValue(kWindowClient, placement.client);
    settings.setValue(kWindowMaximized, placement.maximized);
}

QRect clientForFrame(const WindowPlacement& saved, const QRect& fitted)
{
    const QMargins border(saved.client.left() - saved.frame.left(), saved.client.top() - saved.frame.top(),
                          saved.frame.right() - saved.client.right(), saved.frame.bottom() - saved.client.bottom());
    return fitted.marginsRemoved(border);
}

QRect firstWindowGeometry(QSize preferred, const QRect& available, QSize minimum, int titleBar)
{
    const QSize room(available.width(), available.height() - titleBar);
    if (!available.isValid() || (preferred.width() <= room.width() && preferred.height() <= room.height()))
        return {};
    const QSize size = preferred.boundedTo(room * 0.9).expandedTo(minimum).boundedTo(room);
    const QPoint topLeft(available.left() + (available.width() - size.width()) / 2,
                         available.top() + titleBar + (room.height() - size.height()) / 2);
    return QRect(topLeft, size);
}

QString projectFileBaseName(const QString& typed)
{
    QString name = typed.trimmed();
    if (name.endsWith(QStringLiteral(".openshape"), Qt::CaseInsensitive))
        name.chop(int(sizeof(".openshape")) - 1);
    QString out;
    for (qsizetype i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        const bool forbidden = c.unicode() < 0x20 || QStringLiteral("/\\:*?\"<>|").contains(c);
        out += forbidden ? QChar(u'-') : c;
    }
    out = out.trimmed().left(100).trimmed();
    // Leading dots would hide the file (and "." / ".." are folders).
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    return out.trimmed();
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
