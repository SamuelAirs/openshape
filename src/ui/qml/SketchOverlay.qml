// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Everything drawn over the viewport while a sketch is being edited:
// the tool bar, dimension labels (click to edit), live values of the shape
// being drawn, inference hints, and the sketch's constraint status.
Item {
    id: overlay

    required property AppController app
    signal finished()

    // Characters typed while drawing go to the focused value (e.g. width).
    property string typing: ""
    property string typingError: ""

    function handleKey(event) {
        if (!app.sketchMode)
            return false
        // +/- step the counter (a polygon's sides) unless a value is being typed.
        if (app.sketchCounterVisible && typing.length === 0 && !(event.modifiers & Qt.ControlModifier)
                && (event.text === "+" || event.text === "=" || event.text === "-")) {
            app.stepSketchCounter(event.text === "-" ? -1 : 1)
            return true
        }
        if (app.sketchDrawing) {
            if (event.key === Qt.Key_Tab) {
                app.focusNextSketchInput()
                typing = ""
                typingError = ""
                return true
            }
            if (event.key === Qt.Key_Backspace) {
                typing = typing.slice(0, -1)
                typingError = app.sketchType(typing)
                return true
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                app.commitSketchTool()
                typing = ""
                typingError = ""
                return true
            }
            if (event.key === Qt.Key_Escape) {
                typing = ""
                typingError = ""
                return app.handleKey(event.key)
            }
            if (event.text.length === 1 && /[0-9.,+\-*/()a-zA-Z" ]/.test(event.text)
                    && !(event.modifiers & Qt.ControlModifier)) {
                typing += event.text
                typingError = app.sketchType(typing)
                return true
            }
        }
        if (!(event.modifiers & Qt.ControlModifier)) {
            const tools = { "l": "line", "r": "rectangle", "e": "centerRectangle", "p": "polygon", "c": "circle",
                            "a": "arc", "g": "tangentArc", "o": "slot", "t": "trim", "s": "select" }
            const tool = tools[event.text.toLowerCase()]
            if (tool !== undefined && !app.sketchDrawing) {
                app.setSketchTool(tool)
                return true
            }
        }
        if (event.key === Qt.Key_Escape)
            typing = ""
        return app.handleKey(event.key)
    }

    Connections {
        target: overlay.app
        function onStateChanged() {
            if (!overlay.app.sketchDrawing) {
                overlay.typing = ""
                overlay.typingError = ""
            }
        }
    }

    // ------------------------------------------------------------ tool bar
    Panel {
        id: toolbar
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.margin }
        width: toolRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding

        RowLayout {
            id: toolRow
            anchors.centerIn: parent
            spacing: 2
            Text {
                text: overlay.app.sketchName
                textFormat: Text.PlainText // a name from the project
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
                Layout.leftMargin: 8
                Layout.rightMargin: 6
            }
            Separator {}
            ActionButton {
                objectName: "finishSketchButton"
                text: "Finish sketch"
                accent: true
                onClicked: {
                    overlay.app.finishSketch()
                    overlay.finished()
                }
            }
        }
    }

    // ------------------------------------------------------------ tool palette
    // On the left like the Create palette in model mode (a row of ten tools no
    // longer fits between the top bar and the Model panel); scrolls when the
    // window is too short for touch-sized buttons.
    Panel {
        id: toolPanel
        objectName: "sketchToolPanel"
        readonly property real minY: 2 * Theme.margin + Theme.controlHeight + 2 * Theme.panelPadding
        readonly property real maxBottom: overlay.height - 64
        anchors { left: parent.left; leftMargin: Theme.margin }
        y: Math.max(minY, Math.min((overlay.height - height) / 2, maxBottom - height))
        width: toolColumn.implicitWidth + 2 * Theme.panelPadding
        height: Math.min(toolColumn.implicitHeight + 2 * Theme.panelPadding, maxBottom - minY)

        Flickable {
            id: toolScroll
            anchors { fill: parent; margins: Theme.panelPadding }
            contentWidth: width
            contentHeight: toolColumn.implicitHeight
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: toolColumn
                width: Math.max(implicitWidth, toolScroll.width)
                spacing: 4
                Repeater {
                    model: [
                        { section: "Draw", id: "line", label: "Line", key: "L" },
                        { id: "rectangle", label: "Rectangle", key: "R" },
                        { id: "centerRectangle", label: "Center rectangle", key: "E" },
                        { id: "polygon", label: "Polygon", key: "P" },
                        { id: "circle", label: "Circle", key: "C" },
                        { id: "arc", label: "Arc", key: "A" },
                        { id: "tangentArc", label: "Tangent arc", key: "G" },
                        { id: "slot", label: "Slot", key: "O" },
                        { section: "Edit", id: "select", label: "Select", key: "S" },
                        { id: "trim", label: "Trim", key: "T" }
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        SectionLabel {
                            visible: modelData.section !== undefined
                            text: modelData.section !== undefined ? modelData.section : ""
                            Layout.topMargin: modelData.section === "Draw" ? 0 : 6
                        }
                        ActionButton {
                            objectName: "tool_" + modelData.id
                            text: modelData.label
                            Layout.fillWidth: true
                            checked: overlay.app.sketchTool === modelData.id
                            onClicked: overlay.app.setSketchTool(modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: modelData.label + " (" + modelData.key + ")"
                            ToolTip.delay: 500
                        }
                    }
                }
            }
        }
        // More tools below: a fade at the bottom edge says "scroll".
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 28
            radius: 12
            visible: toolScroll.contentY + toolScroll.height < toolScroll.contentHeight - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00F9FAFB" }
                GradientStop { position: 1.0; color: Theme.panel }
            }
        }
    }

    // Constraint status under the tool bar.
    Rectangle {
        anchors { horizontalCenter: toolbar.horizontalCenter; top: toolbar.bottom; topMargin: 8 }
        visible: overlay.app.sketchStatus.length > 0
        width: statusText.implicitWidth + 24
        height: 26
        radius: 13
        color: overlay.app.sketchStatus === "Fully defined" ? "#E3F3E7"
             : overlay.app.sketchStatus === "Conflicting constraints" ? "#FBE3E3" : Theme.panel
        border.color: Theme.panelBorder
        Text {
            id: statusText
            anchors.centerIn: parent
            text: overlay.app.sketchStatus
            font.pixelSize: 12
            color: overlay.app.sketchStatus === "Fully defined" ? "#1E7A3A"
                 : overlay.app.sketchStatus === "Conflicting constraints" ? Theme.error : Theme.mutedText
        }
    }

    // A count with -/+ buttons (a polygon's sides): touch has no +/- keys.
    Panel {
        id: counterPanel
        objectName: "sketchCounter"
        anchors { horizontalCenter: toolbar.horizontalCenter; top: toolbar.bottom; topMargin: 42 }
        visible: overlay.app.sketchCounterVisible
        width: counterRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding
        Row {
            id: counterRow
            anchors.centerIn: parent
            spacing: 4
            ActionButton {
                objectName: "sketchCounterMinus"
                text: "\u2212"
                onClicked: overlay.app.stepSketchCounter(-1)
                ToolTip.visible: hovered
                ToolTip.text: "Fewer (-)"
                ToolTip.delay: 500
            }
            Text {
                objectName: "sketchCounterText"
                width: Math.max(implicitWidth, 64)
                height: parent.height
                text: overlay.app.sketchCounterText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 13
                color: Theme.text
            }
            ActionButton {
                objectName: "sketchCounterPlus"
                text: "+"
                onClicked: overlay.app.stepSketchCounter(1)
                ToolTip.visible: hovered
                ToolTip.text: "More (+)"
                ToolTip.delay: 500
            }
        }
    }

    // ------------------------------------------------------------ labels
    Repeater {
        model: overlay.app.sketchLabels
        delegate: Item {
            id: labelItem
            required property var modelData
            readonly property bool isConstraint: modelData.kind === "constraint"
            objectName: isConstraint ? "constraintIcon_" + modelData.constraint
                      : modelData.kind === "dimension" ? "dimensionLabel_" + modelData.constraint : ""
            x: modelData.x - width / 2
            y: modelData.y - height / 2
            width: isConstraint ? badge.width : pill.width
            height: isConstraint ? badge.height : pill.height
            visible: !(dimensionEditor.visible && dimensionEditor.constraintId === modelData.constraint
                       && modelData.kind === "dimension")

            // A constraint glyph: tap or click it with the Select tool to select the
            // constraint (Delete removes it). It takes no input itself: the sketch
            // session resolves the tap (a point or curve within reach wins, then the
            // glyph, with a 24 px or, in the touch layout, 40 px target), so a
            // glyph never steals a tap meant for the geometry beside it.
            Rectangle {
                id: badge
                visible: labelItem.isConstraint
                width: Math.max(18, glyph.implicitWidth + 8)
                height: 18
                radius: 5
                readonly property bool hot: labelItem.modelData.hot === true
                color: labelItem.modelData.selected ? Theme.accent : "white"
                border.color: labelItem.modelData.selected || hot ? Theme.accent : Theme.panelBorder
                opacity: labelItem.modelData.selected || hot ? 1.0 : 0.85
                Text {
                    id: glyph
                    anchors.centerIn: parent
                    text: labelItem.isConstraint ? labelItem.modelData.text : ""
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: labelItem.modelData.selected ? "white" : badge.hot ? Theme.accent : "#4A5360"
                }
            }

            Rectangle {
                id: pill
                visible: !labelItem.isConstraint
                readonly property bool isHint: labelItem.modelData.kind === "hint"
                readonly property bool isInput: labelItem.modelData.kind === "input"
                width: label.implicitWidth + (caption.visible ? caption.implicitWidth + 4 : 0) + (isHint ? 12 : 16)
                height: isHint ? 20 : 24
                radius: height / 2
                color: isHint ? "transparent"
                     : isInput && labelItem.modelData.focused ? "white" : Theme.panel
                border.color: isInput && labelItem.modelData.focused ? Theme.accent
                            : isHint ? "transparent" : Theme.panelBorder
                border.width: isInput && labelItem.modelData.focused ? 1.5 : 1
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    Text {
                        id: label
                        text: pill.isInput && labelItem.modelData.focused && overlay.typing.length > 0
                              ? overlay.typing : labelItem.modelData.text
                        font.pixelSize: pill.isHint ? 11 : 12
                        font.weight: labelItem.modelData.locked ? Font.DemiBold : Font.Normal
                        color: pill.isHint ? Theme.accent : Theme.text
                    }
                    // What the value is ("across flats", "sides").
                    Text {
                        id: caption
                        visible: text.length > 0
                        text: labelItem.modelData.caption !== undefined ? labelItem.modelData.caption : ""
                        font.pixelSize: 11
                        color: Theme.mutedText
                        anchors.verticalCenter: label.verticalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: labelItem.modelData.kind === "dimension"
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: dimensionEditor.open(labelItem.modelData.constraint, labelItem.modelData.text,
                                                    labelItem.x + labelItem.width / 2, labelItem.y + labelItem.height / 2)
                }
            }
        }
    }

    // Typing error while drawing.
    Text {
        visible: overlay.typingError.length > 0 && overlay.app.sketchDrawing
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 100 }
        text: overlay.typingError
        color: Theme.error
        font.pixelSize: 12
    }

    // ------------------------------------------------------------ dimension editor
    TextField {
        id: dimensionEditor
        property int constraintId: 0
        visible: false
        width: 96
        height: 30
        font.pixelSize: 13
        horizontalAlignment: TextInput.AlignHCenter
        selectByMouse: true
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: dimensionError.text.length > 0 ? Theme.error : Theme.accent
            border.width: 1.5
        }
        function open(id, text, cx, cy) {
            constraintId = id
            this.text = text.replace("Ø", "").replace("°", "")
            x = cx - width / 2
            y = cy - height / 2
            dimensionError.text = ""
            visible = true
            forceActiveFocus()
            selectAll()
        }
        function close() {
            visible = false
            overlay.finished()
        }
        Keys.onReturnPressed: apply()
        Keys.onEnterPressed: apply()
        Keys.onEscapePressed: close()
        onActiveFocusChanged: if (!activeFocus && visible) close()
        function apply() {
            const error = overlay.app.setSketchDimension(constraintId, text)
            dimensionError.text = error
            if (error.length === 0)
                close()
        }
    }
    Text {
        id: dimensionError
        visible: dimensionEditor.visible && text.length > 0
        x: dimensionEditor.x
        y: dimensionEditor.y + dimensionEditor.height + 4
        color: Theme.error
        font.pixelSize: 11
    }

    // ------------------------------------------------------------ constraint actions
    Panel {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 64 }
        visible: overlay.app.contextActions.length > 0
        width: actionRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding
        Row {
            id: actionRow
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: overlay.app.contextActions
                delegate: ActionButton {
                    required property var modelData
                    objectName: "sketchAction_" + modelData.id
                    text: modelData.label
                    checked: modelData.active
                    onClicked: overlay.app.triggerAction(modelData.id)
                }
            }
        }
    }
}
