// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Floating value editor that follows the manipulator: shows the live value,
// accepts typed input (with units and arithmetic), and offers apply/cancel
// plus the contextual alternatives for the current selection. Main.qml
// places it (interact::placeValueChip): never over the selection, the arrows
// or the tapped point; docked as a bar in a compact window.
Item {
    id: chip

    required property AppController app
    // The widest it may be (a phone's width): the actions below the field
    // then scroll sideways.
    property real maximumWidth: Infinity
    // A phone held sideways: the actions beside the field, so the chip is a
    // single bar (it docks in the top bar's row or above the hint).
    property bool singleRow: false
    // Where the field and the actions sit when the chip is wider than they
    // are: toward the arrow tip (Qt.AlignLeft when the chip is right of it).
    property int alignment: Qt.AlignHCenter
    // The value is being typed (the field has the focus): on a touch screen
    // the on-screen keyboard is up, so a docked chip goes to the top.
    readonly property bool typing: field.activeFocus
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

    // One column: the field, an error, the actions. In a single row the
    // actions sit beside the field and an error goes below both.
    GridLayout {
        id: column
        columns: chip.singleRow ? 2 : 1
        rowSpacing: 6
        columnSpacing: 6

        Panel {
            id: fieldPanel
            Layout.row: 0
            Layout.column: 0
            Layout.alignment: chip.alignment
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
                    objectName: "valueChipField"
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
                    // Operations with several fields (the Hole tool's
                    // diameter, depth, X, Y): Tab goes to the next one, only
                    // with a usable value (it waits for the verdict of a
                    // preview still computing, e.g. a hole off the face).
                    Keys.onTabPressed: (event) => {
                        if (!chip.app.contextActions.some(a => a.id.startsWith("field:"))) {
                            event.accepted = false
                            return
                        }
                        errorText.text = chip.app.confirmValueText(text)
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
                        const error = chip.typeValue(text)
                        if (error.length > 0)
                            return
                        field.focus = false
                        // Refused - its verdict may come only now, when the
                        // value's preview was still computing: stay in the
                        // field with the message, as for a refused preview.
                        if (!chip.app.commitOperation() && chip.app.operationActive) {
                            field.forceActiveFocus()
                            chip.typedTextRefused = false
                            chip.syncFromModel()
                            return
                        }
                        chip.finished()
                        chip.syncFromModel()
                    }
                }
                ActionButton {
                    objectName: "valueChipApply"
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
                    objectName: "valueChipCancel"
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
            Layout.row: 1
            Layout.column: 0
            Layout.columnSpan: chip.singleRow ? 2 : 1
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
            Layout.row: 3
            Layout.column: 0
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
            Layout.row: 2
            Layout.column: 0
            Layout.alignment: chip.alignment
            Layout.preferredWidth: Math.min(actionMeasure.implicitWidth, 460)
            spacing: 4
            visible: !Theme.compact && chip.app.contextActions.length > 1
            Repeater {
                model: Theme.compact ? [] : chip.app.contextActions
                delegate: chipAction
            }
        }
        ScrollRow {
            objectName: "valueChipActions"
            Layout.row: chip.singleRow ? 0 : 2
            Layout.column: chip.singleRow ? 1 : 0
            Layout.alignment: chip.singleRow ? Qt.AlignVCenter : chip.alignment
            maximumWidth: chip.singleRow ? Math.max(0, Math.floor(chip.maximumWidth - fieldPanel.implicitWidth - column.columnSpacing) - 1)
                                         : chip.maximumWidth
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
