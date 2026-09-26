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
    // --safe-area (main.cpp): [top, right, bottom, left] in logical px, to
    // try a phone's layout (Dynamic Island, home indicator) on the desktop.
    property var simulatedSafeArea: null
    readonly property bool simulatingSafeArea: simulatedSafeArea !== null && simulatedSafeArea !== undefined
                                               && simulatedSafeArea.length === 4
    // Phones and tablets size the window themselves (a phone is narrower
    // than the desktop minimum; Split View makes it narrower still).
    readonly property bool mobile: Qt.platform.os === "ios" || Qt.platform.os === "android"

    width: 1400
    height: 900
    minimumWidth: mobile ? 0 : 720
    minimumHeight: mobile ? 0 : 480
    // The 3D view fills the whole window, under the status bar and the home
    // indicator too; the controls keep out of the safe areas themselves
    // (Theme.inset*). ApplicationWindow's padding would otherwise follow the
    // safe areas (Qt 6.9+) and inset everything.
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0
    // Shown by main.cpp once it has put the window where it was last time.
    visible: false
    title: (app.dirty ? "• " : "") + app.documentTitle + " — OpenShape"
    color: Theme.background

    property bool closeConfirmed: false
    property var afterSave: null   // action to run once a Save As completes
    // A question the user must answer first: the window's shortcuts wait
    // (Ctrl+N behind "Save changes?" would replace what it is asking about).
    readonly property bool modalOpen: unsavedDialog.visible || recoveryOverlay.visible || saveNamePrompt.visible

    // ---------------------------------------------------------------- layout
    // The window's size and safe areas drive the layout (Theme.compact,
    // Theme.inset*); both change live (rotation, Split View, a phone that
    // folds or unfolds).
    Binding { target: Theme; property: "windowWidth"; value: window.width }
    Binding { target: Theme; property: "windowHeight"; value: window.height }
    Item {
        // The window's safe-area margins (SafeArea, Qt 6.9+; zero on the desktop).
        id: safeInsets
        anchors.fill: parent
        readonly property real marginTop: window.simulatingSafeArea ? window.simulatedSafeArea[0] : SafeArea.margins.top
        readonly property real marginRight: window.simulatingSafeArea ? window.simulatedSafeArea[1] : SafeArea.margins.right
        readonly property real marginBottom: window.simulatingSafeArea ? window.simulatedSafeArea[2] : SafeArea.margins.bottom
        readonly property real marginLeft: window.simulatingSafeArea ? window.simulatedSafeArea[3] : SafeArea.margins.left
    }
    Binding { target: Theme; property: "safeTop"; value: safeInsets.marginTop }
    Binding { target: Theme; property: "safeRight"; value: safeInsets.marginRight }
    Binding { target: Theme; property: "safeBottom"; value: safeInsets.marginBottom }
    Binding { target: Theme; property: "safeLeft"; value: safeInsets.marginLeft }
    // Touch-sized controls once the app is used by touch (from the start on a tablet).
    Binding { target: Theme; property: "touch"; value: window.app.touchMode }

    // Compact layout: the Model panel slides in behind the Model button, the
    // view buttons open from the View button (one at a time).
    property bool historyOpen: false
    property bool viewMenuOpen: false
    onHistoryOpenChanged: if (historyOpen) viewMenuOpen = false
    onViewMenuOpenChanged: {
        if (viewMenuOpen)
            historyOpen = false
        else
            viewport.forceActiveFocus()
    }
    // Compact layout: the top of the tool strip along the bottom edge, and
    // where the things along the bottom edge start (the tool strip; the
    // sketch's tools and actions): the hint, the selection's actions, the
    // value chip and messages stay above it. (From the window's size, not
    // from the strip's position: the regular palette's position depends on
    // the hint's, which would make a loop.)
    readonly property real stripTop: height - Theme.insetBottom - Theme.controlHeight - 2 * Theme.panelPadding
    readonly property real bottomStackTop: app.sketchMode ? sketchOverlay.bottomStackTop : stripTop

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
    Shortcut { sequences: [StandardKey.SaveAs]; enabled: !window.modalOpen; onActivated: window.saveAs() }
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
        const panels = [saveNamePrompt, unsavedDialog, recoveryOverlay, preferencesOverlay, aboutOverlay, helpOverlay]
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
            saveAs()
    }

    // The desktop asks where with a file dialog; an iPhone or iPad only for
    // a name (the project goes into OpenShape's folder: iOS has no save dialog).
    function saveAs() {
        if (app.savesToAppFolder)
            saveNamePrompt.open(app.hasProjectPath() ? app.documentTitle : "")
        else
            saveDialog.open()
    }

    // Exports: a file dialog on the desktop; OpenShape's Exports folder on an
    // iPhone or iPad.
    function exportAs(format, dialog) {
        if (app.savesToAppFolder)
            app.exportToAppFolder(format)
        else
            dialog.open()
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
        objectName: "topBar"
        x: Theme.insetLeft
        y: Theme.insetTop
        height: Theme.controlHeight + 2 * Theme.panelPadding
        width: topRow.implicitWidth + 2 * Theme.panelPadding

        RowLayout {
            id: topRow
            anchors.centerIn: parent
            spacing: 4

            Text {
                visible: !Theme.compact // a phone needs the room
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
                objectName: "undoButton"
                text: "Undo"
                enabled: window.app.canUndo
                // Touch has no tooltip: a message says what was undone.
                onClicked: Theme.touch ? window.app.undoWithFeedback() : window.app.undo()
                ToolTip.visible: hovered && window.app.canUndo && !Theme.touch
                ToolTip.text: "Undo " + window.app.undoText + "  (Ctrl+Z)"
                ToolTip.delay: 500
            }
            ActionButton {
                objectName: "redoButton"
                text: "Redo"
                enabled: window.app.canRedo
                onClicked: Theme.touch ? window.app.redoWithFeedback() : window.app.redo()
                ToolTip.visible: hovered && window.app.canRedo && !Theme.touch
                ToolTip.text: "Redo " + window.app.redoText + "  (Ctrl+Y)"
                ToolTip.delay: 500
            }
            Separator {}
            ActionButton {
                objectName: "helpButton"
                text: "?"
                implicitWidth: Theme.controlHeight
                onClicked: helpOverlay.toggle()
                ToolTip.visible: hovered && !Theme.touch
                ToolTip.text: "How OpenShape works (F1)"
                ToolTip.delay: 500
            }
        }
    }

    Menu {
        id: planeMenu
        MenuItem { objectName: "planeTop"; text: "Top (XY) — ground"; onTriggered: window.app.startSketch("top") }
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
        MenuItem { objectName: "openMenuItem"; text: "Open…"; onTriggered: window.confirmDiscard(() => openDialog.open()) }
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
        MenuItem { objectName: "saveMenuItem"; text: "Save"; onTriggered: window.save() }
        MenuItem { objectName: "saveAsMenuItem"; text: "Save As…"; onTriggered: window.saveAs() }
        MenuSeparator {}
        // (No "…" on an iPhone or iPad: the file goes straight into Exports.)
        MenuItem {
            objectName: "exportStepMenuItem"
            text: window.app.savesToAppFolder ? "Export STEP" : "Export STEP…"
            enabled: window.app.bodyCount > 0
            onTriggered: window.exportAs("step", stepDialog)
        }
        MenuItem {
            objectName: "exportStlMenuItem"
            text: window.app.savesToAppFolder ? "Export STL" : "Export STL…"
            enabled: window.app.bodyCount > 0
            onTriggered: window.exportAs("stl", stlDialog)
        }
        MenuItem {
            objectName: "export3mfMenuItem"
            text: window.app.savesToAppFolder ? "Export 3MF" : "Export 3MF…"
            enabled: window.app.bodyCount > 0
            onTriggered: window.exportAs("3mf", threeMfDialog)
        }
        MenuSeparator {}
        MenuItem { objectName: "preferencesMenuItem"; text: "Preferences…"; onTriggered: preferencesOverlay.open() }
        MenuItem { objectName: "aboutMenuItem"; text: "About OpenShape"; onTriggered: aboutOverlay.open() }
    }

    // ---------------------------------------------------------------- create palette
    // Regular windows: a column on the left, centered between the top bar and
    // whatever is at the bottom left (hints, the selection's actions); it
    // scrolls when the window is too short for every tool (touch-sized
    // buttons on a tablet). Compact windows (a phone): a strip along the
    // bottom edge that scrolls sideways. The same buttons either way.
    Panel {
        id: createPanel
        objectName: "createPanel"
        visible: !window.app.sketchMode
        readonly property real minY: topBar.y + topBar.height + Theme.margin
        readonly property real maxBottom: statusColumn.y - Theme.margin
        readonly property real stripWidth: window.width - Theme.insetLeft - Theme.insetRight
        x: Theme.compact ? Theme.insetLeft + Math.max(0, (stripWidth - width) / 2) : Theme.insetLeft
        y: Theme.compact ? window.stripTop : Math.max(minY, Math.min((window.height - height) / 2, maxBottom - height))
        width: Theme.compact ? Math.min(createGrid.implicitWidth + 2 * Theme.panelPadding, stripWidth)
                             : createGrid.implicitWidth + 2 * Theme.panelPadding
        height: Theme.compact ? Theme.controlHeight + 2 * Theme.panelPadding
                              : Math.min(createGrid.implicitHeight + 2 * Theme.panelPadding, maxBottom - minY)

        Flickable {
            id: createScroll
            objectName: "createScroll"
            anchors { fill: parent; margins: Theme.panelPadding }
            contentWidth: Theme.compact ? createGrid.implicitWidth : width
            contentHeight: Theme.compact ? height : createGrid.implicitHeight
            clip: true
            interactive: Theme.compact ? contentWidth > width : contentHeight > height
            flickableDirection: Theme.compact ? Flickable.HorizontalFlick : Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

        GridLayout {
            id: createGrid
            // One column, or one row in the compact strip.
            flow: Theme.compact ? GridLayout.LeftToRight : GridLayout.TopToBottom
            width: Theme.compact ? implicitWidth : createScroll.width
            height: Theme.compact ? createScroll.height : implicitHeight
            rowSpacing: 4
            columnSpacing: 4
            SectionLabel { text: "Create"; visible: !Theme.compact }
            ActionButton {
                objectName: "tool_box"
                text: "Box"
                Layout.fillWidth: !Theme.compact
                onClicked: window.app.createBox(20)
                ToolTip.visible: hovered && !Theme.touch
                ToolTip.text: "Add a 20 mm cube (B). Push and pull its faces to shape it."
                ToolTip.delay: 500
            }
            ActionButton {
                id: sketchButton
                objectName: "sketchButton"
                text: "Sketch"
                Layout.fillWidth: !Theme.compact
                enabled: window.app.canStartSketch
                // On a selected face: sketch right there. Otherwise pick an
                // origin plane (beside the button, or above the compact strip).
                onClicked: {
                    if (window.app.faceSelected())
                        window.app.startSketch()
                    else if (Theme.compact)
                        planeMenu.popup(this, 0, -planeMenu.implicitHeight - 6)
                    else
                        planeMenu.popup(this, width + 6, 0)
                }
                ToolTip.visible: hovered && !planeMenu.visible && !Theme.touch
                ToolTip.text: "Sketch on the selected flat face, or on an origin plane (K = ground)."
                ToolTip.delay: 500
            }

            // Tools that act on a selection: with a fitting selection they run,
            // otherwise they say what to select (no hidden gestures to learn).
            SectionLabel { text: "Modify"; Layout.topMargin: 6; visible: !Theme.compact }
            Separator { visible: Theme.compact }
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
                    Layout.fillWidth: !Theme.compact
                    onClicked: { window.app.runTool(modelData.id); viewport.forceActiveFocus() }
                    // Touch has no hover: tapped without a fitting selection, a
                    // tool says what to select instead (runTool).
                    ToolTip.visible: hovered && !Theme.touch
                    ToolTip.text: modelData.tip
                    ToolTip.delay: 500
                }
            }
            SectionLabel { text: "Combine"; Layout.topMargin: 6; visible: !Theme.compact }
            Separator { visible: Theme.compact }
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
                    Layout.fillWidth: !Theme.compact
                    onClicked: { window.app.runTool(modelData.id); viewport.forceActiveFocus() }
                    ToolTip.visible: hovered && !Theme.touch
                    ToolTip.text: modelData.tip + " Select bodies by double-clicking (Shift adds), or in the Model panel."
                    ToolTip.delay: 500
                }
            }
        }
        }
        // More tools below (to the right in the strip): a fade at that edge says "scroll".
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: 28
            radius: 12
            visible: !Theme.compact && createScroll.contentY + createScroll.height < createScroll.contentHeight - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00F9FAFB" }
                GradientStop { position: 1.0; color: Theme.panel }
            }
        }
        Rectangle {
            anchors { top: parent.top; bottom: parent.bottom; right: parent.right; margins: 1 }
            width: 32
            radius: 12
            visible: Theme.compact && createScroll.contentX + createScroll.width < createScroll.contentWidth - 1
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
            visible: Theme.compact && createScroll.contentX > 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.panel }
                GradientStop { position: 1.0; color: "#00F9FAFB" }
            }
        }
    }

    // ---------------------------------------------------------------- compact: Model and View buttons
    // Top right in a compact window; below the top bar when the window is too
    // narrow for both on one row.
    readonly property real rightColumnY: topBar.x + topBar.width + 8 > window.width - Theme.insetRight - modelButtonPanel.width
                                         ? topBar.y + topBar.height + 8 : Theme.insetTop
    Panel {
        id: modelButtonPanel
        objectName: "modelButtonPanel"
        visible: Theme.compact && window.app.history.length > 0
        x: window.width - Theme.insetRight - width
        y: window.rightColumnY
        width: modelButton.implicitWidth + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding
        ActionButton {
            id: modelButton
            objectName: "modelPanelButton"
            anchors.centerIn: parent
            text: "Model"
            checked: window.historyOpen
            onClicked: window.historyOpen = !window.historyOpen
        }
    }
    Panel {
        id: viewButtonPanel
        objectName: "viewButtonPanel"
        visible: Theme.compact
        x: window.width - Theme.insetRight - width
        y: modelButtonPanel.visible ? modelButtonPanel.y + modelButtonPanel.height + 8 : window.rightColumnY
        width: Math.max(viewMenuButton.implicitWidth, modelButton.implicitWidth) + 2 * Theme.panelPadding
        height: Theme.controlHeight + 2 * Theme.panelPadding
        ActionButton {
            id: viewMenuButton
            objectName: "viewMenuButton"
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.panelPadding
            text: "View"
            checked: window.viewMenuOpen
            onClicked: window.viewMenuOpen = !window.viewMenuOpen
        }
    }

    // ---------------------------------------------------------------- model history
    // Regular windows: top right, always there once the model has steps.
    // Compact windows: slides in from the right edge behind the Model button.
    HistoryPanel {
        id: historyPanel
        objectName: "historyPanel"
        app: window.app
        z: Theme.compact ? 3 : 0
        width: Theme.compact ? Math.min(320, window.width - Theme.insetLeft - Theme.insetRight) : 290
        x: Theme.compact && !window.historyOpen ? window.width + 8 : window.width - Theme.insetRight - width
        y: Theme.compact ? modelButtonPanel.y + modelButtonPanel.height + 8 : Theme.insetTop
        visible: window.app.history.length > 0 && (!Theme.compact || window.historyOpen || x < window.width)
        maximumHeight: Theme.compact ? window.bottomStackTop - 8 - y : window.height - 2 * Theme.margin - 80
        onFinished: viewport.forceActiveFocus()
        onCloseRequested: window.historyOpen = false
        Behavior on x {
            enabled: Theme.compact
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
    }

    // ---------------------------------------------------------------- view controls
    AxisTriad {
        id: axisTriad
        objectName: "axisTriad"
        app: window.app
        // Above the view buttons; in a compact window under the View button.
        x: Theme.compact ? window.width - Theme.insetRight - width : viewPanel.x + viewPanel.width - width
        y: Theme.compact ? viewButtonPanel.y + viewButtonPanel.height + 8 : viewPanel.y - 8 - height
    }

    // A tap outside the compact View menu closes it (and does nothing else).
    // The menu is above everything while open (the value chip included: the
    // user asked for it, and any choice closes it).
    MouseArea {
        anchors.fill: parent
        z: 7
        visible: Theme.compact && window.viewMenuOpen
        acceptedButtons: Qt.AllButtons
        onPressed: window.viewMenuOpen = false
        onWheel: (wheel) => wheel.accepted = true
    }

    // Regular windows: a row at the bottom right. Compact windows: the same
    // buttons in a menu that opens from the View button (in columns when the
    // window is short).
    Panel {
        id: viewPanel
        objectName: "viewPanel"
        z: Theme.compact ? 7 : 0
        visible: !Theme.compact || window.viewMenuOpen
        readonly property real menuTop: viewButtonPanel.y + viewButtonPanel.height + 6
        x: window.width - Theme.insetRight - width
        y: Theme.compact ? menuTop : window.height - Theme.insetBottom - height
        width: viewRow.implicitWidth + 2 * Theme.panelPadding
        height: Theme.compact ? viewRow.implicitHeight + 2 * Theme.panelPadding : Theme.controlHeight + 2 * Theme.panelPadding

        function picked() {
            if (Theme.compact)
                window.viewMenuOpen = false
        }

        GridLayout {
            id: viewRow
            anchors.centerIn: parent
            flow: Theme.compact ? GridLayout.TopToBottom : GridLayout.LeftToRight
            // As many rows as fit below the View button (a phone in landscape
            // gets two columns).
            rows: Theme.compact ? Math.max(1, Math.floor((window.height - Theme.insetBottom - viewPanel.menuTop
                                                          - 2 * Theme.panelPadding + 2) / (Theme.controlHeight + 2)))
                                : 1
            rowSpacing: 2
            columnSpacing: 2
            ActionButton { objectName: "viewFit"; text: "Fit"; Layout.fillWidth: true; onClicked: { window.app.fitAll(); viewPanel.picked() } }
            Separator { visible: !Theme.compact }
            ActionButton { objectName: "viewIso"; text: "Iso"; Layout.fillWidth: true; onClicked: { window.app.setView("iso"); viewPanel.picked() } }
            ActionButton { objectName: "viewTop"; text: "Top"; Layout.fillWidth: true; onClicked: { window.app.setView("top"); viewPanel.picked() } }
            ActionButton { objectName: "viewFront"; text: "Front"; Layout.fillWidth: true; onClicked: { window.app.setView("front"); viewPanel.picked() } }
            ActionButton { objectName: "viewRight"; text: "Right"; Layout.fillWidth: true; onClicked: { window.app.setView("right"); viewPanel.picked() } }
            Separator { visible: !Theme.compact }
            ActionButton {
                objectName: "viewProjection"
                Layout.fillWidth: true
                text: window.app.perspective ? "Perspective" : "Orthographic"
                onClicked: { window.app.togglePerspective(); viewPanel.picked() }
            }
            Separator { visible: !Theme.compact }
            ActionButton {
                id: penButton
                objectName: "penModeButton"
                Layout.fillWidth: true
                visible: window.app.touchMode || window.app.penMode
                text: Theme.compact ? (window.app.penMode ? "Pen: on" : "Pen: off") : "Pen"
                checked: window.app.penMode
                onClicked: { window.app.penMode = !window.app.penMode; viewPanel.picked() }
                ToolTip.visible: hovered && !Theme.touch
                ToolTip.text: "Pen mode: the pen selects and draws, fingers only move the view (a resting hand does nothing)."
                ToolTip.delay: 500
            }
            Separator { visible: penButton.visible && !Theme.compact }
            ActionButton {
                objectName: "viewUnit"
                Layout.fillWidth: true
                text: Theme.compact ? "Unit: " + window.app.displayUnit : window.app.displayUnit
                onClicked: { window.app.setDisplayUnit(window.app.displayUnit === "mm" ? "in" : "mm"); viewPanel.picked() }
                ToolTip.visible: hovered && !Theme.touch
                ToolTip.text: "Display unit. You can always type any unit, e.g. 1in or 25mm."
                ToolTip.delay: 500
            }
        }
    }

    // ---------------------------------------------------------------- status / hints
    // Regular windows: bottom left, beside the view buttons; above them when
    // the window is too narrow for both (e.g. an iPad in portrait). Compact
    // windows: across the window, above the tool strip.
    Column {
        id: statusColumn
        objectName: "statusColumn"
        readonly property bool stacked: viewPanel.x < window.width / 2
            || (selectionBar.visible && Theme.insetLeft + selectionBar.width + Theme.margin > viewPanel.x)
        readonly property real fullWidth: window.width - Theme.insetLeft - Theme.insetRight
        x: Theme.insetLeft
        y: Theme.compact ? window.bottomStackTop - 8 - height
                         : (stacked ? viewPanel.y - Theme.margin : window.height - Theme.insetBottom) - height
        width: Theme.compact ? fullWidth : implicitWidth
        spacing: 6

        // What is selected and what can be done with it when there is no
        // manipulator (e.g. two bodies: Union / Subtract / Intersect). With a
        // manipulator, the same actions sit in the value chip instead. In a
        // compact window the row scrolls sideways.
        Panel {
            id: selectionBar
            objectName: "selectionActions"
            visible: !window.app.sketchMode && window.app.contextActions.length > 0
                     && (!window.app.operationActive || !window.app.valueLabelVisible)
            width: selectionActionRow.implicitWidth + 2 * Theme.panelPadding
            height: Theme.controlHeight + 2 * Theme.panelPadding
            ScrollRow {
                id: selectionActionRow
                anchors.centerIn: parent
                maximumWidth: Theme.compact ? statusColumn.fullWidth - 2 * Theme.panelPadding : Infinity
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
            width: Theme.compact ? Math.min(summaryText.implicitWidth + 28, statusColumn.fullWidth) : summaryText.implicitWidth + 28
            height: 34
            Text {
                id: summaryText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 16)
                elide: Text.ElideRight
                text: window.app.selectionSummary
                color: Theme.text
                font.pixelSize: 13
            }
        }
        Text {
            // What to do next. Regular windows: wraps instead of running under
            // the view buttons or the axis marker. Compact windows: one short
            // line (the hint's first part); a tap shows all of it.
            id: hintLine
            objectName: "hintText"
            readonly property string full: window.hintText()
            readonly property string brief: full.split(" · ")[0]
            property bool expanded: false
            readonly property bool oneLine: Theme.compact && !expanded
            onFullChanged: expanded = false
            width: Theme.compact ? statusColumn.fullWidth : (statusColumn.stacked ? axisTriad.x : viewPanel.x) - 2 * Theme.margin
            text: oneLine ? brief + (brief.length < full.length ? " …" : "") : full
            color: Theme.mutedText
            font.pixelSize: 12
            leftPadding: 4
            wrapMode: Text.WordWrap
            maximumLineCount: oneLine ? 1 : 1000
            elide: oneLine ? Text.ElideRight : Text.ElideNone
            MouseArea {
                anchors.fill: parent
                enabled: Theme.compact && hintLine.brief.length < hintLine.full.length
                onClicked: hintLine.expanded = !hintLine.expanded
            }
        }
    }

    // What to do next, worded for the input in use: in the touch layout taps,
    // the on-screen ✓ / ✕ and two-finger gestures instead of clicks, Enter,
    // Esc, Shift and the scroll wheel (AppController::touchWording).
    function hintText() {
        const text = mouseHintText()
        return app.touchMode ? app.touchWording(text) : text
    }

    function mouseHintText() {
        if (app.sketchMode)
            return app.sketchHint
        if (app.operationPrompt.length > 0)
            return app.operationPrompt
        if (app.operationActive && app.operationTitle === "Align")
            return "Drag the arrow or type an offset · Flip turns it around · click another face to re-aim · Enter applies"
        if (app.operationActive && app.operationTitle === "Extrude" && app.operationValueLabel === "Thickness")
            return "Drag the arrow or type the total thickness (half on each side of the sketch) · Enter applies"
        if (app.operationActive && app.operationTitle === "Extrude" && !app.operationHasValue)
            return "Drag the arrow or type a distance · \"Up to face\" ends it on a face you click · Shift-click adds profiles"
        if (app.bodyCount === 0 && app.sketchCount > 0)
            return "Click inside a closed sketch shape to extrude it · double-click it to edit the sketch"
        if (app.bodyCount === 0)
            return "Add a box, or start a sketch."
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
            width: Math.min(implicitWidth, window.width - Theme.insetLeft - Theme.insetRight)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Pull faces, round edges, or sketch a profile and extrude it."
            font.pixelSize: 14
            color: Theme.mutedText
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            ActionButton {
                objectName: "emptyAddBox"
                text: "Add a box"
                accent: true
                onClicked: window.app.createBox(20)
            }
            ActionButton {
                objectName: "emptyStartSketch"
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
        // Room on the top row between the top bar and what is on the right.
        topRowLeft: topBar.x + topBar.width + 8
        topRowRight: Theme.compact ? (modelButtonPanel.visible ? modelButtonPanel.x : viewButtonPanel.x) - 8
                                   : window.width - Theme.insetRight
        topRowBottom: Math.max(topBar.y + topBar.height, Theme.compact && window.rightColumnY > Theme.insetTop
                                                         ? viewButtonPanel.y + viewButtonPanel.height : 0)
        onFinished: viewport.forceActiveFocus()
    }

    // ---------------------------------------------------------------- operation chip
    ValueChip {
        id: valueChip
        objectName: "valueChip"
        app: window.app
        z: 6 // nothing covers its field (the View menu may, while open: any choice closes it)
        // The compact Model panel covers most of a phone: the chip waits until it closes.
        visible: window.app.operationActive && window.app.valueLabelVisible && !(Theme.compact && window.historyOpen)
        maximumWidth: window.width - Theme.insetLeft - Theme.insetRight
        // Beside the arrow tip (right side, or left if there is no room),
        // so the manipulator itself is never covered.
        readonly property real tipX: window.app.valueLabelPosition.x
        readonly property real tipY: window.app.valueLabelPosition.y
        readonly property bool fitsRight: tipX + 28 + width < window.width - Theme.insetRight
        readonly property real minY: topBar.y + topBar.height + 8
        x: Theme.compact ? Math.max(Theme.insetLeft, Math.min(window.width - Theme.insetRight - width,
                                                              fitsRight ? tipX + 28 : tipX - 28 - width))
                         : Math.max(Theme.insetLeft, fitsRight ? tipX + 28 : tipX - 28 - width)
        // Compact windows: never over the hint, the selection or the tool strip.
        y: Theme.compact ? Math.max(minY, Math.min(statusColumn.y - 8 - height, tipY - 24))
                         : Math.max(minY, Math.min(window.height - Theme.safeBottom - height - 70, tipY - 24))
        onFinished: viewport.forceActiveFocus()
    }

    // ---------------------------------------------------------------- help
    HelpOverlay {
        id: helpOverlay
        objectName: "helpOverlay"
        appFolder: window.app.savesToAppFolder
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
                window.saveAs()
            }
        }
        onVisibleChanged: if (!visible) window.focusViewUnlessPanel()
    }

    // iPhone / iPad: Save asks for a name only (see saveAs()).
    SaveNameOverlay {
        id: saveNamePrompt
        objectName: "saveNamePrompt"
        app: window.app
        anchors.fill: parent
        z: 125 // above "Save changes?", whose Save may ask for the name
        onSaved: {
            if (window.afterSave) {
                const action = window.afterSave
                window.afterSave = null
                action()
            }
        }
        onCancelled: window.afterSave = null
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
        // Above the hint and the tool strip in a compact window.
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom
                  bottomMargin: Theme.compact ? window.height - statusColumn.y + 8 : 72 + Theme.safeBottom }
        width: toastText.width + 32
        height: Math.max(38, toastText.implicitHeight + 16)
        radius: Math.min(19, height / 2)
        color: Theme.toast
        opacity: 0
        visible: opacity > 0
        Text {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, window.width - Theme.insetLeft - Theme.insetRight - 32)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
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

    // ---------------------------------------------------------------- safe-area preview
    // With --safe-area: shade what a phone's Dynamic Island, rounded corners
    // and home indicator take, so screenshots show that nothing sits there.
    Item {
        anchors.fill: parent
        z: 1000
        visible: window.simulatingSafeArea
        enabled: false
        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: Theme.safeTop; color: "#33D93F42" }
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: Theme.safeBottom; color: "#33D93F42" }
        Rectangle { anchors { left: parent.left; top: parent.top; bottom: parent.bottom } width: Theme.safeLeft; color: "#33D93F42" }
        Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: Theme.safeRight; color: "#33D93F42" }
        // The island: top center in portrait, on the left side in landscape.
        Rectangle {
            readonly property bool portrait: window.height >= window.width
            visible: portrait ? Theme.safeTop > 40 : Theme.safeLeft > 40
            width: portrait ? 126 : 37
            height: portrait ? 37 : 126
            radius: 18.5
            color: "black"
            x: portrait ? (parent.width - width) / 2 : 11
            y: portrait ? 11 : (parent.height - height) / 2
        }
        Rectangle { // the home indicator
            visible: Theme.safeBottom > 0
            width: Math.min(140, parent.width / 3)
            height: 5
            radius: 2.5
            color: "black"
            x: (parent.width - width) / 2
            y: parent.height - 8 - height
        }
    }

    // ---------------------------------------------------------------- dialogs
    // On iOS Open is the system document picker (Qt's FileDialog), starting
    // in OpenShape's folder; saving and exporting need no dialog there.
    FileDialog {
        id: openDialog
        title: "Open project"
        currentFolder: window.app.projectFolder !== "" ? window.app.projectFolder : window.app.appFolderUrl
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
