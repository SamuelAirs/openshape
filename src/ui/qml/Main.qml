import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import OpenShape

ApplicationWindow {
    id: window

    required property AppController app

    width: 1400
    height: 900
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: (app.dirty ? "• " : "") + app.documentTitle + " — OpenShape"
    color: Theme.background

    property bool closeConfirmed: false
    property var afterSave: null   // action to run once a Save As completes

    // ---------------------------------------------------------------- viewport
    Viewport {
        id: viewport
        anchors.fill: parent
        controller: window.app
        focus: true

        Keys.onPressed: (event) => {
            if (window.app.sketchMode) {
                if (sketchOverlay.handleKey(event))
                    event.accepted = true
                return
            }
            // Typing a number while an operation is armed goes straight into
            // the value field (no need to click it first).
            if (window.app.operationActive && event.text.length === 1 && "0123456789.-+(".indexOf(event.text) >= 0
                    && !(event.modifiers & Qt.ControlModifier)) {
                valueChip.beginTyping(event.text)
                event.accepted = true
                return
            }
            if (window.app.handleKey(event.key))
                event.accepted = true
        }
    }

    // ---------------------------------------------------------------- shortcuts
    Shortcut { sequences: [StandardKey.Undo]; onActivated: window.app.undo() }
    // StandardKey.Redo is Ctrl+Y on Windows but Ctrl+Shift+Z elsewhere; accept
    // both everywhere. On Windows the duplicate makes Qt report the match as
    // ambiguous, so handle that signal too.
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Y"]
        onActivated: window.app.redo()
        onActivatedAmbiguously: window.app.redo()
    }
    Shortcut { sequences: [StandardKey.Save]; onActivated: window.save() }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: window.confirmDiscard(() => openDialog.open()) }
    Shortcut { sequences: [StandardKey.New]; onActivated: window.confirmDiscard(() => window.app.newDocument()) }
    Shortcut { sequence: "F"; enabled: viewport.activeFocus; onActivated: window.app.fitAll() }
    Shortcut { sequence: "F1"; onActivated: helpOverlay.toggle() }
    Shortcut { sequence: "B"; enabled: viewport.activeFocus && !window.app.sketchMode; onActivated: window.app.createBox(20) }
    Shortcut {
        sequence: "K"
        enabled: viewport.activeFocus && window.app.canStartSketch
        onActivated: window.app.startSketch()
    }

    function save() {
        if (app.hasProjectPath())
            app.saveProject()
        else
            saveDialog.open()
    }

    // Runs `action` now if there are no unsaved changes, otherwise asks first.
    function confirmDiscard(action) {
        if (!app.dirty) {
            action()
            return
        }
        unsavedDialog.pendingAction = action
        unsavedDialog.open()
    }

    onClosing: (close) => {
        if (app.dirty && !closeConfirmed) {
            close.accepted = false
            confirmDiscard(() => { window.closeConfirmed = true; window.close() })
        }
    }

    // ---------------------------------------------------------------- top bar
    Panel {
        id: topBar
        anchors { left: parent.left; top: parent.top; margins: Theme.margin }
        height: Theme.controlHeight + 2 * Theme.panelPadding
        width: topRow.implicitWidth + 2 * Theme.panelPadding

        RowLayout {
            id: topRow
            anchors.centerIn: parent
            spacing: 4

            Text {
                text: "OpenShape"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: Theme.text
                Layout.leftMargin: 6
                Layout.rightMargin: 8
            }
            ActionButton { text: "File"; onClicked: fileMenu.popup(this, 0, height + 6) }
            Separator {}
            ActionButton {
                text: "Undo"
                enabled: window.app.canUndo
                onClicked: window.app.undo()
                ToolTip.visible: hovered && window.app.canUndo
                ToolTip.text: "Undo " + window.app.undoText + "  (Ctrl+Z)"
                ToolTip.delay: 500
            }
            ActionButton {
                text: "Redo"
                enabled: window.app.canRedo
                onClicked: window.app.redo()
                ToolTip.visible: hovered && window.app.canRedo
                ToolTip.text: "Redo " + window.app.redoText + "  (Ctrl+Y)"
                ToolTip.delay: 500
            }
            Separator {}
            ActionButton {
                objectName: "helpButton"
                text: "?"
                implicitWidth: Theme.controlHeight
                onClicked: helpOverlay.toggle()
                ToolTip.visible: hovered
                ToolTip.text: "How OpenShape works (F1)"
                ToolTip.delay: 500
            }
        }
    }

    Menu {
        id: planeMenu
        MenuItem { objectName: "planeTop"; text: "Top (XY) \u2014 ground"; onTriggered: window.app.startSketch("top") }
        MenuItem { objectName: "planeFront"; text: "Front (XZ)"; onTriggered: window.app.startSketch("front") }
        MenuItem { objectName: "planeRight"; text: "Right (YZ)"; onTriggered: window.app.startSketch("right") }
    }

    Menu {
        id: fileMenu
        MenuItem { text: "New"; onTriggered: window.confirmDiscard(() => window.app.newDocument()) }
        MenuItem { text: "Open…"; onTriggered: window.confirmDiscard(() => openDialog.open()) }
        MenuSeparator {}
        MenuItem { text: "Save"; onTriggered: window.save() }
        MenuItem { text: "Save As…"; onTriggered: saveDialog.open() }
        MenuSeparator {}
        MenuItem { text: "Export STEP…"; enabled: window.app.bodyCount > 0; onTriggered: stepDialog.open() }
        MenuItem { text: "Export STL…"; enabled: window.app.bodyCount > 0; onTriggered: stlDialog.open() }
        MenuItem { text: "Export 3MF…"; enabled: window.app.bodyCount > 0; onTriggered: threeMfDialog.open() }
    }

    // ---------------------------------------------------------------- create palette
    Panel {
        id: createPanel
        visible: !window.app.sketchMode
        anchors { left: parent.left; verticalCenter: parent.verticalCenter; margins: Theme.margin }
        width: createColumn.implicitWidth + 2 * Theme.panelPadding
        height: createColumn.implicitHeight + 2 * Theme.panelPadding

        ColumnLayout {
            id: createColumn
            anchors.centerIn: parent
            spacing: 4
            SectionLabel { text: "Create" }
            ActionButton {
                text: "Box"
                Layout.fillWidth: true
                onClicked: window.app.createBox(20)
                ToolTip.visible: hovered
                ToolTip.text: "Add a 20 mm cube (B). Push and pull its faces to shape it."
                ToolTip.delay: 500
            }
            ActionButton {
                id: sketchButton
                objectName: "sketchButton"
                text: "Sketch"
                Layout.fillWidth: true
                enabled: window.app.canStartSketch
                // On a selected face: sketch right there. Otherwise pick an origin plane.
                onClicked: window.app.faceSelected() ? window.app.startSketch() : planeMenu.popup(this, width + 6, 0)
                ToolTip.visible: hovered && !planeMenu.visible
                ToolTip.text: "Sketch on the selected flat face, or on an origin plane (K = ground)."
                ToolTip.delay: 500
            }

            // Tools that act on a selection: with a fitting selection they run,
            // otherwise they say what to select (no hidden gestures to learn).
            SectionLabel { text: "Modify"; Layout.topMargin: 6 }
            Repeater {
                model: [
                    { id: "pushpull", label: "Push/Pull", tip: "Move a flat face: select it, drag the arrow or type a distance." },
                    { id: "fillet", label: "Fillet", tip: "Round edges: select them, drag or type the radius." },
                    { id: "chamfer", label: "Chamfer", tip: "Bevel edges: select them, drag or type the size." },
                    { id: "shell", label: "Shell", tip: "Hollow a body through the selected face(s)." },
                    { id: "move", label: "Move", tip: "Move a body along X, Y or Z." },
                    { id: "rotate", label: "Rotate", tip: "Turn a body about X, Y or Z: drag a ring (15° steps, Alt for 1°) or type an angle." },
                    { id: "align", label: "Align", tip: "Put a face or edge of one body against a face or edge of another (or lay a face on the ground)." }
                ]
                delegate: ActionButton {
                    required property var modelData
                    objectName: "tool_" + modelData.id
                    text: modelData.label
                    Layout.fillWidth: true
                    onClicked: { window.app.runTool(modelData.id); viewport.forceActiveFocus() }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.tip
                    ToolTip.delay: 500
                }
            }
            SectionLabel { text: "Combine"; Layout.topMargin: 6 }
            Repeater {
                model: [
                    { id: "union", label: "Union", tip: "Join two or more bodies into one." },
                    { id: "subtract", label: "Subtract", tip: "Cut the other bodies away from the first one selected." },
                    { id: "intersect", label: "Intersect", tip: "Keep only the volume the bodies share." }
                ]
                delegate: ActionButton {
                    required property var modelData
                    objectName: "tool_" + modelData.id
                    text: modelData.label
                    Layout.fillWidth: true
                    onClicked: { window.app.runTool(modelData.id); viewport.forceActiveFocus() }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.tip + " Select bodies by double-clicking (Shift adds), or in the Model panel."
                    ToolTip.delay: 500
                }
            }
        }
    }

    // ---------------------------------------------------------------- model history
    HistoryPanel {
        id: historyPanel
        app: window.app
        anchors { right: parent.right; top: parent.top; margins: Theme.margin }
        visible: window.app.history.length > 0
        maximumHeight: window.height - 2 * Theme.margin - 80
        onFinished: viewport.forceActiveFocus()
    }

    // ---------------------------------------------------------------- view controls
    Panel {
        anchors { right: parent.right; bottom: parent.bottom; margins: Theme.margin }
        width: viewRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding

        RowLayout {
            id: viewRow
            anchors.centerIn: parent
            spacing: 2
            ActionButton { text: "Fit"; onClicked: window.app.fitAll() }
            Separator {}
            ActionButton { text: "Iso"; onClicked: window.app.setView("iso") }
            ActionButton { text: "Top"; onClicked: window.app.setView("top") }
            ActionButton { text: "Front"; onClicked: window.app.setView("front") }
            ActionButton { text: "Right"; onClicked: window.app.setView("right") }
            Separator {}
            ActionButton {
                text: window.app.perspective ? "Perspective" : "Orthographic"
                onClicked: window.app.togglePerspective()
            }
            Separator {}
            ActionButton {
                text: window.app.displayUnit
                onClicked: window.app.setDisplayUnit(window.app.displayUnit === "mm" ? "in" : "mm")
                ToolTip.visible: hovered
                ToolTip.text: "Display unit. You can always type any unit, e.g. 1in or 25mm."
                ToolTip.delay: 500
            }
        }
    }

    // ---------------------------------------------------------------- status / hints
    Column {
        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.margin }
        spacing: 6

        // What can be done with the selection when there is no manipulator
        // (e.g. two bodies: Union / Subtract / Intersect). With a manipulator,
        // the same actions sit in the value chip instead.
        Panel {
            objectName: "selectionActions"
            visible: !window.app.sketchMode && window.app.contextActions.length > 0
                     && (!window.app.operationActive || !window.app.valueLabelVisible)
            width: selectionActionRow.implicitWidth + 2 * Theme.panelPadding
            height: Theme.controlHeight + 2 * Theme.panelPadding
            Row {
                id: selectionActionRow
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: window.app.contextActions
                    delegate: ActionButton {
                        required property var modelData
                        objectName: "barAction_" + modelData.id
                        text: modelData.label
                        checked: modelData.active
                        compact: true
                        onClicked: {
                            // Triggering rebuilds this list: capture first.
                            const app = window.app
                            const id = modelData.id
                            app.triggerAction(id)
                            viewport.forceActiveFocus()
                        }
                    }
                }
            }
        }

        Panel {
            visible: window.app.selectionSummary.length > 0
            width: summaryText.implicitWidth + 28
            height: 34
            Text {
                id: summaryText
                anchors.centerIn: parent
                text: window.app.selectionSummary
                color: Theme.text
                font.pixelSize: 13
            }
        }
        Text {
            text: window.hintText()
            color: Theme.mutedText
            font.pixelSize: 12
            leftPadding: 4
        }
    }

    function hintText() {
        if (app.sketchMode)
            return app.sketchHint
        if (app.operationPrompt.length > 0)
            return app.operationPrompt
        if (app.operationActive && app.operationTitle === "Align")
            return "Drag the arrow or type an offset · Flip turns it around · click another face to re-aim · Enter applies"
        if (app.operationActive && app.operationTitle === "Extrude" && !app.operationHasValue)
            return "Drag the arrow or type a distance \u00b7 Shift-click to add more profiles"
        if (app.bodyCount === 0 && app.sketchCount > 0)
            return "Click inside a closed sketch shape to extrude it \u00b7 double-click it to edit the sketch"
        if (app.bodyCount === 0)
            return "Add a box, or start a sketch."
        if (app.operationActive && app.operationHasValue)
            return "Enter to apply · Esc to cancel · click elsewhere to apply and continue"
        if (app.operationActive && app.operationTitle === "Move")
            return "Drag an arrow or type a distance · Shift+double-click another body to combine them"
        if (app.operationActive && app.operationTitle === "Rotate")
            return "Drag a ring (15° steps, Alt for 1°) or type an angle · Enter applies"
        if (app.operationActive && (app.operationTitle === "Fillet" || app.operationTitle === "Chamfer"))
            return "Drag the arrow, or just type a value · Shift-click to add more edges"
        if (app.operationActive && app.operationTitle === "Shell")
            return "Type the wall thickness · Shift-click to open more faces"
        if (app.operationActive)
            return "Drag the arrow, or just type a value"
        if (app.hasSelection && app.contextActions.length > 0)
            return "Choose an action above · Esc clears the selection"
        return "Click a face to push/pull, an edge to round it · double-click selects the body · "
             + "drag to orbit · Shift/middle-drag to pan · scroll to zoom"
    }

    // ---------------------------------------------------------------- empty state
    Column {
        anchors.centerIn: parent
        spacing: 14
        visible: window.app.bodyCount === 0 && window.app.sketchCount === 0 && !window.app.sketchMode
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Start with a shape"
            font.pixelSize: 22
            font.weight: Font.Medium
            color: Theme.text
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Pull faces, round edges, or sketch a profile and extrude it."
            font.pixelSize: 14
            color: Theme.mutedText
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            ActionButton {
                text: "Add a box"
                accent: true
                onClicked: window.app.createBox(20)
            }
            ActionButton {
                text: "Start a sketch"
                onClicked: window.app.startSketch()
            }
        }
    }

    // ---------------------------------------------------------------- sketch mode
    SketchOverlay {
        id: sketchOverlay
        anchors.fill: parent
        app: window.app
        visible: window.app.sketchMode
        onFinished: viewport.forceActiveFocus()
    }

    // ---------------------------------------------------------------- operation chip
    ValueChip {
        id: valueChip
        app: window.app
        visible: window.app.operationActive && window.app.valueLabelVisible
        // Beside the arrow tip (right side, or left if there is no room),
        // so the manipulator itself is never covered.
        readonly property real tipX: window.app.valueLabelPosition.x
        readonly property real tipY: window.app.valueLabelPosition.y
        readonly property bool fitsRight: tipX + 28 + width < window.width - Theme.margin
        x: Math.max(Theme.margin, fitsRight ? tipX + 28 : tipX - 28 - width)
        y: Math.max(topBar.y + topBar.height + 8, Math.min(window.height - height - 70, tipY - 24))
        onFinished: viewport.forceActiveFocus()
    }

    // ---------------------------------------------------------------- help
    HelpOverlay {
        id: helpOverlay
        objectName: "helpOverlay"
        anchors.fill: parent
        z: 100
        onVisibleChanged: if (!visible) viewport.forceActiveFocus()
    }

    // ---------------------------------------------------------------- toast
    Rectangle {
        id: toast
        property alias text: toastText.text
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 72 }
        width: toastText.implicitWidth + 32
        height: 38
        radius: 19
        color: Theme.toast
        opacity: 0
        visible: opacity > 0
        Text {
            id: toastText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 13
        }
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Timer { id: toastTimer; interval: 3200; onTriggered: toast.opacity = 0 }
        function show(message) {
            text = message
            opacity = 0.94
            toastTimer.restart()
        }
    }

    Connections {
        target: window.app
        function onMessage(text) { toast.show(text) }
    }

    // ---------------------------------------------------------------- dialogs
    FileDialog {
        id: openDialog
        title: "Open project"
        currentFolder: window.app.projectFolder
        nameFilters: ["OpenShape projects (*.openshape)"]
        onAccepted: window.app.openProject(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: "Save project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "openshape"
        currentFolder: window.app.projectFolder
        nameFilters: ["OpenShape projects (*.openshape)"]
        onAccepted: {
            if (window.app.saveProjectAs(selectedFile) && window.afterSave) {
                const action = window.afterSave
                window.afterSave = null
                action()
            }
        }
        onRejected: window.afterSave = null
    }
    FileDialog {
        id: stepDialog
        title: "Export STEP"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "step"
        nameFilters: ["STEP files (*.step *.stp)"]
        onAccepted: window.app.exportStep(selectedFile)
    }
    FileDialog {
        id: stlDialog
        title: "Export STL"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "stl"
        nameFilters: ["STL files (*.stl)"]
        onAccepted: window.app.exportStl(selectedFile)
    }
    FileDialog {
        id: threeMfDialog
        title: "Export 3MF"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "3mf"
        nameFilters: ["3MF files (*.3mf)"]
        onAccepted: window.app.export3mf(selectedFile)
    }
    MessageDialog {
        id: unsavedDialog
        property var pendingAction: null
        title: "Unsaved changes"
        text: "Save changes to “" + window.app.documentTitle + "”?"
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            const action = pendingAction
            pendingAction = null
            if (button === MessageDialog.Discard) {
                action()
            } else if (button === MessageDialog.Save) {
                if (window.app.hasProjectPath()) {
                    if (window.app.saveProject())
                        action()
                } else {
                    window.afterSave = action
                    saveDialog.open()
                }
            }
        }
    }
}
