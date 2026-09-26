// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// After a crash: the recovery copies of the work that was not saved, each
// with Restore and Discard. Shown at startup while app.recoveryItems is not
// empty; it does not close on an outside click (Esc or "Decide later" keeps
// the copies for the next start).
Rectangle {
    id: overlay

    required property AppController app
    // Restore asks about unsaved changes first (like Open does).
    signal restoreRequested(string session)

    color: "#66000000"
    visible: app.recoveryItems.length > 0
    focus: visible

    Keys.onEscapePressed: app.postponeRecovery()
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
        anchors.centerIn: safeRect
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 32), 600)
        height: Math.min(safeRect.height - (Theme.compact ? 16 : 32), content.implicitHeight + 2 * (Theme.compact ? 16 : 24))

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.compact ? 16 : 24
            contentHeight: content.implicitHeight
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "OpenShape closed unexpectedly"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: Theme.text
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 14
                    text: overlay.app.recoveryItems.length === 1
                          ? "Restore your unsaved work?"
                          : "Restore your unsaved work? There are " + overlay.app.recoveryItems.length + " documents."
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 13
                    text: "A restored document opens unsaved; save it to keep it. Your own files were not changed."
                }

                Repeater {
                    model: overlay.app.recoveryItems
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: rowLayout.implicitHeight + 20
                        radius: 10
                        color: Theme.background
                        border.color: Theme.panelBorder

                        RowLayout {
                            id: rowLayout
                            anchors { fill: parent; margins: 10 }
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    color: Theme.text
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: row.modelData.time + " · " + row.modelData.detail
                                    color: Theme.mutedText
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                }
                            }
                            ActionButton {
                                objectName: "recoveryRestore_" + row.modelData.session
                                text: "Restore"
                                accent: true
                                onClicked: overlay.restoreRequested(row.modelData.session)
                            }
                            ActionButton {
                                objectName: "recoveryDiscard_" + row.modelData.session
                                text: "Discard"
                                onClicked: {
                                    const app = overlay.app // the row goes away with its item
                                    app.discardRecovery(row.modelData.session)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    spacing: 8
                    ActionButton {
                        objectName: "recoveryDiscardAll"
                        visible: overlay.app.recoveryItems.length > 1
                        text: "Discard all"
                        onClicked: overlay.app.discardAllRecovery()
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        objectName: "recoveryLater"
                        text: "Decide later"
                        onClicked: overlay.app.postponeRecovery()
                    }
                }
            }
        }
    }
}
