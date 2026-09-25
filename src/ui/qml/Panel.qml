// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick

// Floating surface for controls. Slightly translucent so the model stays dominant.
Rectangle {
    radius: 12
    color: Theme.panel
    opacity: 0.97
    border.color: Theme.panelBorder
    border.width: 1

    // Swallow pointer input on the panel's own surface so clicks between
    // buttons never fall through to the viewport (and deselect things).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }
}
