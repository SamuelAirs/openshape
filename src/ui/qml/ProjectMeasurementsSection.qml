// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The Model panel's "Measurements": the measurements kept in the project
// (docs/MEASURE.md, "Measurements in the project"), each typed by name in
// any value box. A row reads "shelf_depth = 243.0 mm"; a click (a tap)
// opens it to rename it, give it a new value (the numeric keypad on a touch
// screen) or delete it. "+" adds one by hand. Every change is one undo step.
// The rows follow the steps' pattern (HistoryPanel.qml); on touch each is at
// least 44 pt high.
ColumnLayout {
    id: section

    required property AppController app
    // The numeric keypad (Main.qml): on a touch screen values are typed there.
    property NumericKeypad keypad: null
    // Where a keypad beside the panel goes (the panel, in window coordinates).
    property Item host: null
    property string expandedName: ""
    property bool adding: false
    // An edit was applied (the view takes the keys back).
    signal finished()
    // A field got the focus: keep it in sight above a phone's keypad.
    signal revealRequested()

    readonly property var measurements: app.projectMeasurements
    readonly property int rowHeight: Theme.touch ? 44 : 30

    spacing: 2
    // Nothing while the project has none (the panel's header offers
    // "+ Measurement" then): a panel without measurements keeps its height.
    visible: measurements.length > 0 || adding
    height: visible ? implicitHeight : 0

    // The form to add one, empty, its name focused (the "+" here or the panel's header).
    function openAdd() {
        adding = true
        expandedName = ""
        Qt.callLater(() => { addName.text = ""; addValue.text = ""; addError.text = ""; addName.forceActiveFocus() })
    }

    // Names change and rows rebuild: forget an expanded row that is gone.
    onMeasurementsChanged: {
        if (expandedName.length > 0 && !measurements.some(m => m.name === expandedName))
            expandedName = ""
    }

    // A value field: right-aligned, the keypad on touch (read-only there, no
    // system keyboard), as the steps' numbers in HistoryPanel.qml.
    component ValueField: TextField {
        id: field
        property string mode: "length"
        // The keypad's check mark and Enter.
        signal applyRequested()
        readonly property bool usesKeypad: Theme.touch && section.keypad !== null
        Layout.fillWidth: true
        implicitHeight: Theme.touch ? 40 : 30
        font.pixelSize: 13
        horizontalAlignment: TextInput.AlignRight
        selectByMouse: !usesKeypad
        readOnly: usesKeypad
        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        background: Rectangle {
            radius: 6
            color: "white"
            border.color: field.activeFocus ? Theme.accent : Theme.panelBorder
            border.width: field.activeFocus ? 1.5 : 1
        }
        readonly property var keypadClient: ({
            name: "measurement",
            mode: () => field.mode,
            hasNext: () => false,
            text: () => field.text,
            replacing: () => field.text.length > 0 && field.selectedText === field.text,
            setText: (text, replacing) => {
                field.text = text
                if (replacing)
                    field.selectAll()
                else
                    field.cursorPosition = text.length
            },
            next: () => {},
            done: () => field.applyRequested(),
            target: () => section.host ? section.host.mapToItem(null, 0, 0, section.host.width, section.host.height)
                                        : field.mapToItem(null, 0, 0, field.width, field.height),
            keepClear: "",
            takesFocus: false
        })
        onActiveFocusChanged: {
            if (activeFocus) {
                selectAll()
                if (usesKeypad)
                    section.keypad.attach(keypadClient)
                section.revealRequested()
            } else if (section.keypad) {
                section.keypad.detach(keypadClient)
            }
        }
        function release() {
            if (section.keypad)
                section.keypad.detach(keypadClient)
        }
        Keys.onReturnPressed: applyRequested()
        Keys.onEnterPressed: applyRequested()
        // A hardware keyboard on a touch screen (the field is read-only there).
        Keys.onPressed: (event) => {
            if (usesKeypad && section.keypad.serves("measurement") && section.keypad.hardwareKey(event))
                event.accepted = true
        }
    }

    // A name field: the system keyboard (letters), no capitals or corrections.
    component NameField: TextField {
        id: nameField
        Layout.fillWidth: true
        implicitHeight: Theme.touch ? 40 : 30
        font.pixelSize: 13
        selectByMouse: true
        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhPreferLowercase
        background: Rectangle {
            radius: 6
            color: "white"
            border.color: nameField.activeFocus ? Theme.accent : Theme.panelBorder
            border.width: nameField.activeFocus ? 1.5 : 1
        }
        onActiveFocusChanged: if (activeFocus) section.revealRequested()
    }

    // ---- The heading: "Measurements" and "+".
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 6
        SectionLabel { text: "Measurements" }
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "measurementAdd"
            compact: true
            checked: section.adding
            text: "+"
            fontSize: 17
            implicitWidth: section.rowHeight
            implicitHeight: section.rowHeight
            onClicked: {
                if (section.adding)
                    section.adding = false
                else
                    section.openAdd()
            }
        }
    }

    // ---- "+": a name, Length or Angle, a value.
    Rectangle {
        id: addForm
        objectName: "measurementAddForm"
        property string quantity: "length"
        visible: section.adding
        Layout.fillWidth: true
        implicitHeight: addColumn.implicitHeight + 12
        radius: 8
        // White: the Length / Angle choice shows on it (Theme.checked).
        color: Theme.card
        border.color: Theme.panelBorder
        ColumnLayout {
            id: addColumn
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "Name"
                    font.pixelSize: 12
                    color: Theme.mutedText
                    Layout.preferredWidth: 44
                }
                NameField {
                    id: addName
                    objectName: "measurementAddName"
                    placeholderText: "shelf_depth"
                    Keys.onReturnPressed: addValue.forceActiveFocus()
                    Keys.onEnterPressed: addValue.forceActiveFocus()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "Value"
                    font.pixelSize: 12
                    color: Theme.mutedText
                    Layout.preferredWidth: 44
                }
                ActionButton {
                    objectName: "measurementAddLength"
                    compact: true
                    text: "Length"
                    checked: addForm.quantity === "length"
                    onClicked: addForm.quantity = "length"
                }
                ActionButton {
                    objectName: "measurementAddAngle"
                    compact: true
                    text: "Angle"
                    checked: addForm.quantity === "angle"
                    onClicked: addForm.quantity = "angle"
                }
                ValueField {
                    id: addValue
                    objectName: "measurementAddValue"
                    mode: addForm.quantity
                    placeholderText: addForm.quantity === "angle" ? "90°" : "243 mm"
                    onApplyRequested: addButton.add()
                }
            }
            Text {
                id: addError
                objectName: "measurementAddError"
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: Theme.error
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "measurementAddCancel"
                    compact: true
                    text: "Cancel"
                    onClicked: {
                        addValue.release()
                        section.adding = false
                        section.finished()
                    }
                }
                ActionButton {
                    id: addButton
                    objectName: "measurementAddConfirm"
                    compact: true
                    accent: true
                    text: "Add"
                    function add() {
                        const result = section.app.addProjectMeasurementTyped(addName.text, addForm.quantity, addValue.text)
                        if (result.name.length === 0) {
                            addError.text = result.error
                            return
                        }
                        addValue.release()
                        addError.text = ""
                        section.adding = false
                        section.finished()
                    }
                    onClicked: add()
                }
            }
        }
    }

    // ---- The measurements: "shelf_depth = 243.0 mm", a click opens one.
    Repeater {
        model: section.measurements
        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool expanded: section.expandedName === modelData.name
            Layout.fillWidth: true
            implicitHeight: Math.max(section.rowHeight, content.implicitHeight + 12)
            radius: 8
            color: expanded ? Theme.checked : rowMouse.containsMouse ? Theme.hover : "transparent"

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    section.adding = false
                    section.expandedName = row.expanded ? "" : row.modelData.name
                }
            }

            ColumnLayout {
                id: content
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                RowLayout {
                    // The title line (the acceptance run clicks its center).
                    objectName: "measurementRow_" + row.modelData.name
                    Layout.fillWidth: true
                    Layout.minimumHeight: section.rowHeight - 12
                    spacing: 6
                    // Names come from files: plain text, never HTML.
                    Text {
                        objectName: "measurementText_" + row.modelData.name
                        Layout.fillWidth: true
                        text: row.modelData.name + " = " + row.modelData.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        color: Theme.text
                    }
                    Text {
                        visible: row.modelData.note.length > 0
                        text: row.modelData.note
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        Layout.maximumWidth: 90
                        font.pixelSize: 11
                        color: Theme.mutedText
                    }
                }

                // Open: its name and value to change, and Delete.
                RowLayout {
                    visible: row.expanded
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: "Name"
                        font.pixelSize: 12
                        color: Theme.mutedText
                        Layout.preferredWidth: 44
                    }
                    NameField {
                        id: nameEdit
                        objectName: "measurementName_" + row.modelData.name
                        text: row.modelData.name
                        function apply() {
                            // A rename rebuilds the rows (and this delegate): capture first.
                            const owner = section
                            const newName = text.trim()
                            const error = owner.app.renameProjectMeasurement(row.modelData.name, newName)
                            if (error.length > 0) {
                                rowError.text = error
                                return
                            }
                            owner.expandedName = ""
                            owner.finished()
                        }
                        Keys.onReturnPressed: apply()
                        Keys.onEnterPressed: apply()
                        Keys.onEscapePressed: {
                            text = row.modelData.name
                            section.finished()
                        }
                    }
                }
                RowLayout {
                    visible: row.expanded
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: row.modelData.quantity === "angle" ? "Angle" : "Length"
                        font.pixelSize: 12
                        color: Theme.mutedText
                        Layout.preferredWidth: 44
                    }
                    ValueField {
                        id: valueEdit
                        objectName: "measurementValue_" + row.modelData.name
                        mode: row.modelData.quantity
                        text: row.modelData.typed
                        onApplyRequested: {
                            // A new value rebuilds the rows: capture first.
                            const owner = section
                            // As it was shown (rounded to the display): nothing to change.
                            if (text === row.modelData.typed) {
                                release()
                                owner.finished()
                                return
                            }
                            const error = owner.app.setProjectMeasurementValue(row.modelData.name, text)
                            if (error.length > 0) {
                                rowError.text = error
                                return
                            }
                            release()
                            owner.finished()
                        }
                        Keys.onEscapePressed: {
                            text = row.modelData.typed
                            section.finished()
                        }
                    }
                }
                Text {
                    id: rowError
                    objectName: "measurementError_" + row.modelData.name
                    visible: row.expanded && text.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    color: Theme.error
                    font.pixelSize: 11
                }
                Flow {
                    visible: row.expanded
                    Layout.fillWidth: true
                    spacing: 4
                    ActionButton {
                        objectName: "measurementDelete_" + row.modelData.name
                        compact: true
                        text: "Delete"
                        onClicked: {
                            const owner = section
                            const name = row.modelData.name
                            valueEdit.release()
                            owner.expandedName = ""
                            owner.app.removeProjectMeasurement(name)
                            owner.finished()
                        }
                    }
                }
            }
        }
    }

    // ---- How to use them, and what typing one does.
    Text {
        objectName: "measurementHint"
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        Layout.topMargin: 2
        text: "Type a name in any value box, e.g. "
              + (section.measurements.length > 0 ? section.measurements[0].name : "shelf_depth") + " - 2"
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.pixelSize: 11
        color: Theme.mutedText
    }
    Text {
        objectName: "measurementFootnote"
        visible: section.measurements.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8
        Layout.bottomMargin: 4
        text: "The value is copied when typed: changing a measurement later does not change the model."
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        font.pixelSize: 11
        color: Theme.mutedText
    }
}
