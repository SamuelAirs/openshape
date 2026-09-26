// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Units.h"

#include <QtCore/QList>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QStringList>

#include <array>
#include <optional>

class QSettings;

// What the app remembers between runs, in QSettings (on Windows the registry
// under HKCU\Software\OpenShape\OpenShape; automated runs use a throw-away
// INI file instead, see main.cpp). Readers treat stored values as untrusted:
// anything unexpected falls back to the default.
namespace os::ui {

// File → Preferences…
struct Preferences {
    LengthUnit defaultUnit = LengthUnit::Millimeter; // for new documents
    bool sketchGridSnap = true;
    int recoveryIntervalSeconds = 60; // at least this often while unsaved; 0 = no recovery copies
};
// The recovery intervals the Preferences panel offers (0 = off).
inline constexpr std::array<int, 4> kRecoveryIntervals{0, 30, 60, 300};
// Recovery copies are written this long after the last edit (edits settle first).
inline constexpr int kRecoveryDebounceMs = 3000;

Preferences loadPreferences(QSettings& settings);
void savePreferences(QSettings& settings, const Preferences& preferences);

QStringList loadRecentFiles(QSettings& settings);
void saveRecentFiles(QSettings& settings, const QStringList& files);

// The window's last place: its frame (title bar included) and client size
// while not maximized, and whether it was maximized.
struct WindowPlacement {
    QRect frame;
    QSize client;
    bool maximized = false;
};
std::optional<WindowPlacement> loadWindowPlacement(QSettings& settings);
void saveWindowPlacement(QSettings& settings, const WindowPlacement& placement);

// Fits a remembered frame rectangle onto the screens that exist now (a monitor
// may be gone or its resolution changed): it goes onto the screen it overlaps
// most (else the first, the primary), shrinks to fit that screen's available
// area (not below `minimum` unless the screen is smaller) and moves fully onto
// it, so the title bar is always reachable. Empty when nothing was saved.
QRect fitToScreens(const QRect& frame, const QList<QRect>& availableScreens, QSize minimum);

} // namespace os::ui
