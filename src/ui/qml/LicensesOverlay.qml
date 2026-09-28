// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// About -> Licenses: the license texts and notices of OpenShape and of
// everything built into it, and how to rebuild the app with modified LGPL
// libraries (docs/LICENSING.md). A list; tapping an entry shows its full
// text, Back returns to the list. The texts are built into the app
// (resources/licenses; AppController.licenseEntries / licenseText).
Rectangle {
    id: overlay

    required property AppController app

    // The entry shown ("" = the list).
    property string currentId: ""
    property string currentName: ""
    property var entries: []

    color: "#66000000"
    visible: false
    focus: visible

    function open() {
        entries = app.licenseEntries()
        currentId = ""
        list.contentY = 0
        visible = true
        forceActiveFocus() // Esc goes back / closes this, not the About card below
    }
    function show(id, name) {
        pageText.text = app.licenseText(id)
        currentName = name
        currentId = id
        page.contentY = 0
    }
    function back() { currentId = "" }

    Keys.onEscapePressed: currentId !== "" ? back() : (visible = false)
    MouseArea {
        anchors.fill: parent
        onClicked: overlay.visible = false
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
        id: card
        objectName: "licensesCard"
        anchors.centerIn: safeRect
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 48), 640)
        height: Math.min(safeRect.height - (Theme.compact ? 16 : 48), 760)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.compact ? 12 : 20
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    objectName: "licensesBack"
                    visible: overlay.currentId !== ""
                    text: "Back"
                    onClicked: overlay.back()
                }
                Text {
                    objectName: "licensesTitle"
                    Layout.fillWidth: true
                    text: overlay.currentId === "" ? "Licenses" : overlay.currentName
                    font.pixelSize: overlay.currentId === "" ? 20 : 16
                    font.weight: Font.DemiBold
                    color: Theme.text
                    elide: Text.ElideRight
                }
                ActionButton { objectName: "licensesClose"; text: "Close"; onClicked: overlay.visible = false }
            }

            // ---- The list
            Flickable {
                id: list
                objectName: "licensesList"
                visible: overlay.currentId === ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: rows.implicitHeight

                ColumnLayout {
                    id: rows
                    width: list.width
                    spacing: 0

                    Text {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 4
                        wrapMode: Text.WordWrap
                        color: Theme.mutedText
                        font.pixelSize: 13
                        text: "OpenShape is free software under the Mozilla Public License 2.0. It is built with "
                            + "the libraries below, which keep their own licenses. Each entry shows its license "
                            + "text and where its source code is."
                    }
                    Repeater {
                        model: overlay.entries
                        delegate: ColumnLayout {
                            id: entry
                            required property var modelData
                            required property int index
                            readonly property bool firstOfGroup: index === 0
                                || overlay.entries[index - 1].group !== modelData.group
                            Layout.fillWidth: true
                            spacing: 0

                            SectionLabel {
                                visible: entry.firstOfGroup
                                Layout.leftMargin: 0
                                Layout.topMargin: 10
                                text: entry.modelData.group === "offer" ? "Your rights"
                                    : entry.modelData.group === "app" ? "OpenShape"
                                    : entry.modelData.group === "library" ? "Built with"
                                    : "Inside Qt (code from other projects)"
                            }
                            AbstractButton {
                                id: row
                                objectName: "license_" + entry.modelData.id
                                Layout.fillWidth: true
                                implicitHeight: Math.max(Theme.controlHeight, rowText.implicitHeight + 12)
                                hoverEnabled: true
                                focusPolicy: Qt.NoFocus
                                onClicked: overlay.show(entry.modelData.id, entry.modelData.name)
                                background: Rectangle {
                                    radius: 8
                                    color: row.pressed ? Theme.pressed : row.hovered ? Theme.hover : "transparent"
                                }
                                contentItem: ColumnLayout {
                                    id: rowText
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        leftPadding: 8
                                        rightPadding: 8
                                        text: entry.modelData.name + (entry.modelData.version ? " " + entry.modelData.version : "")
                                        textFormat: Text.PlainText
                                        color: Theme.text
                                        font.pixelSize: 14
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        leftPadding: 8
                                        rightPadding: 8
                                        text: entry.modelData.license + (entry.modelData.usedFor ? " · " + entry.modelData.usedFor : "")
                                        textFormat: Text.PlainText
                                        color: Theme.mutedText
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- One entry's text
            Flickable {
                id: page
                objectName: "licensePage"
                visible: overlay.currentId !== ""
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: pageText.implicitHeight
                TextEdit {
                    id: pageText
                    objectName: "licenseText"
                    width: page.width
                    readOnly: true
                    selectByMouse: !Theme.touch
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    color: Theme.text
                    font.pixelSize: 12
                }
            }
        }
    }
}
