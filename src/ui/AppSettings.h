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

// The view's projection as the user last chose it (the View buttons'
// Perspective / Orthographic toggle): perspective unless they chose
// orthographic.
bool loadPerspective(QSettings& settings);
void savePerspective(QSettings& settings, bool perspective);

QStringList loadRecentFiles(QSettings& settings);
void saveRecentFiles(QSettings& settings, const QStringList& files);

// The window's last place while not maximized: its frame (title bar
// included) and its client area (inside the frame), and whether it was
// maximized. Both rectangles are kept because a window can only be placed
// by its client area before it exists, and fitted onto a screen by its frame.
struct WindowPlacement {
    QRect frame;
    QRect client;
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
// Where the client area goes when the frame is placed at `fitted`: the saved
// frame's borders (title bar, edges) stay the same.
QRect clientForFrame(const WindowPlacement& saved, const QRect& fitted);
// The first start (nothing saved): the default client size may not fit a
// small or strongly scaled screen (1400x900 logical is more than a 1080p
// screen at 150 %). Then a client area of 90 % of the available area
// (title bar included), centered; empty when the default fits.
QRect firstWindowGeometry(QSize preferred, const QRect& available, QSize minimum, int titleBar);

// iPhone / iPad: projects are saved by name into the app's folder (no save
// dialog). The file name for what the user typed: trimmed, a typed
// ".openshape" dropped, characters no file system takes (/ \ : * ? " < > |
// and control characters) replaced by "-", at most 100 characters. Empty
// when nothing usable is left (e.g. "", "  ", ".", "..").
QString projectFileBaseName(const QString& typed);

} // namespace os::ui
