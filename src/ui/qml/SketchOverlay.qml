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
    // The numeric keypad (Main.qml): on a touch screen a tap on a live value
    // or a dimension types it there.
    property NumericKeypad keypad: null
    readonly property bool usesKeypad: Theme.touch && keypad !== null
    signal finished()

    // The top row's free room (Main.qml): the tool bar sits there, centered,
    // or below the top bar in a compact window when it does not fit.
    property real topRowLeft: 0
    property real topRowRight: width
    property real topRowBottom: 0
    // Where this overlay's things along the bottom edge start (compact
    // layout: the tool strip and the constraint actions above it).
    readonly property real bottomStackTop: actionPanel.visible && Theme.compact ? actionPanel.y : toolPanel.y

    // Characters typed while drawing go to the focused value (e.g. width).
    // The shape takes them once typing pauses (app.typeSketchValue), or at
    // once with Tab, Enter or a tap that places the next point.
    property string typing: ""
    readonly property string typingError: app.sketchMode ? app.typedValueError : ""
    function typeKeys(text) {
        typing = text
        app.typeSketchValue(text)
    }

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
                return true
            }
            if (event.key === Qt.Key_Backspace) {
                typeKeys(typing.slice(0, -1))
                return true
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                app.commitSketchTool()
                // A typed value refused stays with its message; otherwise the shape is done.
                if (app.typedValueError.length === 0)
                    typing = ""
                return true
            }
            if (event.key === Qt.Key_Escape) {
                typing = ""
                return app.handleKey(event.key)
            }
            if (event.text.length === 1 && /[0-9.,+\-*/()a-zA-Z" ]/.test(event.text)
                    && !(event.modifiers & Qt.ControlModifier)) {
                typeKeys(typing + event.text)
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
                // The shape is done (or gone): the keypad for its values goes.
                if (overlay.keypad && overlay.keypad.serves("sketchInput")) {
                    overlay.keypad.close()
                    overlay.finished() // the keys go back to the view
                }
            }
        }
    }

    // ---- The keypad for the live values of the shape being drawn (touch)
    // The focused live value: its label (sketchLabels), or undefined.
    function focusedInput() {
        return app.sketchLabels.find(l => l.kind === "input" && l.focused)
    }
    readonly property var inputKeypadClient: ({
        name: "sketchInput",
        mode: () => {
            const input = overlay.focusedInput()
            return input && input.keypadMode ? input.keypadMode : "length"
        },
        hasNext: () => overlay.app.sketchLabels.filter(l => l.kind === "input").length > 1,
        text: () => overlay.typing,
        replacing: () => overlay.typing.length === 0,
        setText: (text, replacing) => overlay.typeKeys(text),
        next: () => {
            overlay.app.focusNextSketchInput()
            overlay.typing = ""
        },
        done: () => {
            overlay.app.commitSketchTool()
            if (overlay.app.typedValueError.length === 0)
                overlay.typing = ""
        },
        target: () => {
            const input = overlay.focusedInput()
            return input ? Qt.rect(input.x - 60, input.y - 16, 120, 32) : Qt.rect(overlay.width / 2, overlay.height / 2, 0, 0)
        },
        keepClear: "sketch",
        takesFocus: true,
        key: (event) => overlay.handleKey(event),
        // The keypad shows what is typed too: a phone's docked keypad may cover the live value.
        display: () => {
            const input = overlay.focusedInput()
            return overlay.typing.length > 0 ? overlay.typing : input ? input.text : ""
        }
    })
    // A live value tapped: the keypad types into it.
    function typeIntoInput(key) {
        if (!app.focusSketchInput(key))
            return
        typing = ""
        keypad.attach(inputKeypadClient)
    }

    // ------------------------------------------------------------ tool bar
    Item {
        // Where the tool bar goes: the whole top row (centered in the window),
        // or in a compact window without room there, below the top bar.
        id: toolbarSlot
        readonly property bool fitsTopRow: !Theme.compact
            || ((overlay.width - toolbar.width) / 2 >= overlay.topRowLeft
                && (overlay.width + toolbar.width) / 2 <= overlay.topRowRight)
        x: fitsTopRow ? 0 : Theme.insetLeft
        y: fitsTopRow ? Theme.insetTop : overlay.topRowBottom + 8
        width: fitsTopRow ? overlay.width : toolbar.width
        height: toolbar.height
    }
    Panel {
        id: toolbar
        objectName: "sketchToolbar"
        anchors { horizontalCenter: toolbarSlot.horizontalCenter; top: toolbarSlot.top }
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
    // window is too short for touch-sized buttons. In a compact window (a
    // phone): a strip along the bottom edge that scrolls sideways.
    Panel {
        id: toolPanel
        objectName: "sketchToolPanel"
        readonly property real minY: Theme.insetTop + Theme.margin + Theme.controlHeight + 2 * Theme.panelPadding
        readonly property real maxBottom: overlay.height - Theme.safeBottom - 64
        readonly property real stripWidth: overlay.width - Theme.insetLeft - Theme.insetRight
        x: Theme.compact ? Theme.insetLeft + Math.max(0, (stripWidth - width) / 2) : Theme.insetLeft
        y: Theme.compact ? overlay.height - Theme.insetBottom - height
                         : Math.max(minY, Math.min((overlay.height - height) / 2, maxBottom - height))
        width: Theme.compact ? Math.min(toolColumn.implicitWidth + 2 * Theme.panelPadding, stripWidth)
                             : toolColumn.implicitWidth + 2 * Theme.panelPadding
        height: Theme.compact ? Theme.controlHeight + 2 * Theme.panelPadding
                              : Math.min(toolColumn.implicitHeight + 2 * Theme.panelPadding, maxBottom - minY)

        Flickable {
            id: toolScroll
            objectName: "sketchToolScroll"
            anchors { fill: parent; margins: Theme.panelPadding }
            contentWidth: Theme.compact ? toolColumn.implicitWidth : width
            contentHeight: Theme.compact ? height : toolColumn.implicitHeight
            clip: true
            interactive: Theme.compact ? contentWidth > width : contentHeight > height
            flickableDirection: Theme.compact ? Flickable.HorizontalFlick : Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            // The active tool stays in view (a key picked it, or the layout
            // switched between the column and the strip).
            property Item current: null
            function reveal(item) {
                current = item
                if (!item || !item.visible)
                    return
                const p = item.mapToItem(contentItem, 0, 0)
                if (Theme.compact) {
                    if (p.x < contentX)
                        contentX = Math.max(0, p.x - 8)
                    else if (p.x + item.width > contentX + width)
                        contentX = Math.max(0, Math.min(contentWidth - width, p.x + item.width - width + 8))
                } else {
                    if (p.y < contentY)
                        contentY = Math.max(0, p.y - 8)
                    else if (p.y + item.height > contentY + height)
                        contentY = Math.max(0, Math.min(contentHeight - height, p.y + item.height - height + 8))
                }
            }
            Connections {
                target: Theme
                function onCompactChanged() { Qt.callLater(toolScroll.reveal, toolScroll.current) }
            }

            GridLayout {
                id: toolColumn
                // One column, or one row in the compact strip.
                flow: Theme.compact ? GridLayout.LeftToRight : GridLayout.TopToBottom
                width: Theme.compact ? implicitWidth : Math.max(implicitWidth, toolScroll.width)
                height: Theme.compact ? toolScroll.height : implicitHeight
                rowSpacing: 4
                columnSpacing: 4
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
                    delegate: GridLayout {
                        required property var modelData
                        readonly property bool sectionStart: modelData.section !== undefined
                        flow: Theme.compact ? GridLayout.LeftToRight : GridLayout.TopToBottom
                        Layout.fillWidth: !Theme.compact
                        Layout.fillHeight: Theme.compact
                        rowSpacing: 4
                        columnSpacing: 4
                        SectionLabel {
                            visible: parent.sectionStart && !Theme.compact
                            text: parent.sectionStart ? modelData.section : ""
                            Layout.topMargin: modelData.section === "Draw" ? 0 : 6
                        }
                        // The strip has no room for section names: a line between Draw and Edit.
                        Separator { visible: Theme.compact && parent.sectionStart && modelData.section !== "Draw" }
                        ActionButton {
                            objectName: "tool_" + modelData.id
                            text: modelData.label
                            Layout.fillWidth: !Theme.compact
                            checked: overlay.app.sketchTool === modelData.id
                            onCheckedChanged: if (checked) Qt.callLater(toolScroll.reveal, this)
                            Component.onCompleted: if (checked) Qt.callLater(toolScroll.reveal, this)
                            onClicked: overlay.app.setSketchTool(modelData.id)
                            ToolTip.visible: hovered && !Theme.touch
                            ToolTip.text: modelData.label + " (" + modelData.key + ")"
                            ToolTip.delay: 500
                        }
                    }
                }
            }
        }
        // More tools below (to the right in the strip): a fade at that edge says "scroll".
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 28
            radius: 12
            visible: !Theme.compact && toolScroll.contentY + toolScroll.height < toolScroll.contentHeight - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00F9FAFB" }
                GradientStop { position: 1.0; color: Theme.panel }
            }
        }
        Rectangle {
            anchors { top: parent.top; bottom: parent.bottom; right: parent.right; margins: 1 }
            width: 32
            radius: 12
            visible: Theme.compact && toolScroll.contentX + toolScroll.width < toolScroll.contentWidth - 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#00F9FAFB" }
                GradientStop { position: 1.0; color: Theme.panel }
            }
        }
        Rectangle {
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left; margins: 1 }
            width: 32
            radius: 12
            visible: Theme.compact && toolScroll.contentX > 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.panel }
                GradientStop { position: 1.0; color: "#00F9FAFB" }
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
                ToolTip.visible: hovered && !Theme.touch
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
                ToolTip.visible: hovered && !Theme.touch
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
                    objectName: pill.isInput ? "sketchInput_" + labelItem.modelData.key : ""
                    anchors.fill: parent
                    // A finger-sized target for a live value (44 px high).
                    anchors.margins: pill.isInput ? -10 : 0
                    enabled: labelItem.modelData.kind === "dimension" || (pill.isInput && overlay.usesKeypad)
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (pill.isInput) {
                            overlay.typeIntoInput(labelItem.modelData.key)
                            return
                        }
                        dimensionEditor.open(labelItem.modelData.constraint, labelItem.modelData.text,
                                             labelItem.x + labelItem.width / 2, labelItem.y + labelItem.height / 2)
                    }
                }
            }
        }
    }

    // Typing error while drawing.
    Text {
        visible: overlay.typingError.length > 0 && overlay.app.sketchDrawing
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom
                  bottomMargin: Theme.compact ? overlay.height - overlay.bottomStackTop + 30 : 100 + Theme.safeBottom }
        text: overlay.typingError
        color: Theme.error
        font.pixelSize: 12
    }

    // ------------------------------------------------------------ dimension editor
    TextField {
        id: dimensionEditor
        objectName: "dimensionEditor"
        property int constraintId: 0
        property bool isAngle: false
        visible: false
        width: overlay.usesKeypad ? 120 : 96
        height: overlay.usesKeypad ? 40 : 30
        font.pixelSize: overlay.usesKeypad ? 16 : 13
        horizontalAlignment: TextInput.AlignHCenter
        selectByMouse: !overlay.usesKeypad
        // Touch: the numeric keypad types (read-only: no system keyboard).
        readOnly: overlay.usesKeypad
        inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        readonly property var keypadClient: ({
            name: "dimension",
            mode: () => dimensionEditor.isAngle ? "angle" : "length",
            hasNext: () => false,
            text: () => dimensionEditor.text,
            replacing: () => dimensionEditor.text.length > 0 && dimensionEditor.selectedText === dimensionEditor.text,
            setText: (text, replacing) => {
                dimensionEditor.text = text
                if (replacing)
                    dimensionEditor.selectAll()
                else
                    dimensionEditor.cursorPosition = text.length
            },
            next: () => {},
            done: () => dimensionEditor.apply(),
            // A keypad over the field (a phone's): the field moves out from
            // under it, above it when there is room below the top bar,
            // otherwise beside it.
            avoidKeypad: (pad) => dimensionEditor.moveOffKeypad(pad),
            target: () => dimensionEditor.mapToItem(null, 0, 0, dimensionEditor.width, dimensionEditor.height),
            keepClear: "sketch",
            takesFocus: false
        })
        Keys.onPressed: (event) => {
            if (overlay.usesKeypad && overlay.keypad.serves("dimension") && overlay.keypad.hardwareKey(event))
                event.accepted = true
        }
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: dimensionError.text.length > 0 ? Theme.error : Theme.accent
            border.width: 1.5
        }
        function open(id, text, cx, cy) {
            constraintId = id
            isAngle = text.indexOf("°") >= 0
            this.text = text.replace("Ø", "").replace("°", "")
            x = cx - width / 2
            y = cy - height / 2
            dimensionError.text = ""
            visible = true
            forceActiveFocus()
            selectAll()
            if (overlay.usesKeypad)
                overlay.keypad.attach(keypadClient)
        }
        function moveOffKeypad(pad) {
            const e = mapToItem(null, 0, 0, width, height)
            if (!(e.x < pad.x + pad.width && pad.x < e.x + e.width && e.y < pad.y + pad.height && pad.y < e.y + e.height))
                return
            const topRoom = Theme.insetTop + Theme.controlHeight + 2 * Theme.panelPadding + 8
            if (pad.y - height - 24 >= topRoom)
                y += pad.y - height - 24 - e.y
            else if (pad.x + pad.width + 8 + width <= overlay.width - Theme.insetRight)
                x += pad.x + pad.width + 8 - e.x
            else
                x += Math.max(Theme.insetLeft, pad.x - width - 8) - e.x
        }
        function close() {
            if (overlay.keypad)
                overlay.keypad.detach(keypadClient)
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
    // Bottom center; in a compact window above the tool strip, scrolling
    // sideways when there are more than fit.
    Item {
        id: actionSlot
        x: Theme.compact ? Theme.insetLeft : 0
        width: Theme.compact ? actionPanel.width : overlay.width
        height: actionPanel.height
        y: (Theme.compact ? toolPanel.y - 8 : overlay.height - Theme.safeBottom - 64) - height
    }
    Panel {
        id: actionPanel
        objectName: "sketchActions"
        anchors { horizontalCenter: actionSlot.horizontalCenter; bottom: actionSlot.bottom }
        visible: overlay.app.contextActions.length > 0
        width: actionRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding
        ScrollRow {
            id: actionRow
            anchors.centerIn: parent
            maximumWidth: Theme.compact ? overlay.width - Theme.insetLeft - Theme.insetRight - 2 * Theme.panelPadding : Infinity
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
