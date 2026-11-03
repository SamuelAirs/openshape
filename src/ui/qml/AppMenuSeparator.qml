// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

// A hairline between groups of an AppMenu's rows.
MenuSeparator {
    topPadding: 5
    bottomPadding: 5
    leftPadding: 10
    rightPadding: 10
    contentItem: Rectangle {
        implicitWidth: 200
        implicitHeight: 1
        color: Theme.hairline
    }
}
