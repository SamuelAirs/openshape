// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Open... and Import STEP... on the iPhone Duo, where Apple's file picker
// shows no files (and File > Open's picker closed the app; TD-158): the way
// that works there instead, sharing the file to FoldForm from the Files app
// (AppController::openIncomingFile takes it, as it does Mail's attachments).
// Open Files switches to the Files app.
Rectangle {
    id: overlay

    required property AppController app
    // "project" or "step": what the person was looking for.
    property string kind: "project"

    function open(what) {
        kind = what
        visible = true
        forceActiveFocus()
    }
    function close() {
        visible = false
    }

    color: "#66000000"
    visible: false
    focus: visible

    Keys.onEscapePressed: close()
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // The card stays clear of the status bar, the side bar and the home
    // indicator (Theme.safe*; zero on the desktop).
    Item {
        id: safeRect
        anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                  bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
    }

    Panel {
        anchors.centerIn: safeRect
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 32), 480)
        height: content.implicitHeight + 2 * (Theme.compact ? 16 : 24)

        ColumnLayout {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.compact ? 16 : 24 }
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Share It from the Files App"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: Theme.text
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "shareFromFilesText"
                Layout.fillWidth: true
                text: overlay.kind === "step"
                      ? "In the Files app, touch and hold the STEP file, tap Share, then FoldForm. It opens here as a new project."
                      : "In the Files app, touch and hold the project, tap Share, then FoldForm. It opens here, and FoldForm keeps a copy in its folder."
                font.pixelSize: 13
                color: Theme.mutedText
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "shareFromFilesCancel"; text: "Cancel"; onClicked: overlay.close() }
                ActionButton {
                    objectName: "shareFromFilesOpen"
                    text: "Open Files"
                    accent: true
                    onClicked: {
                        overlay.close()
                        overlay.app.openFilesApp()
                    }
                }
            }
        }
    }
}
