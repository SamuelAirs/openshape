// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace os::ui {

// What the window is like right now, as the layout sees it: its size in
// logical pixels (points on iOS), the device pixel ratio, the safe-area
// margins and the screen it is on. Logged whenever any of it changes
// (ui/WindowMetricsLog), so a device test's log (Logs/openshape.log) tells
// the real point sizes and insets of a phone, a Split View half or a
// folding phone's two displays. Qt Core only (tests/test_uistate).
struct WindowMetrics {
    int width = 0; // logical px (points)
    int height = 0;
    double devicePixelRatio = 1.0;
    // Safe-area margins in logical px (the status bar, the Dynamic Island,
    // the rounded corners, the home indicator; a folding phone's side bar).
    double safeTop = 0;
    double safeRight = 0;
    double safeBottom = 0;
    double safeLeft = 0;
    bool safeAreaSimulated = false; // --safe-area on the desktop
    QString layout;                 // "compact" or "regular" (Theme.compact), empty if unknown
    QString screenName;
    int screenWidth = 0; // the screen's logical size
    int screenHeight = 0;
    double screenDevicePixelRatio = 1.0;

    bool operator==(const WindowMetrics&) const = default;
};

// What differs between two readings, in words: "size", "pixel ratio",
// "safe area", "layout", "screen" (empty when nothing does).
QStringList windowMetricsChanges(const WindowMetrics& before, const WindowMetrics& after);

// One log line, e.g. "window 951x669 pt at 3x (2853x2007 px), safe area
// top 0 right 84 bottom 20 left 0, usable 867x649 pt, regular layout;
// screen "iPhone" 951x669 pt at 3x". `changes` (optional) is appended as
// " (changed: size, safe area)".
QString describeWindowMetrics(const WindowMetrics& metrics, const QStringList& changes = {});

} // namespace os::ui
