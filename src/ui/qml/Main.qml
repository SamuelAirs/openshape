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
        }
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
                objectName: "sketchButton"
                text: "Sketch"
                Layout.fillWidth: true
                enabled: window.app.canStartSketch
                onClicked: window.app.startSketch()
                ToolTip.visible: hovered
                ToolTip.text: "Sketch on the ground plane, or on the selected flat face (K)."
                ToolTip.delay: 500
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
        if (app.operationActive && app.operationTitle === "Extrude" && !app.operationHasValue)
            return "Drag the arrow or type a distance \u00b7 Shift-click to add more profiles"
        if (app.bodyCount === 0 && app.sketchCount > 0)
            return "Click inside a closed sketch shape to extrude it \u00b7 double-click it to edit the sketch"
        if (app.bodyCount === 0)
            return "Add a box, or start a sketch."
        if (app.operationActive && app.operationHasValue)
            return "Enter to apply · Esc to cancel · click elsewhere to apply and continue"
        if (app.operationActive)
            return "Drag the arrow, or just type a value · Shift-click to add more edges"
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
