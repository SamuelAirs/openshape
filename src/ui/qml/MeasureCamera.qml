// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import OpenShape

// Camera (docs/MEASURE.md): ARKit measures in the room. Pick Distance
// (points in space), Outline (points on one surface, closed) or Box (three
// corners of its base, then its top); Start shows the native camera view
// until Done, and its result comes back here to keep, use, sketch (an
// outline) or make into a box.
Item {
    id: camera

    property MeasureController m
    property Item screen

    property string mode: "distance"
    readonly property var result: m ? m.arResult : ({})
    readonly property bool hasResult: result && result.outcome === "done"
    readonly property string resultMode: hasResult ? result.mode : ""
    readonly property bool narrow: width < 560

    readonly property var reading: {
        if (!hasResult)
            return { valid: false, items: [], caption: "", source: "Camera" }
        const items = []
        if (resultMode === "distance") {
            const d = result.distances
            if (d.length > 1)
                items.push({ label: "Total", quantity: "length", value: result.total })
            for (let i = 0; i < d.length; ++i)
                items.push({ label: d.length > 1 ? "Segment " + (i + 1) : "Distance", quantity: "length", value: d[i] })
        } else if (resultMode === "outline") {
            items.push({ label: "Area", quantity: "area", value: result.area })
            items.push({ label: "Perimeter", quantity: "length", value: result.perimeter })
            for (let i = 0; i < result.sides.length; ++i)
                items.push({ label: "Side " + (i + 1), quantity: "length", value: result.sides[i] })
        } else if (resultMode === "box") {
            items.push({ label: "Width", quantity: "length", value: result.width })
            items.push({ label: "Depth", quantity: "length", value: result.depth })
            items.push({ label: "Height", quantity: "length", value: result.height })
        }
        return { valid: items.length > 0, caption: "in the camera (AR)", source: "Camera", items: items }
    }
    readonly property var actions: resultMode === "outline" ? [{ key: "outlineSketch", label: "Make sketch", icon: "sketch" }]
                                 : resultMode === "box" ? [{ key: "box", label: "Make box", icon: "box" }] : []
    function runAction(key) {
        let made = false
        if (key === "outlineSketch")
            made = m.makeOutlineSketch(result.outline, false)
        else if (key === "box")
            made = m.makeBox(result.width, result.depth, result.height)
        if (made)
            screen.close()
        else
            screen.toast(key === "box" ? "The box couldn't be made" : "The sketch couldn't be made", true)
    }

    MeasureNotice {
        objectName: "cameraUnavailable"
        anchors.fill: parent
        visible: camera.m && !camera.m.arAvailable
        iconName: "ar"
        title: "Camera measuring needs ARKit"
        text: "On an iPhone or iPad, the camera measures distances in the room, an outline on a surface and a box's sides."
    }

    ColumnLayout {
        anchors.fill: parent
        visible: camera.m && camera.m.arAvailable
        spacing: 12

        // The three ways, as tiles.
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: [{ key: "distance", label: "Distance", icon: "distance", text: "Point to point" },
                        { key: "outline", label: "Outline", icon: "sketch_polygon", text: "On a surface" },
                        { key: "box", label: "Box", icon: "box", text: "Width, depth, height" }]
                delegate: AbstractButton {
                    id: tile
                    required property var modelData
                    readonly property bool selected: camera.mode === modelData.key
                    objectName: "cameraMode_" + modelData.key
                    Layout.fillWidth: true
                    Layout.preferredHeight: camera.narrow ? 76 : 92
                    focusPolicy: Qt.NoFocus
                    onClicked: camera.mode = modelData.key
                    background: MeasureCard {
                        color: tile.selected ? Theme.menuHighlight : tile.pressed ? Theme.pressed : Theme.card
                        border.color: tile.selected ? Theme.accent : Theme.cardBorder
                        border.width: tile.selected ? 1.5 : 1
                    }
                    contentItem: Item {
                        Column {
                            anchors.centerIn: parent
                            spacing: 4
                            LineIcon {
                                anchors.horizontalCenter: parent.horizontalCenter
                                name: tile.modelData.icon
                                size: 24
                                color: tile.selected ? Theme.accent : Theme.iconColor
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: tile.modelData.label
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: tile.selected ? Theme.accent : Theme.text
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: !camera.narrow
                                text: tile.modelData.text
                                font.pixelSize: 11
                                color: Theme.mutedText
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: camera.m && camera.m.lidar ? "LiDAR: points land on any surface, good to about 1 cm."
                    : "Without LiDAR, points land on the flat surfaces ARKit finds. For rooms and furniture, not print tolerances."
                font.pixelSize: 13
                color: Theme.mutedText
                wrapMode: Text.WordWrap
            }
            ActionButton {
                objectName: "cameraStart"
                text: camera.m && camera.m.arBusy ? "Measuring…" : "Start"
                iconName: "camera"
                accent: true
                enabled: camera.m && !camera.m.arBusy
                onClicked: camera.m.startAr(camera.mode)
            }
        }

        // The result.
        MeasureCard {
            id: resultCard
            objectName: "cameraResult"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 56
            clip: true
            // Too little room for a drawing (the iPhone Duo's outer display):
            // the result in one line.
            readonly property bool compact: height < 150

            Row {
                objectName: "cameraResultCompact"
                anchors.centerIn: parent
                visible: resultCard.compact && camera.hasResult
                spacing: 10
                LineIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: camera.resultMode === "box" ? "box" : camera.resultMode === "outline" ? "sketch_polygon" : "distance"
                    size: 22
                    color: Theme.accent
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, resultCard.width - 70)
                    elide: Text.ElideRight
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                    color: Theme.text
                    text: {
                        if (!camera.hasResult || !camera.m)
                            return ""
                        const r = camera.result
                        const len = (v) => camera.m.formatNumber("length", v)
                        const unit = camera.m.unitSymbol("length")
                        if (camera.resultMode === "box")
                            return len(r.width) + " × " + len(r.depth) + " × " + len(r.height) + " " + unit
                        if (camera.resultMode === "outline")
                            return r.sides.length + " sides · " + len(r.boundsWidth) + " × " + len(r.boundsHeight) + " " + unit
                        return r.distances.length > 1 ? r.distances.length + " segments · " + camera.m.formatValue("length", r.total)
                                                      : camera.m.formatValue("length", r.total)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: !camera.hasResult
                maximumLineCount: resultCard.compact ? 2 : 6
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: camera.result && camera.result.outcome === "failed" ? (camera.result.error || "The camera couldn't measure.")
                    : "Start, then aim the crosshair at each point and tap Add point; Done brings the result here."
                font.pixelSize: 14
                color: camera.result && camera.result.outcome === "failed" ? Theme.error : Theme.mutedText
            }

            // Distance: the segments.
            Column {
                anchors.centerIn: parent
                visible: camera.resultMode === "distance" && !resultCard.compact
                spacing: 6
                Repeater {
                    model: camera.resultMode === "distance" ? camera.result.distances : []
                    Text {
                        required property var modelData
                        required property int index
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (camera.result.distances.length > 1 ? "Segment " + (index + 1) + "   " : "")
                              + camera.m.formatValue("length", modelData)
                        font.pixelSize: camera.result.distances.length > 1 ? 16 : 28
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                        color: Theme.text
                    }
                }
            }

            // Outline: the shape at its proportions.
            Item {
                id: outlineView
                anchors.fill: parent
                anchors.margins: 28
                visible: camera.resultMode === "outline" && !resultCard.compact
                readonly property var pts: camera.resultMode === "outline" ? camera.result.outline : []
                readonly property real bw: camera.resultMode === "outline" ? Math.max(1, camera.result.boundsWidth) : 1
                readonly property real bh: camera.resultMode === "outline" ? Math.max(1, camera.result.boundsHeight) : 1
                readonly property real k: Math.min(width / bw, height / bh)
                readonly property real minX: pts.length ? Math.min(...pts.map(p => p.x)) : 0
                readonly property real maxY: pts.length ? Math.max(...pts.map(p => p.y)) : 0
                function toView(p) {
                    if (!p)
                        return Qt.point(0, 0)
                    return Qt.point((width - bw * k) / 2 + (p.x - minX) * k, (height - bh * k) / 2 + (maxY - p.y) * k)
                }
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 2.5
                        fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                        joinStyle: ShapePath.RoundJoin
                        PathPolyline {
                            path: {
                                const v = outlineView.pts.map(p => outlineView.toView(p))
                                return v.length ? v.concat([v[0]]) : []
                            }
                        }
                    }
                }
                Repeater {
                    model: camera.resultMode === "outline" ? camera.result.sides : []
                    Rectangle {
                        required property var modelData
                        required property int index
                        readonly property point a: outlineView.toView(outlineView.pts[index])
                        readonly property point b: outlineView.toView(outlineView.pts[(index + 1) % outlineView.pts.length])
                        x: (a.x + b.x) / 2 - width / 2
                        y: (a.y + b.y) / 2 - height / 2
                        width: sideText.implicitWidth + 16
                        height: 24
                        radius: 12
                        color: Theme.toast
                        Text {
                            id: sideText
                            anchors.centerIn: parent
                            text: camera.m.formatValue("length", parent.modelData)
                            color: "white"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            font.features: ({ "tnum": 1 })
                        }
                    }
                }
            }

            // Box: the box drawn at its proportions (isometric), its three sizes on its edges.
            Item {
                id: boxView
                anchors.fill: parent
                anchors.margins: 28
                visible: camera.resultMode === "box" && !resultCard.compact
                readonly property real biggest: camera.resultMode === "box" ? Math.max(1, camera.result.width, camera.result.depth, camera.result.height) : 1
                readonly property real wn: camera.resultMode === "box" ? Math.max(0.25, camera.result.width / biggest) : 1
                readonly property real dn: camera.resultMode === "box" ? Math.max(0.25, camera.result.depth / biggest) : 1
                readonly property real hn: camera.resultMode === "box" ? Math.max(0.25, camera.result.height / biggest) : 1
                readonly property real spanX: (wn + dn) * 0.866
                readonly property real spanY: hn + (wn + dn) * 0.5
                readonly property real k: Math.min(width / spanX, height / spanY) * 0.78
                readonly property real ox: (width - spanX * k) / 2 + dn * 0.866 * k
                readonly property real oy: (height - spanY * k) / 2 + hn * k
                function at(u, v, w) {
                    return Qt.point(ox + (u * wn - v * dn) * 0.866 * k, oy + (u * wn + v * dn) * 0.5 * k - w * hn * k)
                }
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 2
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                        PathPolyline { path: [boxView.at(0, 0, 1), boxView.at(1, 0, 1), boxView.at(1, 1, 1), boxView.at(0, 1, 1), boxView.at(0, 0, 1)] }
                    }
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 2
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.20)
                        PathPolyline { path: [boxView.at(1, 0, 0), boxView.at(1, 1, 0), boxView.at(1, 1, 1), boxView.at(1, 0, 1), boxView.at(1, 0, 0)] }
                    }
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 2
                        joinStyle: ShapePath.RoundJoin
                        fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.13)
                        PathPolyline { path: [boxView.at(0, 1, 0), boxView.at(1, 1, 0), boxView.at(1, 1, 1), boxView.at(0, 1, 1), boxView.at(0, 1, 0)] }
                    }
                }
                BoxLabel { at: boxView.at(0.5, 1, 0); text: camera.hasResult ? "W " + camera.m.formatValue("length", camera.result.width) : ""; dx: -22; dy: 18 }
                BoxLabel { at: boxView.at(1, 0.5, 0); text: camera.hasResult ? "D " + camera.m.formatValue("length", camera.result.depth) : ""; dx: 26; dy: 16 }
                BoxLabel { at: boxView.at(1, 0, 0.5); text: camera.hasResult ? "H " + camera.m.formatValue("length", camera.result.height) : ""; dx: 52 }
            }
        }
    }

    component BoxLabel: Rectangle {
        property point at
        property real dx: 0
        property real dy: 0
        property alias text: boxText.text
        x: at.x + dx - width / 2
        y: at.y + dy - height / 2
        width: boxText.implicitWidth + 16
        height: 24
        radius: 12
        color: Theme.toast
        Text {
            id: boxText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
        }
    }
}
