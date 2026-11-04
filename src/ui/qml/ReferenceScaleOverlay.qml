// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Set scale on a reference photo (AppController.referenceScale): the two
// points tapped on the photo, the line between them and what it measures
// now, a loupe while a finger places a point, and a card with the step's
// hint, then the real distance to type (the numeric keypad on a touch
// screen). The points themselves are taken by the view (ViewportItem); this
// overlay takes no input outside its card.
Item {
    id: overlay

    required property AppController app
    property Item viewportItem: null
    property NumericKeypad keypad: null
    // Where the card goes (below the top bar).
    property real cardTop: Theme.insetTop + 60
    // How far right the card may reach (a phone's view cube is beside it).
    property real cardRight: width - Theme.insetRight

    readonly property var info: app.referenceScale
    readonly property bool active: info.active === true
    readonly property var points: active && info.points ? info.points : []
    readonly property var cursor: active && info.cursor ? info.cursor : null
    readonly property var lineEnd: points.length >= 2 ? points[1] : cursor
    readonly property bool typing: active && info.step === 2

    visible: active
    z: 6

    // ---- the line between the points (to the cursor while the second is chosen)
    Rectangle {
        readonly property var a: overlay.points.length > 0 ? overlay.points[0] : null
        readonly property var b: overlay.lineEnd
        visible: a !== null && a !== undefined && b !== null && b !== undefined
        x: visible ? a.x : 0
        y: visible ? a.y - height / 2 : 0
        width: visible ? Math.hypot(b.x - a.x, b.y - a.y) : 0
        height: 2.5
        radius: 1.25
        color: Theme.accent
        transformOrigin: Item.Left
        rotation: visible ? Math.atan2(b.y - a.y, b.x - a.x) * 180 / Math.PI : 0
        antialiasing: true
    }
    // The distance as the photo is scaled now, at the line's middle.
    Rectangle {
        readonly property var a: overlay.points.length > 0 ? overlay.points[0] : null
        readonly property var b: overlay.lineEnd
        visible: a !== null && a !== undefined && b !== null && b !== undefined && (overlay.info.measured || "").length > 0
        x: visible ? (a.x + b.x) / 2 - width / 2 : 0
        y: visible ? (a.y + b.y) / 2 - height - 10 : 0
        width: measuredText.implicitWidth + 14
        height: 22
        radius: 11
        color: "white"
        border.color: Theme.accent
        Text {
            id: measuredText
            objectName: "referenceScaleMeasured"
            anchors.centerIn: parent
            text: overlay.info.measured || ""
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: Theme.accent
        }
    }

    // ---- the points: a ring with a cross
    Repeater {
        model: overlay.points
        delegate: Item {
            required property var modelData
            required property int index
            objectName: "referenceScalePoint_" + index
            visible: modelData !== null && modelData !== undefined
            x: visible ? modelData.x - width / 2 : 0
            y: visible ? modelData.y - height / 2 : 0
            width: 22
            height: 22
            Rectangle { anchors.fill: parent; radius: width / 2; color: "#552977ED"; border.color: "white"; border.width: 2 }
            Rectangle { anchors.centerIn: parent; width: 12; height: 1.5; color: "white" }
            Rectangle { anchors.centerIn: parent; width: 1.5; height: 12; color: "white" }
            Rectangle { anchors.centerIn: parent; width: 3; height: 3; radius: 1.5; color: Theme.accent }
        }
    }
    // The mouse's spot before it is clicked.
    Item {
        visible: overlay.cursor !== null && overlay.cursor !== undefined
        x: visible ? overlay.cursor.x - width / 2 : 0
        y: visible ? overlay.cursor.y - height / 2 : 0
        width: 22
        height: 22
        Rectangle { anchors.centerIn: parent; width: 22; height: 1.5; color: Theme.accent }
        Rectangle { anchors.centerIn: parent; width: 1.5; height: 22; color: Theme.accent }
    }

    // ---- the loupe: what is under the finger, magnified beside it
    Item {
        id: loupe
        objectName: "referenceScaleLoupe"
        readonly property var info: overlay.info.loupe || ({})
        readonly property real zoom: info.zoom !== undefined ? info.zoom : 2.5
        visible: overlay.viewportItem !== null && info.visible === true
        width: info.size !== undefined ? info.size : 112
        height: width
        x: (info.x !== undefined ? info.x : 0) - width / 2
        y: (info.y !== undefined ? info.y : 0) - height / 2
        Rectangle { anchors.fill: parent; radius: width / 2; color: Theme.panel; border.color: Theme.panelBorder }
        ShaderEffectSource {
            anchors { fill: parent; margins: 3 }
            sourceItem: loupe.visible ? overlay.viewportItem : null
            sourceRect: Qt.rect(loupe.info.sx - width / (2 * loupe.zoom), loupe.info.sy - height / (2 * loupe.zoom),
                                width / loupe.zoom, height / loupe.zoom)
            live: true
            hideSource: false
            smooth: true
        }
        Rectangle { anchors.centerIn: parent; width: 18; height: 1.5; color: Theme.accent }
        Rectangle { anchors.centerIn: parent; width: 1.5; height: 18; color: Theme.accent }
        Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.color: Theme.accent; border.width: 2 }
    }

    // ---- the card: the hint, then the real distance
    Panel {
        id: card
        objectName: "referenceScaleCard"
        // Centred, unless that reaches past cardRight.
        x: Math.max(Theme.insetLeft, Math.min((overlay.width - width) / 2, overlay.cardRight - width))
        y: overlay.cardTop
        width: Math.min(overlay.cardRight - Theme.insetLeft, Math.max(320, cardColumn.implicitWidth + 24))
        height: cardColumn.implicitHeight + 20
        // The card takes its own presses (they must not reach the view).
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onPressed: (mouse) => mouse.accepted = true }

        ColumnLayout {
            id: cardColumn
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LineIcon { name: "photo"; color: Theme.accent }
                Text {
                    objectName: "referenceScaleHint"
                    Layout.fillWidth: true
                    text: overlay.info.hint || ""
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
            }
            RowLayout {
                visible: overlay.typing
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: "Real distance"
                    font.pixelSize: 12
                    color: Theme.mutedText
                }
                TextField {
                    id: distanceField
                    objectName: "referenceScaleDistance"
                    readonly property bool usesKeypad: Theme.touch && overlay.keypad !== null
                    Layout.fillWidth: true
                    implicitHeight: Theme.touch ? 44 : 30
                    font.pixelSize: 13
                    horizontalAlignment: TextInput.AlignRight
                    placeholderText: overlay.info.measured || ""
                    selectByMouse: !usesKeypad
                    readOnly: usesKeypad
                    inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    background: Rectangle {
                        radius: 6
                        color: "white"
                        border.color: distanceField.activeFocus ? Theme.accent : Theme.panelBorder
                        border.width: distanceField.activeFocus ? 1.5 : 1
                    }
                    readonly property var keypadClient: ({
                        name: "referenceScale",
                        mode: () => "length",
                        hasNext: () => false,
                        text: () => distanceField.text,
                        replacing: () => distanceField.text.length > 0 && distanceField.selectedText === distanceField.text,
                        setText: (text, replacing) => {
                            distanceField.text = text
                            if (replacing)
                                distanceField.selectAll()
                            else
                                distanceField.cursorPosition = text.length
                        },
                        next: () => {},
                        done: () => overlay.apply(),
                        target: () => card.mapToItem(null, 0, 0, card.width, card.height),
                        keepClear: "",
                        takesFocus: false
                    })
                    onActiveFocusChanged: {
                        if (activeFocus && usesKeypad)
                            overlay.keypad.attach(keypadClient)
                        else if (!activeFocus && overlay.keypad)
                            overlay.keypad.detach(keypadClient)
                    }
                    Keys.onReturnPressed: overlay.apply()
                    Keys.onEnterPressed: overlay.apply()
                    Keys.onEscapePressed: overlay.app.cancelReferenceScale()
                    Keys.onPressed: (event) => {
                        if (usesKeypad && overlay.keypad.serves("referenceScale") && overlay.keypad.hardwareKey(event))
                            event.accepted = true
                    }
                }
            }
            Text {
                id: errorText
                objectName: "referenceScaleError"
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: Theme.error
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "referenceScaleBack"
                    visible: (overlay.info.step || 0) > 0
                    compact: !Theme.touch
                    text: "Back"
                    onClicked: {
                        errorText.text = ""
                        overlay.app.undoReferenceScalePoint()
                    }
                }
                ActionButton {
                    objectName: "referenceScaleCancel"
                    compact: !Theme.touch
                    text: "Cancel"
                    onClicked: overlay.app.cancelReferenceScale()
                }
                ActionButton {
                    objectName: "referenceScaleApply"
                    visible: overlay.typing
                    accent: true
                    compact: !Theme.touch
                    text: "Set scale"
                    onClicked: overlay.apply()
                }
            }
        }
    }

    function apply() {
        const error = app.applyReferenceScale(distanceField.text)
        errorText.text = error
        if (error.length === 0 && keypad)
            keypad.detach(distanceField.keypadClient)
    }

    onTypingChanged: {
        errorText.text = ""
        if (typing) {
            distanceField.text = ""
            // Once its row is shown (bindings update in no set order).
            Qt.callLater(() => { if (overlay.typing) distanceField.forceActiveFocus() })
        } else if (keypad) {
            keypad.detach(distanceField.keypadClient)
        }
    }
    onActiveChanged: if (!active && viewportItem) viewportItem.forceActiveFocus()
}
