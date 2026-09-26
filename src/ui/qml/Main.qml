// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
    // Shown by main.cpp once it has put the window where it was last time.
    visible: false
    title: (app.dirty ? "• " : "") + app.documentTitle + " — OpenShape"
    color: Theme.background

    property bool closeConfirmed: false
    property var afterSave: null   // action to run once a Save As completes
    // A question the user must answer first: the window's shortcuts wait
    // (Ctrl+N behind "Save changes?" would replace what it is asking about).
    readonly property bool modalOpen: unsavedDialog.visible || recoveryOverlay.visible

    // ---------------------------------------------------------------- viewport
    Viewport {
        id: viewport
        objectName: "viewport"
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
            if (window.app.operationActive && window.app.valueLabelVisible && event.text.length === 1
                    && "0123456789.-+(".indexOf(event.text) >= 0
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
    Shortcut { sequences: [StandardKey.Undo]; enabled: !window.modalOpen; onActivated: window.app.undo() }
    // StandardKey.Redo is Ctrl+Y on Windows but Ctrl+Shift+Z elsewhere; accept
    // both everywhere. On Windows the duplicate makes Qt report the match as
    // ambiguous, so handle that signal too.
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Y"]
        enabled: !window.modalOpen
        onActivated: window.app.redo()
        onActivatedAmbiguously: window.app.redo()
    }
    Shortcut { sequences: [StandardKey.Save]; enabled: !window.modalOpen; onActivated: window.save() }
    Shortcut { sequences: [StandardKey.SaveAs]; enabled: !window.modalOpen; onActivated: saveDialog.open() }
    Shortcut {
        sequences: [StandardKey.Open]
        enabled: !window.modalOpen
        onActivated: window.confirmDiscard(() => openDialog.open())
    }
    Shortcut {
        sequences: [StandardKey.New]
        enabled: !window.modalOpen
        onActivated: window.confirmDiscard(() => window.app.newDocument())
    }
    Shortcut { sequence: "Ctrl+,"; enabled: !window.modalOpen; onActivated: preferencesOverlay.open() }
    Shortcut { sequence: "F"; enabled: viewport.activeFocus; onActivated: window.app.fitAll() }
    Shortcut { sequence: "F1"; enabled: !window.modalOpen; onActivated: helpOverlay.toggle() }
    Shortcut { sequence: "B"; enabled: viewport.activeFocus && !window.app.sketchMode; onActivated: window.app.createBox(20) }
    // Duplicate the selected body (the copy is selected, ready to drag away).
    Shortcut { sequence: "Ctrl+D"; enabled: !window.app.sketchMode; onActivated: window.app.triggerAction("duplicate") }
    Shortcut {
        sequence: "K"
        enabled: viewport.activeFocus && window.app.canStartSketch
        onActivated: window.app.startSketch()
    }

    // After a menu closes, keys go back to the view (B, K, F, typed values;
    // Qt leaves them nowhere after a sub-menu), unless the menu opened a
    // panel that takes them.
    function focusViewUnlessPanel() {
        const panels = [unsavedDialog, recoveryOverlay, preferencesOverlay, aboutOverlay, helpOverlay]
        for (const panel of panels) {
            if (panel.visible) {
                panel.forceActiveFocus()
                return
            }
        }
        viewport.forceActiveFocus()
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
        unsavedDialog.ask(action)
    }

    onClosing: (close) => {
        if (app.dirty && !closeConfirmed) {
            close.accepted = false
            // Don't Save: the user lets go of the work, so no recovery copy
            // of it is kept (any other end of the run keeps one).
            confirmDiscard(() => { window.app.discardUnsavedWork(); window.closeConfirmed = true; window.close() })
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
            ActionButton { objectName: "fileMenuButton"; text: "File"; onClicked: fileMenu.popup(this, 0, height + 6) }
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
        objectName: "fileMenu"
        onAboutToShow: window.app.refreshRecentFiles() // a file deleted meanwhile drops out
        onClosed: window.focusViewUnlessPanel()
        // Sub-menu entries are made by this delegate: name them for the acceptance run.
        delegate: MenuItem { objectName: subMenu ? subMenu.objectName + "Item" : ""; enabled: !subMenu || subMenu.enabled }
        MenuItem { objectName: "newMenuItem"; text: "New"; onTriggered: window.confirmDiscard(() => window.app.newDocument()) }
        MenuItem { text: "Open…"; onTriggered: window.confirmDiscard(() => openDialog.open()) }
        Menu {
            id: recentMenu
            objectName: "openRecentMenu"
            // Only when the File menu closes too: Esc on this sub-menu goes
            // back to the File menu, whose own Esc must still reach it.
            onClosed: if (!fileMenu.opened) window.focusViewUnlessPanel()
            title: "Open Recent"
            enabled: window.app.recentFiles.length > 0
            Instantiator {
                model: window.app.recentFiles
                delegate: MenuItem {
                    required property var modelData
                    required property int index
                    objectName: "recentFile_" + index
                    text: modelData.name + "  —  " + modelData.folder
                    onTriggered: {
                        // After the menu has closed: opening rebuilds this list,
                        // and a menu whose item vanishes mid-click stays open.
                        const path = modelData.path
                        Qt.callLater(() => window.confirmDiscard(() => window.app.openRecent(path)))
                    }
                }
                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }
            MenuSeparator {}
            MenuItem { objectName: "clearRecentFiles"; text: "Clear Recent"; onTriggered: Qt.callLater(window.app.clearRecentFiles) }
        }
        MenuSeparator {}
        MenuItem { text: "Save"; onTriggered: window.save() }
        MenuItem { text: "Save As…"; onTriggered: saveDialog.open() }
        MenuSeparator {}
        MenuItem { text: "Export STEP…"; enabled: window.app.bodyCount > 0; onTriggered: stepDialog.open() }
        MenuItem { text: "Export STL…"; enabled: window.app.bodyCount > 0; onTriggered: stlDialog.open() }
        MenuItem { text: "Export 3MF…"; enabled: window.app.bodyCount > 0; onTriggered: threeMfDialog.open() }
        MenuSeparator {}
        MenuItem { objectName: "preferencesMenuItem"; text: "Preferences…"; onTriggered: preferencesOverlay.open() }
        MenuItem { objectName: "aboutMenuItem"; text: "About OpenShape"; onTriggered: aboutOverlay.open() }
    }

    // ---------------------------------------------------------------- create palette
    Panel {
        id: createPanel
        visible: !window.app.sketchMode
        // Centered, between the top bar and whatever is at the bottom left
        // (hints, the selection's actions); scrolls when the window is too
        // short for every tool (touch-sized buttons on a tablet).
        readonly property real minY: topBar.y + topBar.height + Theme.margin
        readonly property real maxBottom: statusColumn.y - Theme.margin
        anchors { left: parent.left; leftMargin: Theme.margin }
        y: Math.max(minY, Math.min((window.height - height) / 2, maxBottom - height))
        width: createColumn.implicitWidth + 2 * Theme.panelPadding
        height: Math.min(createColumn.implicitHeight + 2 * Theme.panelPadding, maxBottom - minY)

        Flickable {
            id: createScroll
            anchors { fill: parent; margins: Theme.panelPadding }
            contentWidth: width
            contentHeight: createColumn.implicitHeight
            clip: true
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: createColumn
            width: createScroll.width
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
                    { id: "offset", label: "Offset", tip: "Move a face with its neighbours following; a hole or shaft takes its new diameter (e.g. print tolerance)." },
                    { id: "move", label: "Move", tip: "Move a body along X, Y or Z." },
                    { id: "rotate", label: "Rotate", tip: "Turn a body about X, Y or Z, or about an edge you click: drag a ring (15° steps, Alt for 1°) or type an angle." },
                    { id: "mirror", label: "Mirror", tip: "Add a body's mirror image across a flat face or an origin plane." },
                    { id: "pattern", label: "Pattern", tip: "Repeat a body in a row or around an axis (holes and shafts work as axes)." },
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
        // More tools below: a fade at the bottom edge says "scroll".
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 28
            radius: 12
            visible: createScroll.contentY + createScroll.height < createScroll.contentHeight - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00F9FAFB" }
                GradientStop { position: 1.0; color: Theme.panel }
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

    // Touch-sized controls once the app is used by touch (from the start on a tablet).
    Binding { target: Theme; property: "touch"; value: window.app.touchMode }

    // ---------------------------------------------------------------- view controls
    AxisTriad {
        id: axisTriad
        objectName: "axisTriad"
        app: window.app
        anchors { right: viewPanel.right; bottom: viewPanel.top; bottomMargin: 8 }
    }

    Panel {
        id: viewPanel
        anchors { right: parent.right; bottom: parent.bottom; margins: Theme.margin }
        width: viewRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding

        RowLayout {
            id: viewRow
            anchors.centerIn: parent
            spacing: 2
            ActionButton { objectName: "viewFit"; text: "Fit"; onClicked: window.app.fitAll() }
            Separator {}
            ActionButton { objectName: "viewIso"; text: "Iso"; onClicked: window.app.setView("iso") }
            ActionButton { objectName: "viewTop"; text: "Top"; onClicked: window.app.setView("top") }
            ActionButton { objectName: "viewFront"; text: "Front"; onClicked: window.app.setView("front") }
            ActionButton { objectName: "viewRight"; text: "Right"; onClicked: window.app.setView("right") }
            Separator {}
            ActionButton {
                objectName: "viewProjection"
                text: window.app.perspective ? "Perspective" : "Orthographic"
                onClicked: window.app.togglePerspective()
            }
            Separator {}
            ActionButton {
                id: penButton
                objectName: "penModeButton"
                visible: window.app.touchMode || window.app.penMode
                text: "Pen"
                checked: window.app.penMode
                onClicked: window.app.penMode = !window.app.penMode
                ToolTip.visible: hovered
                ToolTip.text: "Pen mode: the pen selects and draws, fingers only move the view (a resting hand does nothing)."
                ToolTip.delay: 500
            }
            Separator { visible: penButton.visible }
            ActionButton {
                objectName: "viewUnit"
                text: window.app.displayUnit
                onClicked: window.app.setDisplayUnit(window.app.displayUnit === "mm" ? "in" : "mm")
                ToolTip.visible: hovered
                ToolTip.text: "Display unit. You can always type any unit, e.g. 1in or 25mm."
                ToolTip.delay: 500
            }
        }
    }

    // ---------------------------------------------------------------- status / hints
    // Bottom left, beside the view buttons; above them when the window is
    // too narrow for both (e.g. an iPad in portrait).
    Column {
        id: statusColumn
        readonly property bool stacked: viewPanel.x < window.width / 2
            || (selectionBar.visible && Theme.margin + selectionBar.width + Theme.margin > viewPanel.x)
        anchors { left: parent.left; bottom: stacked ? viewPanel.top : parent.bottom; margins: Theme.margin }
        spacing: 6

        // What is selected and what can be done with it when there is no
        // manipulator (e.g. two bodies: Union / Subtract / Intersect). With a
        // manipulator, the same actions sit in the value chip instead.
        Panel {
            id: selectionBar
            objectName: "selectionActions"
            visible: !window.app.sketchMode && window.app.contextActions.length > 0
                     && (!window.app.operationActive || !window.app.valueLabelVisible)
            width: selectionActionRow.implicitWidth + 2 * Theme.panelPadding
            height: Theme.controlHeight + 2 * Theme.panelPadding
            RowLayout {
                id: selectionActionRow
                anchors.centerIn: parent
                spacing: 4
                Text {
                    visible: window.app.selectionSummary.length > 0
                    text: window.app.selectionSummary
                    color: Theme.text
                    font.pixelSize: 13
                    leftPadding: 8
                    rightPadding: 4
                }
                Separator { visible: window.app.selectionSummary.length > 0 }
                Repeater {
                    model: window.app.contextActions
                    delegate: ActionButton {
                        required property var modelData
                        objectName: "barAction_" + modelData.id
                        text: modelData.label
                        checked: modelData.active
                        compact: true
                        onClicked: {
                            // Triggering rebuilds this list and destroys this
                            // button (and its context): capture first.
                            const app = window.app
                            const id = modelData.id
                            const view = viewport
                            app.triggerAction(id)
                            view.forceActiveFocus()
                        }
                    }
                }
            }
        }

        // The selection alone, while a manipulator's value chip has the actions.
        Panel {
            visible: window.app.selectionSummary.length > 0 && !selectionBar.visible
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
            // Wraps instead of running under the view buttons or the axis marker.
            width: (statusColumn.stacked ? axisTriad.x : viewPanel.x) - 2 * Theme.margin
            text: window.hintText()
            color: Theme.mutedText
            font.pixelSize: 12
            leftPadding: 4
            wrapMode: Text.WordWrap
        }
    }

    function hintText() {
        if (app.sketchMode)
            return app.sketchHint
        if (app.operationPrompt.length > 0)
            return app.operationPrompt
        if (app.operationActive && app.operationTitle === "Align")
            return "Drag the arrow or type an offset · Flip turns it around · click another face to re-aim · Enter applies"
        if (app.operationActive && app.operationTitle === "Extrude" && app.operationValueLabel === "Thickness")
            return "Drag the arrow or type the total thickness (half on each side of the sketch) \u00b7 Enter applies"
        if (app.operationActive && app.operationTitle === "Extrude" && !app.operationHasValue)
            return "Drag the arrow or type a distance \u00b7 \"Up to face\" ends it on a face you click \u00b7 Shift-click adds profiles"
        if (app.bodyCount === 0 && app.sketchCount > 0)
            return "Click inside a closed sketch shape to extrude it \u00b7 double-click it to edit the sketch"
        if (app.bodyCount === 0)
            return "Add a box, or start a sketch."
        if (app.operationActive && app.operationTitle.startsWith("Counterbore"))
            return "Pick the screw size, or type the diameter · click the arrow into the hole to type the depth · Enter applies"
        if (app.operationActive && app.operationTitle.startsWith("Countersink"))
            return "Pick the screw size (90° heads), or drag the arrow / type the diameter at the surface · Enter applies"
        if (app.operationActive && app.operationTitle.startsWith("Heat-set insert"))
            return "Pick the insert size · drag the arrow or type the pilot hole's depth · Enter applies"
        if (app.operationActive && app.operationHasValue)
            return "Enter to apply · Esc to cancel · click elsewhere to apply and continue"
        if (app.operationActive && app.operationTitle === "Move" && app.contextActions.some(a => a.id === "split"))
            return "This body is in separate pieces: Split into bodies makes each piece a body · drag an arrow to move it"
        if (app.operationActive && app.operationTitle === "Move")
            return "Drag an arrow or type a distance · Duplicate (Ctrl+D) makes a copy to drag away · "
                 + "Shift+double-click another body to combine them"
        if (app.operationActive && app.operationTitle === "Rotate")
            return "Drag a ring (15° steps, Alt for 1°) or type an angle · click an edge or hole to turn about it, "
                 + "a corner or circle to move the pivot · Enter applies"
        if (app.operationActive && app.operationTitle === "Push/Pull" && app.operationValueLabel !== "Distance")
            return "Drag the arrow or type the new " + app.operationValueLabel.toLowerCase()
                 + " · +5 or -5 changes it by that much · Enter applies"
        if (app.operationActive && app.operationTitle === "Offset")
            return "Drag the arrow or type the new value · Enter applies · Delete removes the face instead"
        if (app.operationActive && app.operationTitle === "Mirror")
            return "Enter or Apply mirrors it · click another flat face or choose a plane to change it · "
                 + "Separate bodies keeps the image as its own body"
        if (app.operationActive && app.operationTitle === "Pattern")
            return "Drag the arrow or type the spacing (angle when circular) · click an edge or hole to set the direction · "
                 + "Separate bodies makes each copy a body · Enter applies"
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

    AboutOverlay {
        id: aboutOverlay
        objectName: "aboutOverlay"
        anchors.fill: parent
        z: 100
        onVisibleChanged: if (!visible) viewport.forceActiveFocus()
    }

    PreferencesOverlay {
        id: preferencesOverlay
        objectName: "preferencesOverlay"
        app: window.app
        anchors.fill: parent
        z: 100
        onVisibleChanged: if (!visible) window.focusViewUnlessPanel()
    }

    // "Save changes?" before New, Open, Open Recent, Restore or closing.
    UnsavedOverlay {
        id: unsavedDialog
        objectName: "unsavedDialog"
        app: window.app
        anchors.fill: parent
        z: 120 // above the restore prompt, whose Restore asks it
        onSaveRequested: (action) => {
            if (window.app.hasProjectPath()) {
                if (window.app.saveProject())
                    action()
            } else {
                window.afterSave = action
                saveDialog.open()
            }
        }
        onVisibleChanged: if (!visible) window.focusViewUnlessPanel()
    }

    // After a crash: restore or discard the work that was not saved.
    RecoveryOverlay {
        id: recoveryOverlay
        objectName: "recoveryOverlay"
        app: window.app
        anchors.fill: parent
        z: 110
        onRestoreRequested: (session) => window.confirmDiscard(() => window.app.restoreRecovery(session))
        onVisibleChanged: if (!visible) window.focusViewUnlessPanel()
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
}
