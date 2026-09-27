// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The model tree: sketches, construction axes and planes, bodies and each
// body's steps, in order. Hovering
// a row highlights its geometry in the view (so does an expanded row: touch
// has no hover); clicking a body selects it (Shift, or any tap in the touch
// layout, adds another, e.g. to combine); clicking a step edits its values.
// Failures and warnings are shown in place with an explanation.
Panel {
    id: panel

    required property AppController app
    property real maximumHeight: 600
    property bool collapsed: false
    property string expandedId: ""
    signal finished()
    // Close in the compact layout (the panel slides away; Main.qml decides).
    signal closeRequested()
    // Collapsing is for the regular layout; a compact window closes the panel.
    readonly property bool folded: collapsed && !Theme.compact

    onExpandedIdChanged: app.highlightHistoryItem(expandedId)

    width: 290
    height: folded ? header.height + 2 * Theme.panelPadding
                   : Math.min(maximumHeight, header.height + list.contentHeight + 3 * Theme.panelPadding)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelPadding
        spacing: 4

        RowLayout {
            id: header
            Layout.fillWidth: true
            SectionLabel { text: "Model" }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "historyPanelHide"
                compact: true
                text: Theme.compact ? "Close" : panel.collapsed ? "Show" : "Hide"
                onClicked: {
                    if (Theme.compact)
                        panel.closeRequested()
                    else
                        panel.collapsed = !panel.collapsed
                }
            }
        }

        ListView {
            id: list
            visible: !panel.folded
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            model: panel.app.history
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool isFeature: modelData.kind === "feature"
                readonly property bool expanded: panel.expandedId === modelData.id
                readonly property bool failed: modelData.status === "failed"
                readonly property bool warned: modelData.status === "warning"
                readonly property bool inactive: modelData.status === "suppressed" || modelData.status === "blocked"

                width: ListView.view.width
                height: content.implicitHeight + 12
                radius: 8
                color: expanded ? Theme.checked : rowMouse.containsMouse ? Theme.hover : "transparent"

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: panel.app.highlightHistoryItem(row.modelData.id)
                    onExited: panel.app.highlightHistoryItem(panel.expandedId)
                    onClicked: (mouse) => {
                        // Selecting rebuilds the rows (and this delegate): capture first.
                        const owner = panel
                        const data = row.modelData
                        const wasExpanded = row.expanded
                        // Shift adds a body (to combine them) or takes it out
                        // again; a tap in the touch layout adds it (as taps in
                        // the view do) and never takes it out, so tapping the
                        // row again to fold it keeps the body selected.
                        if (data.kind === "body" && data.visible && owner.app.touchMode)
                            owner.app.addBodyToSelection(data.id)
                        else if (data.kind === "body" && data.visible)
                            owner.app.selectBody(data.id, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                        else if (data.kind === "datum" && data.visible && !wasExpanded)
                            owner.app.selectDatum(data.id) // its actions appear (Sketch on a plane)
                        owner.expandedId = wasExpanded ? "" : data.id
                    }
                    onDoubleClicked: if (row.modelData.kind === "sketch") panel.app.editSketch(row.modelData.id)
                }

                ColumnLayout {
                    id: content
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    anchors.leftMargin: row.isFeature ? 22 : 8
                    anchors.rightMargin: 8
                    spacing: 4

                    RowLayout {
                        // The row's title line: what a click on the row hits
                        // (the acceptance run clicks its center; an expanded
                        // row's action buttons sit below it).
                        objectName: "historyRow_" + row.modelData.id
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            // Status dot: failures are red, inactive steps grey.
                            visible: row.isFeature
                            width: 7
                            height: 7
                            radius: 3.5
                            color: row.failed ? Theme.error : row.warned ? "#E0A030" : row.inactive ? "#B8BEC6" : "#4CAF6A"
                        }
                        // Names and messages come from files (STEP product
                        // names, projects): shown as plain text, never as HTML.
                        Text {
                            text: row.modelData.name
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            font.weight: row.isFeature ? Font.Normal : Font.DemiBold
                            font.strikeout: row.modelData.status === "suppressed"
                            color: row.failed ? Theme.error : row.inactive || !row.modelData.visible ? Theme.mutedText : Theme.text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.detail
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            color: Theme.mutedText
                        }
                        Text {
                            visible: !row.isFeature && !row.modelData.visible
                            text: "hidden"
                            font.pixelSize: 11
                            color: Theme.mutedText
                        }
                    }

                    Text {
                        visible: row.modelData.message.length > 0
                                 && (row.failed || row.warned || row.expanded || row.modelData.status === "blocked")
                        Layout.fillWidth: true
                        text: row.modelData.status === "blocked" && row.modelData.message.length === 0
                              ? "Not computed" : row.modelData.message
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: row.failed ? Theme.error : row.warned ? "#9A6A00" : Theme.mutedText
                    }

                    // A body in separate pieces: make each piece a body (right
                    // under the warning on the body; on the step that caused it
                    // while that step is expanded).
                    ActionButton {
                        objectName: "historySplit_" + row.modelData.id
                        compact: true
                        visible: row.modelData.canSplit && (row.modelData.kind === "body" || row.expanded)
                        text: "Split into bodies"
                        onClicked: {
                            // Splitting rebuilds the rows (and this delegate): capture first.
                            const owner = panel
                            const body = row.modelData.bodyId
                            owner.expandedId = ""
                            owner.app.splitBody(body)
                            owner.finished()
                        }
                    }

                    // Editable values of the expanded step.
                    Repeater {
                        model: row.expanded ? row.modelData.parameters : []
                        delegate: RowLayout {
                            id: paramRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: paramRow.modelData.label
                                font.pixelSize: 12
                                color: Theme.mutedText
                                Layout.preferredWidth: 60
                            }
                            TextField {
                                id: valueField
                                objectName: "historyParam_" + row.modelData.id + "_" + paramRow.modelData.key
                                Layout.fillWidth: true
                                implicitHeight: 30
                                text: paramRow.modelData.value
                                font.pixelSize: 13
                                // Text (a Text step's words) reads from the left; numbers line up on the right.
                                horizontalAlignment: paramRow.modelData.isText ? TextInput.AlignLeft : TextInput.AlignRight
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 6
                                    color: "white"
                                    border.color: valueField.activeFocus ? Theme.accent : Theme.panelBorder
                                    border.width: valueField.activeFocus ? 1.5 : 1
                                }
                                onActiveFocusChanged: if (activeFocus) selectAll()
                                onAccepted: {
                                    // A successful edit rebuilds the history and
                                    // destroys this delegate: capture first.
                                    const owner = panel
                                    const error = owner.app.setFeatureParameter(row.modelData.id, paramRow.modelData.key, text)
                                    if (error.length === 0) {
                                        owner.finished()
                                        return
                                    }
                                    paramError.text = error
                                }
                                Keys.onEscapePressed: {
                                    const owner = panel
                                    text = paramRow.modelData.value
                                    owner.finished()
                                }
                            }
                            Text {
                                id: paramError
                                visible: text.length > 0
                                color: Theme.error
                                font.pixelSize: 11
                            }
                        }
                    }

                    // Actions for the expanded row.
                    // (Wraps in a narrow, compact panel.)
                    Flow {
                        visible: row.expanded
                        Layout.fillWidth: true
                        spacing: 4
                        ActionButton {
                            compact: true
                            visible: row.modelData.kind === "sketch"
                            text: "Edit sketch"
                            onClicked: panel.app.editSketch(row.modelData.id)
                        }
                        ActionButton {
                            compact: true
                            objectName: "historyDuplicate_" + row.modelData.id
                            visible: row.modelData.kind === "body"
                            text: "Duplicate"
                            onClicked: {
                                // Duplicating rebuilds the rows (and this delegate): capture first.
                                const owner = panel
                                const id = row.modelData.id
                                owner.expandedId = ""
                                owner.app.duplicateBody(id)
                                owner.finished()
                            }
                        }
                        ActionButton {
                            objectName: "historyVisibility_" + row.modelData.id
                            compact: true
                            visible: row.modelData.kind !== "feature"
                            text: row.modelData.visible ? "Hide" : "Show"
                            onClicked: panel.app.setHistoryItemVisible(row.modelData.kind, row.modelData.id, !row.modelData.visible)
                        }
                        ActionButton {
                            compact: true
                            visible: row.modelData.canSuppress
                            text: row.modelData.status === "suppressed" ? "Restore" : "Suppress"
                            onClicked: panel.app.setFeatureSuppressed(row.modelData.id, row.modelData.status !== "suppressed")
                        }
                        ActionButton {
                            // A body others are built from is hidden instead
                            // (the controller says so); a hidden one has no Delete.
                            objectName: "historyDelete_" + row.modelData.id
                            compact: true
                            visible: row.modelData.canDelete
                            text: "Delete"
                            onClicked: {
                                const owner = panel
                                const kind = row.modelData.kind
                                const id = row.modelData.id
                                owner.expandedId = ""
                                owner.app.deleteHistoryItem(kind, id)
                            }
                        }
                    }
                }
            }
        }
    }
}
