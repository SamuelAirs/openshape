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
        if (!textField.activeFocus && textField.text !== app.operationText)
            textField.text = app.operationText
    }

    // The Text tool: its words are typed in the text field, which takes the
    // keys as soon as the tool opens (on a tablet the keyboard comes up).
    // Words remembered from the last use start selected: typing replaces them.
    readonly property bool takesText: app.operationTakesText
    onTakesTextChanged: if (takesText) Qt.callLater(chip.startText)
    function startText() {
        if (!chip.takesText)
            return
        focusText()
        textField.selectAll()
    }
    function focusText() {
        if (!chip.takesText)
            return
        textField.text = app.operationText
        textField.forceActiveFocus()
        textField.cursorPosition = textField.text.length
    }
    // The Text tool's Depth, Size and Angle buttons: the value field takes
    // the keys (keys typed in the view go to the words).
    function focusValue() {
        if (!app.operationActive)
            return
        field.text = app.operationValueText
        field.forceActiveFocus()
        field.selectAll()
    }
    // Keys typed while the view has the focus (after a click on the face).
    function typeText(characters) {
        focusText()
        textField.insert(textField.cursorPosition, characters)
        errorText.text = chip.app.setOperationText(textField.text)
    }
    function eraseText() {
        focusText()
        if (textField.cursorPosition > 0)
            textField.remove(textField.cursorPosition - 1, textField.cursorPosition)
        errorText.text = chip.app.setOperationText(textField.text)
    }

    Connections {
        target: chip.app
        function onStateChanged() { chip.syncFromModel() }
    }
    Component.onCompleted: syncFromModel()

    ColumnLayout {
        id: column
        spacing: 6

        // The Text tool's words (the value field below is the depth, size or angle).
        Panel {
            Layout.alignment: Qt.AlignHCenter
            visible: chip.takesText
            implicitWidth: textRow.implicitWidth + 2 * Theme.panelPadding
            implicitHeight: Theme.controlHeight + 2 * Theme.panelPadding
            border.color: errorText.text.length > 0 && textField.activeFocus ? Theme.error : Theme.panelBorder

            RowLayout {
                id: textRow
                anchors.centerIn: parent
                spacing: 6
                Text {
                    text: "Text"
                    color: Theme.mutedText
                    font.pixelSize: 12
                    Layout.leftMargin: 6
                }
                TextField {
                    id: textField
                    objectName: "textToolField"
                    implicitWidth: Math.max(120, Math.min(250, chip.maximumWidth - 90))
                    implicitHeight: Theme.controlHeight
                    font.pixelSize: 15
                    placeholderText: "Type the text"
                    selectByMouse: true
                    color: Theme.text
                    background: Rectangle {
                        radius: 8
                        color: textField.activeFocus ? "white" : Theme.fieldIdle
                        border.color: textField.activeFocus ? Theme.accent : "transparent"
                        border.width: 1.5
                    }
                    onTextEdited: errorText.text = chip.app.setOperationText(text)
                    Keys.onReturnPressed: apply()
                    Keys.onEnterPressed: apply()
                    // Tab goes on to the value (depth, size or angle).
                    Keys.onTabPressed: {
                        field.forceActiveFocus()
                        field.selectAll()
                    }
                    Keys.onEscapePressed: {
                        textField.focus = false
                        chip.finished()
                        chip.syncFromModel()
                    }
                    function apply() {
                        errorText.text = chip.app.setOperationText(text)
                        if (errorText.text.length === 0) {
                            // Nothing typed: the app says so and the tool stays.
                            textField.focus = false
                            chip.app.commitOperation()
                            chip.finished()
                            chip.syncFromModel()
                        }
                    }
                }
            }
        }

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
                if (owner.takesText && id.startsWith("field:"))
                    owner.focusValue()
                else
                    owner.finished()
            }
        }
    }
}
