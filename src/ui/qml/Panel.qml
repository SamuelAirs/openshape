// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Effects
import OpenShape

// Floating surface for controls: a light card with a hairline edge and a
// soft shadow beneath, slightly translucent so the model stays dominant.
Rectangle {
    id: panel

    // The shadow beneath; off for a panel drawn inside another surface.
    property bool elevated: true

    radius: 12
    color: Theme.panel
    opacity: 0.97
    border.color: Theme.cardBorder
    border.width: 1

    RectangularShadow {
        z: -1 // beneath the panel's own surface
        visible: panel.elevated
        anchors.fill: parent
        radius: panel.radius
        offset.y: 3
        blur: 18
        spread: -3
        color: Theme.panelShadow
    }

    // Swallow pointer input on the panel's own surface so clicks between
    // buttons never fall through to the viewport (and deselect things).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }
}
