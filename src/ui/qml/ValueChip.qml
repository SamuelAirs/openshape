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
    // The widest it may be (a phone's width): the actions below the field
    // then scroll sideways.
    property real maximumWidth: Infinity
    signal finished()

    width: column.implicitWidth
    height: column.implicitHeight

    function beginTyping(firstChar) {
        field.text = firstChar
        field.forceActiveFocus()
        field.cursorPosition = field.text.length
        errorText.text = app.setValueText(field.text)
    }

    function syncFromModel() {
        if (!field.activeFocus) {
            field.text = app.operationValueText
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
                    onTextEdited: errorText.text = chip.app.setValueText(text)
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    Keys.onReturnPressed: apply()
                    Keys.onEnterPressed: apply()
                    // Operations with several fields (the Hole tool's
                    // diameter, depth, X, Y): Tab goes to the next one.
                    Keys.onTabPressed: (event) => {
                        if (!chip.app.contextActions.some(a => a.id.startsWith("field:"))) {
                            event.accepted = false
                            return
                        }
                        errorText.text = chip.app.setValueText(text)
                        if (errorText.text.length === 0) {
                            chip.app.triggerAction("nextField")
                            field.text = chip.app.operationValueText
                            field.selectAll()
                        }
                    }
                    Keys.onEscapePressed: {
                        field.focus = false
                        chip.finished()
                        chip.syncFromModel()
                    }
                    function apply() {
                        const error = chip.app.setValueText(text)
                        errorText.text = error
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
                    ToolTip.visible: hovered && !Theme.touch
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
                    ToolTip.visible: hovered && !Theme.touch
                    ToolTip.text: "Cancel (Esc)"
                    ToolTip.delay: 500
                }
            }
        }

        Text {
            id: errorText
            objectName: "valueChipError"
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: Math.min(320, chip.maximumWidth)
            visible: text.length > 0
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.error
            font.pixelSize: 12
        }

        // The actions: in a regular window they wrap (the Hole tool has many)
        // at a width that still fits beside the model; in a compact window
        // (a phone) they scroll sideways. Only one of the two is visible; the
        // acceptance run clicks the visible one.
        Row {
            id: actionMeasure
            visible: false
            spacing: 4
            Repeater {
                model: chip.app.contextActions
                delegate: ActionButton {
                    required property var modelData
                    text: modelData.label
                    compact: true
                }
            }
        }
        Flow {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(actionMeasure.implicitWidth, 460)
            spacing: 4
            visible: !Theme.compact && chip.app.contextActions.length > 1
            Repeater {
                model: Theme.compact ? [] : chip.app.contextActions
                delegate: chipAction
            }
        }
        ScrollRow {
            Layout.alignment: Qt.AlignHCenter
            maximumWidth: chip.maximumWidth
            fadeColor: Theme.background
            spacing: 4
            visible: Theme.compact && chip.app.contextActions.length > 1
            Repeater {
                model: Theme.compact ? chip.app.contextActions : []
                delegate: chipAction
            }
        }
    }

    Component {
        id: chipAction
        ActionButton {
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
