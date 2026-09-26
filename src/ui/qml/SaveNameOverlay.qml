// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// iPhone / iPad: Save asks for a name only. The project goes into
// OpenShape's own folder, which the Files app shows (iOS has no save
// dialog; see AppController::savesToAppFolder). Touch-sized; near the top
// on a phone, so the on-screen keyboard does not cover it.
Rectangle {
    id: overlay

    required property AppController app
    signal saved()      // the project was saved (a pending New or Open continues)
    signal cancelled()

    // A project of that name is there already: Save replaces it.
    readonly property bool replaces: visible && app.appFolderHasProject(field.text)

    function open(name) {
        field.text = name
        visible = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function cancel() {
        visible = false
        cancelled()
    }
    function save() {
        if (field.text.trim().length === 0)
            return
        if (app.saveInAppFolder(field.text)) {
            visible = false
            saved()
        }
    }

    color: "#66000000"
    visible: false
    // Focus given to the prompt (a menu closing hands it the keys) goes on
    // to the name field.
    onActiveFocusChanged: if (activeFocus) field.forceActiveFocus()

    Keys.onEscapePressed: cancel()
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // The card stays clear of a phone's Dynamic Island, rounded corners and
    // home indicator (Theme.safe*; zero on the desktop).
    Item {
        id: safeRect
        anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                  bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
    }

    Panel {
        objectName: "saveNamePanel"
        anchors.horizontalCenter: safeRect.horizontalCenter
        y: Theme.compact ? safeRect.y + Theme.margin + 24 : safeRect.y + (safeRect.height - height) / 3
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 32), 480)
        height: content.implicitHeight + 2 * (Theme.compact ? 16 : 24)

        ColumnLayout {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.compact ? 16 : 24 }
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Save project"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: Theme.text
            }
            TextField {
                id: field
                objectName: "saveNameField"
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                font.pixelSize: 15
                color: Theme.text
                placeholderText: "Name"
                selectByMouse: true
                // The offered name stays selected (typing replaces it) while the
                // closing menu hands the focus around.
                persistentSelection: true
                background: Rectangle {
                    radius: 8
                    color: "white"
                    border.color: Theme.accent
                    border.width: 1.5
                }
                onAccepted: overlay.save()
                Keys.onEscapePressed: overlay.cancel()
            }
            Text {
                objectName: "saveNameNote"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: overlay.replaces ? "#9A6A00" : Theme.mutedText
                text: overlay.replaces ? "A project with this name is there already: saving replaces it."
                                       : "Saved in OpenShape's folder, which the Files app shows."
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "saveNameCancel"; text: "Cancel"; onClicked: overlay.cancel() }
                ActionButton {
                    objectName: "saveNameSave"
                    text: overlay.replaces ? "Replace" : "Save"
                    accent: true
                    enabled: field.text.trim().length > 0
                    onClicked: overlay.save()
                }
            }
        }
    }
}
