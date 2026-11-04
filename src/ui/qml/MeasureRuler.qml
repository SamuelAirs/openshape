// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The Ruler (docs/MEASURE.md): the screen at true size. A scale along the
// long side of the instrument (mm with cm numbers, or inches in 1/16), two
// jaws to drag to a thing's ends with the distance between them, and a
// Circle gauge: a ring dragged to fit a round thing gives its diameter.
// The scale is MeasureController.pointsPerMm (the device's density, or a
// card's check: CardCalibration).
Item {
    id: ruler

    property MeasureController m
    property Item screen

    // "length" (the jaws) or "circle" (the ring).
    property string mode: "length"
    // The jaws and the ring, in millimeters (the scale may change under them).
    property real jawA: 0
    property real jawB: 40
    property real ringDiameter: 24
    // The ring's center, as a fraction of the field.
    property real ringX: 0.5
    property real ringY: 0.42

    readonly property real ppm: m ? m.pointsPerMm : 3.78
    readonly property bool inches: m ? m.unit === "in" : false
    // The scale runs along the longer side: down the left of a tall
    // instrument, along the top of a wide one.
    readonly property bool vertical: height >= width
    readonly property real stripThickness: Theme.touch ? 64 : 58
    // Zero, a little in from the strip's start (visible, clear of the edge).
    readonly property real zero: 14
    readonly property real stripLength: vertical ? height : width
    readonly property real maxMm: Math.max(1, (stripLength - zero - 6) / ppm)
    // The jaws as shown: within the scale (a smaller window keeps their
    // millimeters for a larger one).
    readonly property real shownA: Math.min(jawA, maxMm)
    readonly property real shownB: Math.min(jawB, maxMm)
    readonly property real lengthMm: Math.abs(shownB - shownA)

    readonly property var reading: mode === "circle"
        ? { valid: true, caption: m && m.scaleCalibrated ? "across the ring" : "across the ring · estimated scale", source: "Ruler",
            items: [{ label: "Diameter", quantity: "length", value: ringDiameter },
                    { label: "Radius", quantity: "length", value: ringDiameter / 2 }] }
        : { valid: true, caption: m && m.scaleCalibrated ? "between the jaws" : "between the jaws · estimated scale", source: "Ruler",
            items: [{ label: "Length", quantity: "length", value: lengthMm }] }
    readonly property var actions: mode === "circle" ? [{ key: "circleSketch", label: "Make sketch", icon: "sketch_circle" }] : []
    function runAction(key) {
        if (key === "circleSketch") {
            if (m.makeCircleSketch(ringDiameter))
                screen.close()
            else
                screen.toast("The circle sketch couldn't be made", true)
        }
    }

    onInchesChanged: if (jawA === 0 && (jawB === 40 || jawB === 38.1)) jawB = inches ? 38.1 : 40

    // A position along the scale (pt from the strip's start) for a length.
    function along(mm) { return zero + mm * ppm }
    // The jaws' handles: beside the strip, one after the other across it
    // (they never cover each other, however close the jaws).
    readonly property real handleA: stripThickness + 10 + 22
    readonly property real handleB: stripThickness + 10 + 22 + 54

    // ---------------------------------------------------------------- the scale
    MeasureCard {
        id: strip
        objectName: "rulerStrip"
        x: 0
        y: 0
        width: ruler.vertical ? ruler.stripThickness : ruler.width
        height: ruler.vertical ? ruler.height : ruler.stripThickness
        radius: 10

        Repeater {
            model: {
                const step = ruler.inches ? 25.4 / 16 : 1
                return Math.max(0, Math.floor(ruler.maxMm / step) + 1)
            }
            delegate: Item {
                id: tick
                required property int index
                readonly property real step: ruler.inches ? 25.4 / 16 : 1
                readonly property real pos: ruler.along(index * step)
                // 0 long (a number), 1 half, 2 quarter / 5 mm, 3 eighth, 4 short.
                readonly property int level: ruler.inches
                    ? (index % 16 === 0 ? 0 : index % 8 === 0 ? 1 : index % 4 === 0 ? 2 : index % 2 === 0 ? 3 : 4)
                    : (index % 10 === 0 ? 0 : index % 5 === 0 ? 2 : 4)
                readonly property real len: [26, 19, 15, 11, 8][level]
                Rectangle {
                    color: Theme.text
                    opacity: tick.level === 0 ? 0.9 : 0.62
                    readonly property real thick: tick.level === 0 ? 1.5 : 1
                    x: ruler.vertical ? strip.width - tick.len : tick.pos - thick / 2
                    y: ruler.vertical ? tick.pos - thick / 2 : strip.height - tick.len
                    width: ruler.vertical ? tick.len : thick
                    height: ruler.vertical ? thick : tick.len
                }
                Text {
                    visible: tick.level === 0
                    text: ruler.inches ? tick.index / 16 : tick.index / 10
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    font.features: ({ "tnum": 1 })
                    color: Theme.text
                    x: ruler.vertical ? strip.width - tick.len - 6 - width : tick.pos - width / 2
                    y: ruler.vertical ? tick.pos - height / 2 : strip.height - tick.len - 4 - height
                }
            }
        }
        Text {
            x: ruler.vertical ? 8 : ruler.along(ruler.inches ? 25.4 * 0.5 : 5) - width / 2
            y: ruler.vertical ? ruler.along(ruler.inches ? 25.4 * 0.5 : 5) - height / 2 : 6
            text: ruler.inches ? "in" : "cm"
            font.pixelSize: 11
            color: Theme.mutedText
        }
    }

    // ---------------------------------------------------------------- the jaws
    Item {
        id: jaws
        anchors.fill: parent
        visible: ruler.mode === "length"

        // Between the jaws.
        Rectangle {
            readonly property real a: ruler.along(Math.min(ruler.shownA, ruler.shownB))
            readonly property real b: ruler.along(Math.max(ruler.shownA, ruler.shownB))
            x: ruler.vertical ? strip.width : a
            y: ruler.vertical ? a : strip.height
            width: ruler.vertical ? ruler.width - strip.width : b - a
            height: ruler.vertical ? b - a : ruler.height - strip.height
            color: Theme.accent
            opacity: 0.08
        }
        Jaw { objectName: "rulerJawA"; mm: ruler.shownA; handleAt: ruler.handleA; onMoved: (mm) => ruler.jawA = mm }
        Jaw { objectName: "rulerJawB"; mm: ruler.shownB; handleAt: ruler.handleB; onMoved: (mm) => ruler.jawB = mm }

        // The distance, beside the second handle, halfway between the jaws.
        Rectangle {
            objectName: "rulerDistanceLabel"
            readonly property real mid: ruler.along((ruler.shownA + ruler.shownB) / 2)
            x: ruler.vertical ? ruler.handleB + 34 : Math.max(0, Math.min(ruler.width - width, mid - width / 2))
            y: ruler.vertical ? Math.max(0, Math.min(ruler.height - height, mid - height / 2)) : ruler.handleB + 34
            width: distanceText.implicitWidth + 24
            height: 32
            radius: 16
            color: Theme.accent
            Text {
                id: distanceText
                anchors.centerIn: parent
                text: ruler.m ? ruler.m.formatValue("length", ruler.lengthMm) : ""
                color: "white"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }
    }

    // ---------------------------------------------------------------- the circle gauge
    Item {
        id: field
        visible: ruler.mode === "circle"
        x: ruler.vertical ? strip.width + 8 : 0
        y: ruler.vertical ? 0 : strip.height + 8
        width: ruler.width - x
        height: ruler.height - y
        readonly property real r: ruler.ringDiameter * ruler.ppm / 2
        readonly property real cx: Math.max(r, Math.min(width - r, ruler.ringX * width))
        readonly property real cy: Math.max(r, Math.min(height - r, ruler.ringY * height))
        readonly property real maxDiameter: Math.max(4, Math.min(width, height) / ruler.ppm - 2)

        Rectangle {
            id: ring
            objectName: "rulerRing"
            x: field.cx - field.r
            y: field.cy - field.r
            width: 2 * field.r
            height: width
            radius: field.r
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.07)
            border.color: Theme.accent
            border.width: 2
            // Moving the ring: drag inside it.
            MouseArea {
                objectName: "rulerRingMove"
                anchors.fill: parent
                preventStealing: true
                property point start
                property real startX
                property real startY
                onPressed: (mouse) => {
                    start = mapToItem(field, mouse.x, mouse.y)
                    startX = field.cx
                    startY = field.cy
                }
                onPositionChanged: (mouse) => {
                    const p = mapToItem(field, mouse.x, mouse.y)
                    ruler.ringX = (startX + p.x - start.x) / field.width
                    ruler.ringY = (startY + p.y - start.y) / field.height
                }
            }
        }
        // The center.
        Rectangle { x: field.cx - 8; y: field.cy - 0.75; width: 16; height: 1.5; color: Theme.accent }
        Rectangle { x: field.cx - 0.75; y: field.cy - 8; width: 1.5; height: 16; color: Theme.accent }
        // A diameter line through it, and its value below the ring.
        Rectangle {
            x: field.cx - field.r
            y: field.cy - 0.5
            width: 2 * field.r
            height: 1
            color: Theme.accent
            opacity: 0.45
        }
        Rectangle {
            objectName: "rulerRingLabel"
            x: Math.max(0, Math.min(field.width - width, field.cx - width / 2))
            y: field.cy + field.r + 12 + height <= field.height ? field.cy + field.r + 12 : field.cy - field.r - 12 - height
            width: ringText.implicitWidth + 24
            height: 32
            radius: 16
            color: Theme.accent
            Text {
                id: ringText
                anchors.centerIn: parent
                text: ruler.m ? "Ø " + ruler.m.formatValue("length", ruler.ringDiameter) : ""
                color: "white"
                font.pixelSize: 15
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
            }
        }
        // The rim's handle (up and right of the center): drag to size the ring.
        Item {
            id: rimHandle
            objectName: "rulerRingHandle"
            width: 44
            height: 44
            x: field.cx + field.r * Math.SQRT1_2 - width / 2
            y: field.cy - field.r * Math.SQRT1_2 - height / 2
            Rectangle {
                anchors.centerIn: parent
                width: 26
                height: 26
                radius: 13
                color: Theme.card
                border.color: Theme.accent
                border.width: 2
                Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent }
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                onPositionChanged: (mouse) => {
                    const p = mapToItem(field, mouse.x, mouse.y)
                    const r = Math.hypot(p.x - field.cx, p.y - field.cy)
                    ruler.ringDiameter = Math.max(2, Math.min(field.maxDiameter, 2 * r / ruler.ppm))
                }
            }
        }
    }

    // ---------------------------------------------------------------- mode and scale
    MeasureCard {
        id: controls
        objectName: "rulerControls"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: Math.min(ruler.width - ruler.handleB - 40, 236)
        height: controlsColumn.implicitHeight + 24
        ColumnLayout {
            id: controlsColumn
            x: 12
            y: 12
            width: parent.width - 24
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: ruler.m && ruler.m.scaleCalibrated ? "#1A8F57" : "#E0A030"
                }
                Text {
                    objectName: "rulerScaleNote"
                    Layout.fillWidth: true
                    text: !ruler.m ? "" : ruler.m.scaleCalibrated ? "Scale checked with a card" : "Estimated scale"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: Theme.text
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: -6
                visible: ruler.m && !ruler.m.scaleCalibrated && text !== ""
                text: ruler.m ? ruler.m.scaleNote : ""
                font.pixelSize: 11
                color: Theme.mutedText
                elide: Text.ElideRight
            }
            MeasureSegmented {
                Layout.fillWidth: true
                namePrefix: "rulerMode_"
                showIcons: false
                model: [{ key: "length", label: "Length" }, { key: "circle", label: "Circle" }]
                current: ruler.mode
                onPicked: (key) => ruler.mode = key
            }
            ActionButton {
                objectName: "rulerCalibrate"
                Layout.fillWidth: true
                text: "Check with a card"
                iconName: "card"
                outlined: true
                onClicked: ruler.screen.openCalibration()
            }
        }
    }

    // A jaw: a line across the instrument at `mm`, dragged by its handle
    // or anywhere along it.
    component Jaw: Item {
        id: jaw
        property real mm: 0
        property real handleAt: 0
        signal moved(real mm)
        readonly property real pos: ruler.along(mm)
        anchors.fill: parent

        Rectangle {
            x: ruler.vertical ? 0 : jaw.pos - 1
            y: ruler.vertical ? jaw.pos - 1 : 0
            width: ruler.vertical ? ruler.width : 2
            height: ruler.vertical ? 2 : ruler.height
            color: Theme.accent
        }
        // The handle.
        Rectangle {
            objectName: jaw.objectName + "Handle"
            readonly property real size: 30
            x: ruler.vertical ? jaw.handleAt - size / 2 : jaw.pos - size / 2
            y: ruler.vertical ? jaw.pos - size / 2 : jaw.handleAt - size / 2
            width: size
            height: size
            radius: size / 2
            color: Theme.card
            border.color: Theme.accent
            border.width: 2
            Column {
                anchors.centerIn: parent
                spacing: 3
                rotation: ruler.vertical ? 0 : 90
                Repeater {
                    model: 2
                    Rectangle { width: 10; height: 1.5; radius: 0.75; color: Theme.accent }
                }
            }
        }
        // Where a finger takes it: the handle (44 pt) and a band along the line.
        MouseArea {
            objectName: jaw.objectName + "Drag"
            readonly property real band: 30
            x: ruler.vertical ? 0 : jaw.pos - band / 2
            y: ruler.vertical ? jaw.pos - band / 2 : 0
            width: ruler.vertical ? Math.max(jaw.handleAt + 22, ruler.width - controls.width - 12) : band
            height: ruler.vertical ? band : Math.max(jaw.handleAt + 22, ruler.height - controls.height - 12)
            preventStealing: true
            cursorShape: ruler.vertical ? Qt.SizeVerCursor : Qt.SizeHorCursor
            property real start
            property real startMm
            onPressed: (mouse) => {
                const p = mapToItem(ruler, mouse.x, mouse.y)
                start = ruler.vertical ? p.y : p.x
                startMm = jaw.mm
            }
            onPositionChanged: (mouse) => {
                const p = mapToItem(ruler, mouse.x, mouse.y)
                const delta = (ruler.vertical ? p.y : p.x) - start
                jaw.moved(Math.max(0, Math.min(ruler.maxMm, startMm + delta / ruler.ppm)))
            }
        }
        // The handle's own target, 44 pt, above the band.
        MouseArea {
            x: (ruler.vertical ? jaw.handleAt : jaw.pos) - 22
            y: (ruler.vertical ? jaw.pos : jaw.handleAt) - 22
            width: 44
            height: 44
            preventStealing: true
            property real start
            property real startMm
            onPressed: (mouse) => {
                const p = mapToItem(ruler, mouse.x, mouse.y)
                start = ruler.vertical ? p.y : p.x
                startMm = jaw.mm
            }
            onPositionChanged: (mouse) => {
                const p = mapToItem(ruler, mouse.x, mouse.y)
                const delta = (ruler.vertical ? p.y : p.x) - start
                jaw.moved(Math.max(0, Math.min(ruler.maxMm, startMm + delta / ruler.ppm)))
            }
        }
    }
}
