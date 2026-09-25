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
            const tools = { "l": "line", "r": "rectangle", "c": "circle", "a": "arc", "s": "select" }
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
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
                Layout.leftMargin: 8
                Layout.rightMargin: 6
            }
            Separator {}
            Repeater {
                model: [
                    { id: "select", label: "Select", key: "S" },
                    { id: "line", label: "Line", key: "L" },
                    { id: "rectangle", label: "Rectangle", key: "R" },
                    { id: "circle", label: "Circle", key: "C" },
                    { id: "arc", label: "Arc", key: "A" }
                ]
                delegate: ActionButton {
                    required property var modelData
                    objectName: "tool_" + modelData.id
                    text: modelData.label
                    checked: overlay.app.sketchTool === modelData.id
                    onClicked: overlay.app.setSketchTool(modelData.id)
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.label + " (" + modelData.key + ")"
                    ToolTip.delay: 500
                }
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

    // ------------------------------------------------------------ labels
    Repeater {
        model: overlay.app.sketchLabels
        delegate: Item {
            id: labelItem
            required property var modelData
            x: modelData.x - width / 2
            y: modelData.y - height / 2
            width: pill.width
            height: pill.height
            visible: !(dimensionEditor.visible && dimensionEditor.constraintId === modelData.constraint
                       && modelData.kind === "dimension")

            Rectangle {
                id: pill
                readonly property bool isHint: labelItem.modelData.kind === "hint"
                readonly property bool isInput: labelItem.modelData.kind === "input"
                width: label.implicitWidth + (isHint ? 12 : 16)
                height: isHint ? 20 : 24
                radius: height / 2
                color: isHint ? "transparent"
                     : isInput && labelItem.modelData.focused ? "white" : Theme.panel
                border.color: isInput && labelItem.modelData.focused ? Theme.accent
                            : isHint ? "transparent" : Theme.panelBorder
                border.width: isInput && labelItem.modelData.focused ? 1.5 : 1
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: pill.isInput && labelItem.modelData.focused && overlay.typing.length > 0
                          ? overlay.typing : labelItem.modelData.text
                    font.pixelSize: pill.isHint ? 11 : 12
                    font.weight: labelItem.modelData.locked ? Font.DemiBold : Font.Normal
                    color: pill.isHint ? Theme.accent : Theme.text
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
            this.text = text.replace("Ø", "")
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
                    text: modelData.label
                    onClicked: overlay.app.triggerAction(modelData.id)
                }
            }
        }
    }
}
