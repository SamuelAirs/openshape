// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Floating value editor that follows the manipulator: shows the live value,
// accepts typed input (with units and arithmetic), and offers apply/cancel
// plus the contextual alternatives for the current selection.
Item {
    id: chip

    required property AppController app
    signal finished()

    width: column.implicitWidth
    height: column.implicitHeight

    // The typed text itself is refused (not a length, out of range): its
    // message stays while typing. Otherwise the preview's verdict shows, which
    // may arrive after the keystroke (previews compute off the GUI thread).
    property bool typedTextRefused: false

    function typeValue(text) {
        const error = app.setValueText(text)
        typedTextRefused = error.length > 0 && error !== app.operationError
        errorText.text = error
        return error
    }

    function beginTyping(firstChar) {
        field.text = firstChar
        field.forceActiveFocus()
        field.cursorPosition = field.text.length
        typeValue(field.text)
    }

    function syncFromModel() {
        if (!field.activeFocus) {
            field.text = app.operationValueText
            errorText.text = app.operationError
        } else if (!typedTextRefused) {
            errorText.text = app.operationError
        }
    }

    Connections {
        target: chip.app
        function onStateChanged() { chip.syncFromModel() }
    }
    Component.onCompleted: syncFromModel()

    ColumnLayout {
        id: column
        spacing: 6

        Panel {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: row.implicitWidth + 2 * Theme.panelPadding
            implicitHeight: Theme.controlHeight + 2 * Theme.panelPadding
            border.color: errorText.text.length > 0 ? Theme.error : Theme.panelBorder

            RowLayout {
                id: row
                anchors.centerIn: parent
                spacing: 6

                Text {
                    text: chip.app.operationValueLabel
                    color: Theme.mutedText
                    font.pixelSize: 12
                    Layout.leftMargin: 6
                }
                TextField {
                    id: field
                    implicitWidth: 104
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: 15
                    horizontalAlignment: TextInput.AlignRight
                    selectByMouse: true
                    color: Theme.text
                    background: Rectangle {
                        radius: 8
                        color: field.activeFocus ? "white" : Theme.fieldIdle
                        border.color: field.activeFocus ? Theme.accent : "transparent"
                        border.width: 1.5
                    }
                    onTextEdited: chip.typeValue(text)
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    Keys.onReturnPressed: apply()
                    Keys.onEnterPressed: apply()
                    Keys.onEscapePressed: {
                        field.focus = false
                        chip.finished()
                        chip.syncFromModel()
                    }
                    function apply() {
                        const error = chip.typeValue(text)
                        if (error.length === 0) {
                            field.focus = false
                            chip.app.commitOperation()
                            chip.finished()
                            chip.syncFromModel()
                        }
                    }
                }
                ActionButton {
                    text: "✓"
                    accent: true
                    enabled: chip.app.operationCanCommit
                    implicitWidth: Theme.controlHeight
                    onClicked: {
                        field.focus = false
                        chip.app.commitOperation()
                        chip.finished()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Apply (Enter)"
                    ToolTip.delay: 500
                }
                ActionButton {
                    text: "✕"
                    implicitWidth: Theme.controlHeight
                    onClicked: {
                        field.focus = false
                        chip.app.cancelOperation()
                        chip.finished()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Cancel (Esc)"
                    ToolTip.delay: 500
                }
            }
        }

        Text {
            id: errorText
            objectName: "valueChipError"
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 320
            visible: text.length > 0
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.error
            font.pixelSize: 12
        }

        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            visible: chip.app.contextActions.length > 1
            Repeater {
                model: chip.app.contextActions
                delegate: ActionButton {
                    required property var modelData
                    objectName: "action_" + modelData.id
                    text: modelData.label
                    checked: modelData.active
                    compact: true
                    onClicked: {
                        // The action rebuilds the action list and destroys this
                        // delegate: capture what we need first.
                        const owner = chip
                        const id = modelData.id
                        owner.app.triggerAction(id)
                        owner.finished()
                    }
                }
            }
        }
    }
}
