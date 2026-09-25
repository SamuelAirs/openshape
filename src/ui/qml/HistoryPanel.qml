import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The model tree: sketches, bodies and each body's steps, in order. Click a
// step to edit its values; failures are shown in place with an explanation.
Panel {
    id: panel

    required property AppController app
    property real maximumHeight: 600
    property bool collapsed: false
    property string expandedId: ""
    signal finished()

    width: 290
    height: collapsed ? header.height + 2 * Theme.panelPadding
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
                compact: true
                text: panel.collapsed ? "Show" : "Hide"
                onClicked: panel.collapsed = !panel.collapsed
            }
        }

        ListView {
            id: list
            visible: !panel.collapsed
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
                objectName: "historyRow_" + modelData.id
                readonly property bool isFeature: modelData.kind === "feature"
                readonly property bool expanded: panel.expandedId === modelData.id
                readonly property bool failed: modelData.status === "failed"
                readonly property bool inactive: modelData.status === "suppressed" || modelData.status === "blocked"

                width: ListView.view.width
                height: content.implicitHeight + 12
                radius: 8
                color: expanded ? Theme.checked : rowMouse.containsMouse ? Theme.hover : "transparent"

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.expandedId = row.expanded ? "" : row.modelData.id
                    onDoubleClicked: if (row.modelData.kind === "sketch") panel.app.editSketch(row.modelData.id)
                }

                ColumnLayout {
                    id: content
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                    anchors.leftMargin: row.isFeature ? 22 : 8
                    anchors.rightMargin: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            // Status dot: failures are red, inactive steps grey.
                            visible: row.isFeature
                            width: 7
                            height: 7
                            radius: 3.5
                            color: row.failed ? Theme.error : row.inactive ? "#B8BEC6" : "#4CAF6A"
                        }
                        Text {
                            text: row.modelData.name
                            font.pixelSize: 13
                            font.weight: row.isFeature ? Font.Normal : Font.DemiBold
                            font.strikeout: row.modelData.status === "suppressed"
                            color: row.failed ? Theme.error : row.inactive || !row.modelData.visible ? Theme.mutedText : Theme.text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.detail
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
                        visible: row.modelData.message.length > 0 && (row.failed || row.expanded || row.modelData.status === "blocked")
                        Layout.fillWidth: true
                        text: row.modelData.status === "blocked" && row.modelData.message.length === 0
                              ? "Not computed" : row.modelData.message
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: row.failed ? Theme.error : Theme.mutedText
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
                                horizontalAlignment: TextInput.AlignRight
                                selectByMouse: true
                                background: Rectangle {
                                    radius: 6
                                    color: "white"
                                    border.color: valueField.activeFocus ? Theme.accent : Theme.panelBorder
                                    border.width: valueField.activeFocus ? 1.5 : 1
                                }
                                onActiveFocusChanged: if (activeFocus) selectAll()
                                onAccepted: {
                                    const error = panel.app.setFeatureParameter(row.modelData.id, paramRow.modelData.key, text)
                                    paramError.text = error
                                    if (error.length === 0)
                                        panel.finished()
                                }
                                Keys.onEscapePressed: {
                                    text = paramRow.modelData.value
                                    panel.finished()
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
                    Row {
                        visible: row.expanded
                        spacing: 4
                        ActionButton {
                            compact: true
                            visible: row.modelData.kind === "sketch"
                            text: "Edit sketch"
                            onClicked: panel.app.editSketch(row.modelData.id)
                        }
                        ActionButton {
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
                            compact: true
                            visible: row.modelData.canDelete
                            text: "Delete"
                            onClicked: {
                                panel.expandedId = ""
                                panel.app.deleteHistoryItem(row.modelData.kind, row.modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }
}
