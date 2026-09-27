// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

// The app's own numeric keypad, shown on a touch screen instead of the
// system keyboard while a value is typed (the value box, a sketch's live
// values and dimensions, the Model panel's numbers), as Shapr3D does: the
// system keyboard opens on letters and hides half the model. Its keys
// (interact::NumericKeypad, via app.keypadRows / app.keypadPress) are at
// least 44 pt, for a finger or the Apple Pencil. Main.qml places it: docked
// along the bottom of a phone (the value chip is at the top while typing),
// beside the value box on an iPad (and the value chip keeps off it).
//
// It types into a client, a JavaScript object:
//   name        what it is ("valueChip", "sketchInput", "dimension", "parameter")
//   mode()      "length", "angle" or "count": which keys it has
//   hasNext()   there is a next value (a rectangle's height after its width): a Next key
//   text(), replacing()   the text shown, and whether the next key replaces it
//                         (the value as it was, selected)
//   setText(text, replacing)  the keypad changed it: show it and take it as typed
//   next(), done()        Next and the check mark
//   target()    the value box, in window coordinates (the keypad goes beside it)
//   keepClear   what stays in sight: "selection" (the selection and its arrow:
//               the value chip), "sketch" (a sketch's values) or ""
//   display()   (optional) the text to show above the keys (a live value the keypad may cover)
//   keepAbove(y) (optional) a docked keypad's top: the value box moves above it
//   takesFocus  the keypad takes the keys itself (a sketch's live values have no
//               field): a hardware keyboard's keys go to key(event), and the
//               keypad closes when the focus goes elsewhere (a tap in the view)
// A text field client stays focused and read-only on touch (no system
// keyboard); its hardware keys come through hardwareKey().
FocusScope {
    id: keypad

    required property AppController app
    property var client: null
    readonly property bool open: client !== null
    // Main.qml: docked along the bottom (a phone) rather than beside the value box.
    property bool docked: false
    readonly property string mode: client ? client.mode() : "length"
    readonly property bool hasNext: client ? client.hasNext() : false
    readonly property var rows: open ? app.keypadRows(mode, hasNext) : []
    readonly property string displayText: client && client.display ? client.display() : ""
    readonly property int displayHeight: displayText.length > 0 ? 34 + spacing : 0

    readonly property int columns: 5
    readonly property int spacing: 6
    readonly property int padding: 8
    // Apple's minimum touch target is 44 pt: a little more, for a thumb.
    readonly property int keyHeight: 50
    readonly property real keyWidth: docked ? Math.floor((width - 2 * padding - (columns - 1) * spacing) / columns) : 60

    visible: open
    implicitWidth: columns * 60 + (columns - 1) * spacing + 2 * padding
    implicitHeight: displayHeight + rows.length * keyHeight + Math.max(0, rows.length - 1) * spacing + 2 * padding
    height: implicitHeight

    function attach(c) {
        client = c
        if (c.takesFocus)
            keypad.forceActiveFocus()
    }
    function detach(c) {
        if (client === c)
            client = null
    }
    function close() { client = null }
    function serves(name) { return client !== null && client.name === name }

    // A key tapped (or clicked).
    function press(id) {
        if (!client)
            return
        const c = client
        const result = app.keypadPress(c.text(), c.replacing(), id, mode)
        if (result.action === "edited")
            c.setText(result.text, result.replacing)
        else if (result.action === "next")
            c.next()
        else if (result.action === "done")
            c.done()
    }
    // A hardware keyboard while a text field client has the focus (read-only
    // on touch): characters are added as typed, Backspace takes one (or the
    // unit); Enter, Tab and Esc are the field's own. True when taken.
    function hardwareKey(event) {
        if (!client || (event.modifiers & Qt.ControlModifier))
            return false
        if (event.key === Qt.Key_Backspace) {
            press("back")
            return true
        }
        if (event.text.length === 0 || event.text.charCodeAt(0) < 32 || event.text.charCodeAt(0) === 127)
            return false
        const c = client
        const result = app.keypadType(c.text(), c.replacing(), event.text)
        if (result.action === "edited")
            c.setText(result.text, result.replacing)
        return true
    }

    // A client without a field: the keys go to it, and the keypad closes
    // once the focus goes elsewhere (a tap in the view).
    Keys.onPressed: (event) => {
        if (client && client.takesFocus && client.key(event))
            event.accepted = true
    }
    onActiveFocusChanged: {
        if (!activeFocus && client && client.takesFocus)
            Qt.callLater(() => { if (!keypad.activeFocus && keypad.client && keypad.client.takesFocus) keypad.close() })
    }

    Panel {
        anchors.fill: parent
    }

    Column {
        x: keypad.padding
        y: keypad.padding
        spacing: keypad.spacing
        // What is typed, when the value itself may be out of sight.
        Rectangle {
            objectName: "keypadDisplay"
            visible: keypad.displayText.length > 0
            width: keypad.width - 2 * keypad.padding
            height: 34
            radius: 8
            color: "white"
            border.color: Theme.accent
            border.width: 1.5
            Text {
                anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                text: keypad.displayText
                textFormat: Text.PlainText
                font.pixelSize: 17
                color: Theme.text
            }
        }
        Repeater {
            model: keypad.rows
            delegate: Row {
                id: keyRow
                required property var modelData
                spacing: keypad.spacing
                Repeater {
                    model: keyRow.modelData
                    delegate: Rectangle {
                        id: key
                        required property var modelData
                        objectName: "keypadKey_" + modelData.id
                        width: modelData.span * keypad.keyWidth + (modelData.span - 1) * keypad.spacing
                        height: keypad.keyHeight
                        radius: 10
                        color: modelData.accent ? (tap.pressed ? Theme.accentPressed : Theme.accent)
                             : tap.pressed ? Theme.pressed : "white"
                        border.color: modelData.accent ? "transparent" : Theme.panelBorder
                        Text {
                            anchors.centerIn: parent
                            text: key.modelData.label
                            textFormat: Text.PlainText
                            font.pixelSize: key.modelData.id.length === 1 ? 22 : 17
                            font.weight: key.modelData.accent ? Font.DemiBold : Font.Normal
                            color: key.modelData.accent ? "white" : Theme.text
                        }
                        // Takes no focus: the field keeps it (and with it the keypad).
                        MouseArea {
                            id: tap
                            anchors.fill: parent
                            onClicked: keypad.press(key.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
