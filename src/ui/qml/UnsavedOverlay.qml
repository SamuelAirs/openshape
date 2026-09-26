// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// "Save changes?" before something replaces or closes the document (New,
// Open, Open Recent, Restore, closing the window). An overlay rather than a
// native message box: touch-sized, and the acceptance run can click it.
Rectangle {
    id: overlay

    required property AppController app
    // Save: the window saves (asking for a file if there is none), then
    // continues with the action; Don't Save: the action runs now.
    signal saveRequested(var action)

    property var pendingAction: null

    function ask(action) {
        if (visible)
            return // one question at a time: the pending action is the one asked about
        pendingAction = action
        visible = true
        forceActiveFocus()
    }
    function finish(save) {
        const action = pendingAction
        pendingAction = null
        visible = false
        if (!action)
            return
        if (save)
            saveRequested(action)
        else
            action()
    }
    function cancel() {
        pendingAction = null
        visible = false
    }

    color: "#66000000"
    visible: false
    focus: visible

    Keys.onEscapePressed: cancel()
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    Panel {
        anchors.centerIn: parent
        opacity: 1
        width: Math.min(parent.width - 32, 480)
        height: content.implicitHeight + 48

        ColumnLayout {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Save changes to “" + overlay.app.documentTitle + "”?"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: Theme.text
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "Your changes will be lost if you don’t save them."
                font.pixelSize: 13
                color: Theme.mutedText
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                ActionButton { objectName: "unsavedDiscard"; text: "Don’t Save"; onClicked: overlay.finish(false) }
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "unsavedCancel"; text: "Cancel"; onClicked: overlay.cancel() }
                ActionButton { objectName: "unsavedSave"; text: "Save"; accent: true; onClicked: overlay.finish(true) }
            }
        }
    }
}
