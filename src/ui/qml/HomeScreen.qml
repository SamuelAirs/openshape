// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The start screen (Home): recent projects as cards with their previews,
// plus New project, Open... and Import STEP.... Shown at launch without a
// file and from File -> Home; Esc or Back returns to the open document.
// Touch-first: whole cards are tap targets, and the ... button, a long press
// or a right click opens a card's menu.
Rectangle {
    id: home

    required property AppController app
    // Actions that replace the document go through the window (unsaved
    // changes are asked about first; file dialogs).
    signal newRequested()
    signal openRequested()
    signal importRequested()
    signal projectRequested(string path)

    color: Theme.background
    visible: app.homeVisible
    focus: visible

    readonly property bool narrow: width < 600
    // A phone in landscape: less header, smaller cards, more of them per row.
    readonly property bool short_: height < 520
    readonly property bool hasDocument: app.bodyCount > 0 || app.sketchCount > 0 || app.dirty

    Keys.onEscapePressed: app.homeVisible = false
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true // never zooms the view underneath
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 2 * pageMargin
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property int pageMargin: home.narrow || home.short_ ? 16 : 32

        ColumnLayout {
            id: content
            x: Math.max(page.pageMargin, (page.width - width) / 2)
            y: page.pageMargin
            width: Math.min(page.width - 2 * page.pageMargin, 1100)
            spacing: home.short_ ? 8 : 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Column {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "OpenShape"
                        font.pixelSize: home.narrow || home.short_ ? 22 : 26
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Text {
                        visible: !home.short_
                        text: "Projects"
                        font.pixelSize: 14
                        color: Theme.mutedText
                    }
                }
                ActionButton {
                    objectName: "homeBack"
                    visible: home.hasDocument
                    outlined: true
                    text: home.narrow ? "Back" : "Back to " + home.app.documentTitle
                    onClicked: home.app.homeVisible = false
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    objectName: "homeNew"
                    text: "New project"
                    accent: true
                    onClicked: home.newRequested()
                }
                ActionButton {
                    objectName: "homeOpen"
                    text: "Open…"
                    outlined: true
                    onClicked: home.openRequested()
                }
                ActionButton {
                    objectName: "homeImport"
                    text: "Import STEP…"
                    outlined: true
                    onClicked: home.importRequested()
                }
            }

            SectionLabel {
                text: "Recent"
                Layout.topMargin: home.short_ ? 2 : 10
                visible: home.app.homeProjects.length > 0
            }

            // Cards: as many columns as fit (two on a phone in portrait).
            Grid {
                id: grid
                Layout.fillWidth: true
                visible: home.app.homeProjects.length > 0
                readonly property int minCard: home.narrow ? 150 : home.short_ ? 170 : 220
                columns: Math.max(1, Math.floor((content.width + columnSpacing) / (minCard + columnSpacing)))
                columnSpacing: home.narrow ? 12 : 16
                rowSpacing: columnSpacing
                readonly property real cardWidth: (content.width - (columns - 1) * columnSpacing) / columns

                Repeater {
                    model: home.app.homeProjects
                    delegate: ProjectCard {
                        required property var modelData
                        required property int index
                        width: grid.cardWidth
                        project: modelData
                        cardIndex: index
                    }
                }
            }

            Column {
                Layout.fillWidth: true
                Layout.topMargin: 24
                visible: home.app.homeProjects.length === 0
                spacing: 6
                Text {
                    width: parent.width
                    text: "No recent projects"
                    font.pixelSize: 17
                    font.weight: Font.Medium
                    color: Theme.text
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    text: "New project starts one; Open… finds one on this device; Import STEP… brings in parts from other CAD programs."
                    font.pixelSize: 13
                    color: Theme.mutedText
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    component ProjectCard: Rectangle {
        id: card
        property var project: ({})
        property int cardIndex: 0
        objectName: "homeCard_" + cardIndex
        height: preview.height + details.implicitHeight + 16
        radius: 12
        color: tap.pressed ? Theme.hover : Theme.panel
        border.color: tap.containsMouse ? Theme.accent : Theme.panelBorder
        border.width: 1

        function openMenu(x, y) {
            cardMenu.popup(card, x, y)
        }

        MouseArea {
            id: tap
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            pressAndHoldInterval: 500
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton)
                    card.openMenu(mouse.x, mouse.y)
                else
                    home.projectRequested(card.project.path)
            }
            onPressAndHold: (mouse) => card.openMenu(mouse.x, mouse.y)
        }

        Rectangle {
            id: preview
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
            height: Math.round(width * (home.short_ ? 0.6 : 0.72))
            radius: 8
            color: "#F1F3F6"
            Image {
                id: thumbnail
                objectName: "homeThumbnail_" + card.cardIndex
                anchors { fill: parent; margins: 6 }
                source: card.project.thumbnail || ""
                sourceSize: Qt.size(256, 256)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                mipmap: true
            }
            Text {
                anchors.centerIn: parent
                visible: thumbnail.status !== Image.Ready
                text: thumbnail.status === Image.Loading ? "" : "No preview"
                font.pixelSize: 12
                color: Theme.mutedText
            }
        }

        Column {
            id: details
            anchors { left: parent.left; right: parent.right; top: preview.bottom; margins: 10; topMargin: 8 }
            spacing: 2
            Text {
                width: parent.width - menuButton.width
                text: card.project.name || ""
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                width: parent.width - menuButton.width
                text: card.project.modified || ""
                font.pixelSize: 12
                color: Theme.mutedText
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: card.project.folder || ""
                font.pixelSize: 11
                color: Theme.mutedText
                elide: Text.ElideMiddle
            }
        }

        ActionButton {
            id: menuButton
            objectName: "homeCardMenu_" + card.cardIndex
            anchors { right: parent.right; top: preview.bottom; rightMargin: 4; topMargin: 4 }
            text: "⋯"
            compact: true
            fontSize: 18
            implicitWidth: implicitHeight
            onClicked: card.openMenu(x, y + height)
        }

        Menu {
            id: cardMenu
            MenuItem {
                objectName: "homeRemove_" + card.cardIndex
                text: "Remove from list"
                enabled: card.project.removable === true
                // After the menu has closed: removing rebuilds the cards.
                onTriggered: {
                    const path = card.project.path
                    const app = home.app
                    Qt.callLater(() => app.removeRecentFile(path))
                }
            }
        }
    }
}
