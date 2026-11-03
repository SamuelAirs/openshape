// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton
import QtQuick

// Visual constants. One place to tune the calm, neutral look.
QtObject {
    readonly property color background: "#ECEEF1"
    readonly property color panel: "#F9FAFB"
    readonly property color panelBorder: "#D8DCE2"
    readonly property color text: "#1F2329"
    readonly property color mutedText: "#6A717C"
    readonly property color accent: "#2977ED"
    readonly property color accentPressed: "#1B5FCC"
    readonly property color hover: "#E7EBF0"
    readonly property color pressed: "#DADFE6"
    readonly property color checked: "#DCE8FB"
    readonly property color fieldIdle: "#EEF1F4"
    readonly property color error: "#D93F42"
    readonly property color toast: "#2A2F36"

    // Raised surfaces (menus, cards, sheets): white, a hairline edge and a
    // soft shadow beneath (QtQuick.Effects RectangularShadow).
    readonly property color card: "#FFFFFF"
    readonly property color cardBorder: "#1A1F2937"
    readonly property color shadow: "#38101828"
    readonly property color panelShadow: "#24101828"
    readonly property color scrim: "#40101418"
    readonly property color previewWell: "#F1F3F6"
    readonly property color track: "#E4E8EE"
    readonly property color menuHighlight: "#EEF3FD"
    readonly property color menuPressed: "#DEE8FB"
    readonly property color destructiveHighlight: "#FDEEEE"
    readonly property color destructivePressed: "#F9DCDD"
    readonly property color shortcutText: "#8A919B"
    readonly property color iconColor: "#5B636E"
    readonly property color disabledIcon: "#C3C8CF"
    readonly property color disabledText: "#A7ADB5"
    readonly property color hairline: "#E6E9EE"
    readonly property int cardRadius: 16
    readonly property int menuRadius: 12

    readonly property int margin: compact ? 10 : 16
    readonly property int panelPadding: 6
    // Set while the app is used by touch (Main.qml binds it to app.touchMode):
    // controls grow to 44 logical px, Apple's minimum touch target.
    property bool touch: false
    readonly property int controlHeight: touch ? 44 : 36

    // The window's size (Main.qml binds it). The layout follows the window,
    // not the device: a phone either way up, a Split View half on an iPad or
    // a folding phone's outer display are all "compact" - tools in a strip
    // along the bottom, the Model panel and the view buttons behind a button.
    // It changes live when the window is resized (a phone folds or unfolds).
    property real windowWidth: 1400
    property real windowHeight: 900
    readonly property int compactWidth: 600   // below: compact (a phone in portrait, a Split View half)
    readonly property int compactHeight: 500  // below: compact (a phone in landscape)
    readonly property bool compact: windowWidth < compactWidth || windowHeight < compactHeight

    // Safe-area insets in logical px (the Dynamic Island or notch, the
    // rounded corners, the home indicator; zero on the desktop). Controls
    // stay inside them; the 3D view fills the whole window behind. Main.qml
    // binds them to the window's SafeArea (or to --safe-area, to try the
    // phone layouts on the desktop).
    property real safeTop: 0
    property real safeRight: 0
    property real safeBottom: 0
    property real safeLeft: 0
    // Distance of controls from each window edge.
    readonly property real insetTop: margin + safeTop
    readonly property real insetRight: margin + safeRight
    readonly property real insetBottom: margin + safeBottom
    readonly property real insetLeft: margin + safeLeft
}
