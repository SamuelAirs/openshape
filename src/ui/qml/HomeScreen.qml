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
// or a right click opens a card's menu: Rename..., Duplicate, Remove from
// list and (for projects in OpenShape's folder, and recent ones on the
// desktop) Delete..., each confirmed on a card of its own.
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

    // A card's Rename... and Delete... (the cards below the page).
    function askRename(path, name) { renameCard.open(path, name) }
    function askDelete(path, name) { deleteCard.open(path, name) }

    // A card left open when Home goes (File → New from the keyboard, say)
    // would come back later about a project that is no longer open.
    onVisibleChanged: {
        if (!visible) {
            renameCard.visible = false
            deleteCard.visible = false
        }
    }

    Keys.onEscapePressed: app.homeVisible = false
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true // never zooms the view underneath
    }

    Flickable {
        id: page
        objectName: "homePage"
        // Clear of the status bar / Dynamic Island and the rounded corners
        // (iPhone, iPad); the cards scroll on behind the home indicator but
        // the last one can be scrolled above it.
        anchors { fill: parent; topMargin: Theme.safeTop; leftMargin: Theme.safeLeft; rightMargin: Theme.safeRight }
        contentWidth: width
        contentHeight: content.implicitHeight + 2 * pageMargin + Theme.safeBottom
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
                        text: "FoldForm"
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
                objectName: "homeCardName_" + card.cardIndex
                width: parent.width - menuButton.width
                text: card.project.name || ""
                textFormat: Text.PlainText // file names are shown as they are
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                width: parent.width - menuButton.width
                text: card.project.modified || ""
                textFormat: Text.PlainText
                font.pixelSize: 12
                color: Theme.mutedText
                elide: Text.ElideRight
            }
            Text {
                // Where it is: on iPhone and iPad "FoldForm (Files app)"
                // (io::homeFolderLabel), not the app's sandbox path.
                objectName: "homeCardFolder_" + card.cardIndex
                width: parent.width
                text: card.project.folder || ""
                textFormat: Text.PlainText
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
            implicitHeight: Theme.touch ? 44 : 30 // a finger's target on touch
            implicitWidth: implicitHeight
            onClicked: card.openMenu(x, y + height)
        }

        // Each action runs after the menu has closed: most rebuild the cards.
        Menu {
            id: cardMenu
            CardMenuItem {
                objectName: "homeRename_" + card.cardIndex
                text: "Rename…"
                onTriggered: {
                    const project = card.project
                    Qt.callLater(() => home.askRename(project.path, project.name))
                }
            }
            CardMenuItem {
                objectName: "homeDuplicate_" + card.cardIndex
                text: "Duplicate"
                onTriggered: {
                    const path = card.project.path
                    const app = home.app
                    Qt.callLater(() => app.duplicateProject(path))
                }
            }
            CardMenuItem {
                objectName: "homeRemove_" + card.cardIndex
                text: "Remove from list"
                enabled: card.project.removable === true
                onTriggered: {
                    const path = card.project.path
                    const app = home.app
                    Qt.callLater(() => app.removeRecentFile(path))
                }
            }
            // Only for projects in OpenShape's folder and, on the desktop,
            // recent ones (through the Recycle Bin); see deleteProject.
            CardMenuItem {
                objectName: "homeDelete_" + card.cardIndex
                text: "Delete…"
                visible: card.project.deletable === true
                height: visible ? implicitHeight : 0
                destructive: true
                onTriggered: {
                    const project = card.project
                    Qt.callLater(() => home.askDelete(project.path, project.name))
                }
            }
        }
    }

    // Touch-sized menu entries (44 px on touch); Delete... in red.
    component CardMenuItem: MenuItem {
        id: item
        property bool destructive: false
        implicitHeight: Theme.touch ? 44 : 34
        contentItem: Text {
            leftPadding: item.leftPadding
            text: item.text
            font.pixelSize: 14
            color: item.destructive ? Theme.error : Theme.text
            opacity: item.enabled ? 1 : 0.4
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    // A dimmed page with a card in the middle (near the top on a phone, so
    // the on-screen keyboard does not cover it), clear of the safe area.
    component ModalCard: Rectangle {
        id: modal
        default property alias cardContent: column.data
        property alias cardObjectName: panel.objectName
        signal dismissed()
        anchors.fill: parent
        z: 20
        color: "#66000000"
        visible: false
        Keys.onEscapePressed: dismissed()
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => wheel.accepted = true
        }
        Item {
            id: safeRect
            anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                      bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
        }
        Panel {
            id: panel
            anchors.horizontalCenter: safeRect.horizontalCenter
            y: Theme.compact ? safeRect.y + Theme.margin + 24 : safeRect.y + (safeRect.height - height) / 3
            opacity: 1
            width: Math.min(safeRect.width - (Theme.compact ? 16 : 32), 440)
            height: column.implicitHeight + 2 * (Theme.compact ? 16 : 24)
            ColumnLayout {
                id: column
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.compact ? 16 : 24 }
                spacing: 12
            }
        }
    }

    // Rename...: the new name is checked as it is typed (no name, a name
    // another project there has, characters files cannot have).
    ModalCard {
        id: renameCard
        objectName: "homeRenameCard"
        cardObjectName: "homeRenamePanel"
        property string path: ""
        property string oldName: ""
        readonly property string problem: visible ? home.app.projectNameProblem(path, renameField.text) : ""
        readonly property bool unchanged: renameField.text.trim() === oldName

        function open(projectPath, name) {
            path = projectPath
            oldName = name
            renameField.text = name
            visible = true
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        function close() {
            visible = false
            home.forceActiveFocus()
        }
        function accept() {
            if (unchanged) {
                close()
                return
            }
            if (problem === "" && home.app.renameProject(path, renameField.text))
                close()
        }
        onDismissed: close()

        Text {
            Layout.fillWidth: true
            text: "Rename project"
            font.pixelSize: 18
            font.weight: Font.DemiBold
            color: Theme.text
        }
        TextField {
            id: renameField
            objectName: "homeRenameField"
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            font.pixelSize: 15
            color: Theme.text
            placeholderText: "Name"
            selectByMouse: true
            persistentSelection: true
            background: Rectangle {
                radius: 8
                color: "white"
                border.color: renameCard.problem !== "" ? Theme.error : Theme.accent
                border.width: 1.5
            }
            onAccepted: renameCard.accept()
            Keys.onEscapePressed: renameCard.close()
        }
        Text {
            objectName: "homeRenameNote"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.pixelSize: 12
            color: renameCard.problem !== "" ? Theme.error : Theme.mutedText
            text: renameCard.problem !== "" ? renameCard.problem : "The project's file gets the new name."
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "homeRenameCancel"; text: "Cancel"; onClicked: renameCard.close() }
            ActionButton {
                objectName: "homeRenameOk"
                text: "Rename"
                accent: true
                enabled: renameCard.problem === "" && !renameCard.unchanged
                onClicked: renameCard.accept()
            }
        }
    }

    // Delete...: names the project and where it goes. When it cannot go to
    // the trash (a project in OpenShape's folder), the card asks again,
    // saying that it cannot be undone.
    ModalCard {
        id: deleteCard
        objectName: "homeDeleteCard"
        cardObjectName: "homeDeletePanel"
        property string path: ""
        property string name: ""
        property bool permanent: false // the trash failed: deleting is for good
        property bool isOpen: false

        function open(projectPath, projectName) {
            path = projectPath
            name = projectName
            permanent = false
            isOpen = home.app.isOpenProject(projectPath)
            visible = true
            forceActiveFocus()
        }
        function close() {
            visible = false
            home.forceActiveFocus()
        }
        function accept() {
            const result = home.app.deleteProject(path, permanent)
            if (result === "permanent")
                permanent = true // ask again: it cannot be undone
            else
                close()
        }
        onDismissed: close()

        Text {
            objectName: "homeDeleteTitle"
            Layout.fillWidth: true
            text: (deleteCard.permanent ? "Delete “%1” for good?" : "Delete “%1”?").arg(deleteCard.name)
            textFormat: Text.PlainText
            font.pixelSize: 18
            font.weight: Font.DemiBold
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Text {
            objectName: "homeDeleteNote"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            font.pixelSize: 13
            color: deleteCard.permanent ? Theme.error : Theme.mutedText
            text: (deleteCard.permanent
                   ? "It cannot be moved to " + home.app.trashName + ". Deleting it cannot be undone."
                   : "It goes to " + home.app.trashName + ".")
                  + (deleteCard.isOpen ? (home.app.dirty ? " It is open: it closes, and its unsaved changes are lost."
                                                          : " It is open: it closes.")
                                       : "")
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "homeDeleteCancel"; text: "Cancel"; onClicked: deleteCard.close() }
            ActionButton {
                id: deleteButton
                objectName: "homeDeleteOk"
                text: deleteCard.permanent ? "Delete for Good" : "Delete"
                outlined: true
                contentItem: Text {
                    text: deleteButton.text
                    font.pixelSize: deleteButton.fontSize
                    font.weight: Font.DemiBold
                    color: Theme.error
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: deleteCard.accept()
            }
        }
    }
}
