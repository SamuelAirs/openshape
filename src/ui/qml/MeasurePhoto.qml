// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Shapes
import OpenShape

// Photo (docs/MEASURE.md): measuring on a photo of a flat surface with a
// reference lying on it (a bank card, A4 or Letter paper, or a rectangle
// whose sides are typed). 1. Take or choose the photo (the desktop: open an
// image file). 2. Corners: four handles on the reference's corners (found
// by Vision where it can), with a magnifier while one is dragged. 3.
// Straighten: the photo as seen from straight above, in millimeters. 4. Tap
// points on it: Distance (two), Outline (around a shape, closed on the
// first point: each side and the area) or Circle (three on the rim).
// Make sketch puts an outline or a circle on the ground plane at real size.
Item {
    id: photo

    property MeasureController m
    property Item screen

    readonly property string stage: !m ? "start" : m.straightUrl.toString() !== "" ? "measure"
                                  : m.photoUrl.toString() !== "" ? "corners" : "start"
    // Measuring on the straightened photo: "distance", "outline", "circle".
    property string mode: "distance"
    // Tapped points, in the straightened photo's pixels (Qt.point).
    property var points: []
    property bool closed: false
    readonly property bool narrow: width < 560

    onModeChanged: clearPoints()
    onStageChanged: if (stage !== "measure") clearPoints()
    function clearPoints() {
        points = []
        closed = false
    }
    function undoPoint() {
        if (closed)
            closed = false
        else
            points = points.slice(0, Math.max(0, points.length - 1))
    }
    // A tap on the straightened photo (its pixels).
    function addPoint(p) {
        if (mode === "distance") {
            points = points.length >= 2 ? [p] : points.concat([p])
        } else if (mode === "circle") {
            points = points.length >= 3 ? [p] : points.concat([p])
        } else {
            if (closed) {
                points = [p]
                closed = false
            } else {
                points = points.concat([p])
            }
        }
    }
    function closeOutline() {
        if (mode === "outline" && points.length >= 3)
            closed = true
    }

    readonly property var outline: m && mode === "outline" && points.length >= 2 ? m.outlineReport(points, closed) : null
    readonly property var circle: m && mode === "circle" && points.length === 3 ? m.circleFrom3(points[0], points[1], points[2]) : null
    readonly property string surface: m ? "on the " + m.referenceName.toLowerCase() + "'s surface" : ""

    readonly property var reading: {
        const none = { valid: false, items: [], caption: stage === "measure" ? hintText : "", source: "Photo" }
        if (!m || stage !== "measure")
            return none
        if (mode === "distance" && points.length === 2)
            return { valid: true, caption: surface, source: "Photo",
                     items: [{ label: "Distance", quantity: "length", value: m.distanceMm(points[0], points[1]) }] }
        if (mode === "circle" && circle && circle.valid)
            return { valid: true, caption: surface, source: "Photo",
                     items: [{ label: "Diameter", quantity: "length", value: circle.diameter },
                             { label: "Radius", quantity: "length", value: circle.diameter / 2 }] }
        if (mode === "outline" && outline) {
            const items = closed ? [{ label: "Area", quantity: "area", value: outline.area },
                                    { label: "Perimeter", quantity: "length", value: outline.perimeter }]
                                 : [{ label: "Length", quantity: "length", value: outline.perimeter }]
            for (let i = 0; i < outline.sides.length; ++i)
                items.push({ label: "Side " + (i + 1), quantity: "length", value: outline.sides[i] })
            return { valid: true, caption: surface, source: "Photo", items: items }
        }
        return none
    }
    readonly property var actions: stage !== "measure" ? []
        : mode === "outline" && closed ? [{ key: "outlineSketch", label: "Make sketch", icon: "sketch" }]
        : mode === "circle" && circle && circle.valid ? [{ key: "circleSketch", label: "Make sketch", icon: "sketch_circle" }]
        : []
    function runAction(key) {
        let made = false
        if (key === "outlineSketch")
            made = m.makeOutlineSketch(m.straightToMm(points), true)
        else if (key === "circleSketch" && circle && circle.valid)
            made = m.makeCircleSketch(circle.diameter)
        if (made)
            screen.close()
        else
            screen.toast("The sketch couldn't be made", true)
    }

    readonly property string hintText: mode === "distance" ? (points.length < 2 ? "Tap the two ends of what to measure" : "Tap again to measure another")
        : mode === "circle" ? (points.length < 3 ? "Tap three points on the rim (" + points.length + " of 3)" : "Tap again for another circle")
        : closed ? "Closed. Tap to start another outline"
        : points.length < 3 ? "Tap the corners of the shape, one after another"
        : "Tap the first point (or Close) to close the outline"

    function openImage(button) {
        const prepared = screen.app.takeNextFileChoice()
        if (prepared.toString() !== "")
            m.loadPhoto(prepared)
        else
            imageDialog.open()
    }
    function anchorOf(item) {
        const p = item.mapToItem(null, 0, 0)
        return Qt.rect(p.x, p.y, item.width, item.height)
    }

    FileDialog {
        id: imageDialog
        title: "Open a photo to measure on"
        nameFilters: ["Images (*.jpg *.jpeg *.png *.heic *.bmp *.webp *.tif *.tiff)", "All files (*)"]
        onAccepted: photo.m.loadPhoto(selectedFile)
    }

    // ---------------------------------------------------------------- 1. a photo
    MeasureNotice {
        objectName: "photoStart"
        anchors.fill: parent
        visible: photo.stage === "start"
        iconName: "photo"
        title: "Measure on a photo"
        text: "Lay a bank card or a sheet of paper flat beside the thing to measure and photograph both, from any angle. FoldForm straightens the photo on the reference."
        ActionButton {
            id: takeButton
            objectName: "photoTake"
            Layout.fillWidth: true
            visible: photo.m && photo.m.cameraAvailable
            accent: true
            text: "Take photo"
            iconName: "camera"
            enabled: photo.m && !photo.m.photoBusy
            onClicked: photo.m.takePhoto(photo.anchorOf(takeButton))
        }
        ActionButton {
            id: chooseButton
            objectName: "photoChoose"
            Layout.fillWidth: true
            visible: photo.m && photo.m.canPickPhoto
            accent: !(photo.m && photo.m.cameraAvailable)
            outlined: photo.m && photo.m.cameraAvailable
            text: "Choose photo"
            iconName: "photo"
            enabled: photo.m && !photo.m.photoBusy
            onClicked: photo.m.choosePhoto(photo.anchorOf(chooseButton))
        }
        ActionButton {
            objectName: "photoOpen"
            Layout.fillWidth: true
            visible: photo.m && !photo.m.canPickPhoto
            accent: true
            text: "Open image…"
            iconName: "open"
            enabled: photo.m && !photo.m.photoBusy
            onClicked: photo.openImage()
        }
        Text {
            Layout.fillWidth: true
            visible: photo.m && photo.m.photoError !== "" && photo.stage === "start"
            text: photo.m ? photo.m.photoError : ""
            color: Theme.error
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // ---------------------------------------------------------------- 2. corners
    Item {
        id: cornersStage
        anchors.fill: parent
        visible: photo.stage === "corners"

        RowLayout {
            id: cornersTop
            width: parent.width
            spacing: 8
            MeasureSegmented {
                Layout.fillWidth: true
                Layout.maximumWidth: 480
                namePrefix: "photoRef_"
                showIcons: false
                model: [{ key: "card", label: "Card" }, { key: "a4", label: "A4" }, { key: "letter", label: "Letter" },
                        { key: "rectangle", label: photo.narrow ? "Other" : "Rectangle" }]
                current: photo.m ? photo.m.referenceKind : "card"
                onPicked: (key) => photo.m.setReference(key, parseFloat(sideA.text) || 0, parseFloat(sideB.text) || 0)
            }
            Item { Layout.fillWidth: true; visible: !photo.narrow }
            ActionButton {
                objectName: "photoNew"
                visible: !photo.narrow
                text: "New photo"
                outlined: true
                onClicked: photo.m.clearPhoto()
            }
        }
        RowLayout {
            id: sidesRow
            anchors.top: cornersTop.bottom
            anchors.topMargin: visible ? 8 : 0
            width: parent.width
            height: visible ? implicitHeight : 0
            visible: photo.m && photo.m.referenceKind === "rectangle"
            spacing: 8
            Text { text: "Sides"; font.pixelSize: 13; color: Theme.mutedText }
            SideField { id: sideA; objectName: "photoSideA"; placeholderText: "Long" }
            Text { text: "×"; font.pixelSize: 15; color: Theme.mutedText }
            SideField { id: sideB; objectName: "photoSideB"; placeholderText: "Short" }
            Text { text: "mm"; font.pixelSize: 13; color: Theme.mutedText }
            Item { Layout.fillWidth: true }
        }

        PhotoView {
            id: cornerView
            objectName: "photoCornerView"
            anchors { top: sidesRow.bottom; topMargin: 10; left: parent.left; right: parent.right; bottom: cornersBottom.top; bottomMargin: 10 }
            source: photo.stage === "corners" && photo.visible ? photo.m.photoUrl : ""
            sourceSize: photo.m ? photo.m.photoSize : Qt.size(1, 1)
            imageName: "photoImage"

            // The reference's outline through the four corners.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                visible: photo.m && photo.m.corners.length === 4
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 2
                    fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline {
                        path: {
                            const c = photo.m ? photo.m.corners : []
                            if (c.length !== 4)
                                return []
                            const pts = c.map(p => cornerView.toView(p))
                            return pts.concat([pts[0]])
                        }
                    }
                }
            }
            Repeater {
                model: 4
                delegate: Item {
                    id: handle
                    required property int index
                    readonly property point at: photo.m && photo.m.corners.length === 4 ? cornerView.toView(photo.m.corners[index]) : Qt.point(0, 0)
                    objectName: "photoCorner_" + index
                    x: at.x - width / 2
                    y: at.y - height / 2
                    width: 44
                    height: 44
                    Rectangle {
                        anchors.centerIn: parent
                        width: dragArea.pressed ? 30 : 24
                        height: width
                        radius: width / 2
                        color: Qt.rgba(1, 1, 1, 0.35)
                        border.color: "white"
                        border.width: 2.5
                        Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: Theme.accent }
                        Behavior on width { NumberAnimation { duration: 90 } }
                    }
                    MouseArea {
                        id: dragArea
                        anchors.fill: parent
                        preventStealing: true
                        property point grab
                        onPressed: (mouse) => {
                            grab = Qt.point(mouse.x - handle.width / 2, mouse.y - handle.height / 2)
                            loupe.index = handle.index
                        }
                        onPositionChanged: (mouse) => {
                            const p = mapToItem(cornerView, mouse.x - grab.x, mouse.y - grab.y)
                            const img = cornerView.toImage(p)
                            photo.m.setCorner(handle.index, img.x, img.y)
                        }
                        onReleased: loupe.index = -1
                        onCanceled: loupe.index = -1
                    }
                }
            }
            // The magnifier while a corner is dragged: above the finger
            // (below it near the top), the corner at its cross.
            Loupe {
                id: loupe
                objectName: "photoLoupe"
                property int index: -1
                view: cornerView
                target: index >= 0 && photo.m ? cornerView.toView(photo.m.corners[index]) : Qt.point(0, 0)
                visible: index >= 0
            }
        }

        RowLayout {
            id: cornersBottom
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: 8
            Text {
                objectName: "photoCornerHint"
                Layout.fillWidth: true
                text: photo.m && photo.m.photoError !== "" ? photo.m.photoError
                    : photo.m && photo.m.cornersDetected ? "Found the " + photo.m.referenceName.toLowerCase() + ". Check its corners, then straighten."
                    : "Drag the four handles onto the corners of the " + (photo.m ? photo.m.referenceName.toLowerCase() : "reference") + "."
                font.pixelSize: 13
                color: photo.m && photo.m.photoError !== "" ? Theme.error : Theme.mutedText
                wrapMode: Text.WordWrap
                maximumLineCount: 3
            }
            ActionButton {
                objectName: "photoNewNarrow"
                visible: photo.narrow
                text: "New"
                outlined: true
                onClicked: photo.m.clearPhoto()
            }
            ActionButton {
                objectName: "photoStraighten"
                text: "Straighten"
                iconName: "straighten"
                accent: true
                enabled: photo.m && !photo.m.photoBusy
                onClicked: photo.m.straighten()
            }
        }
    }

    // ---------------------------------------------------------------- 3. measuring
    Item {
        id: measureStage
        anchors.fill: parent
        visible: photo.stage === "measure"

        RowLayout {
            id: measureTop
            width: parent.width
            spacing: 8
            MeasureSegmented {
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                namePrefix: "photoMode_"
                model: [{ key: "distance", label: "Distance", icon: "distance" }, { key: "outline", label: "Outline", icon: "sketch_polygon" },
                        { key: "circle", label: "Circle", icon: "sketch_circle" }]
                current: photo.mode
                onPicked: (key) => photo.mode = key
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                objectName: "photoBack"
                text: photo.narrow ? "Corners" : "Back to corners"
                iconName: "chevronLeft"
                outlined: true
                onClicked: photo.m.unstraighten()
            }
        }

        PhotoView {
            id: straightView
            objectName: "photoStraightView"
            anchors { top: measureTop.bottom; topMargin: 10; left: parent.left; right: parent.right; bottom: measureBottom.top; bottomMargin: 10 }
            source: photo.stage === "measure" && photo.visible ? photo.m.straightUrl : ""
            sourceSize: photo.m ? photo.m.straightSize : Qt.size(1, 1)
            imageName: "photoStraightImage"
            checkered: true

            // The reference, faintly.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.9)
                    strokeWidth: 1.5
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [4, 3]
                    fillColor: "transparent"
                    PathPolyline {
                        path: {
                            const c = photo.m ? photo.m.straightReference : []
                            if (c.length !== 4)
                                return []
                            const pts = c.map(p => straightView.toView(p))
                            return pts.concat([pts[0]])
                        }
                    }
                }
            }
            // What was tapped: the line, the outline, the circle.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 2.5
                    fillColor: photo.mode === "outline" && photo.closed ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : "transparent"
                    joinStyle: ShapePath.RoundJoin
                    capStyle: ShapePath.RoundCap
                    PathPolyline {
                        path: {
                            if (photo.mode === "circle")
                                return []
                            const pts = photo.points.map(p => straightView.toView(p))
                            return photo.closed && pts.length > 0 ? pts.concat([pts[0]]) : pts
                        }
                    }
                }
            }
            Rectangle {
                objectName: "photoCircle"
                visible: photo.circle !== null && photo.circle.valid
                readonly property point c: visible ? straightView.toView(photo.circle.center) : Qt.point(0, 0)
                readonly property real r: visible ? photo.circle.radius * straightView.fit : 0
                x: c.x - r
                y: c.y - r
                width: 2 * r
                height: 2 * r
                radius: r
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                border.color: Theme.accent
                border.width: 2.5
            }
            // Side lengths (outline), the distance, the diameter.
            Repeater {
                model: {
                    if (!photo.m)
                        return []
                    const labels = []
                    const v = photo.points.map(p => straightView.toView(p))
                    if (photo.mode === "distance" && v.length === 2)
                        labels.push({ x: (v[0].x + v[1].x) / 2, y: (v[0].y + v[1].y) / 2,
                                      text: photo.m.formatValue("length", photo.m.distanceMm(photo.points[0], photo.points[1])) })
                    if (photo.mode === "outline" && photo.outline) {
                        const n = photo.outline.sides.length
                        for (let i = 0; i < n; ++i) {
                            const a = v[i], b = v[(i + 1) % v.length]
                            labels.push({ x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, text: photo.m.formatValue("length", photo.outline.sides[i]) })
                        }
                    }
                    if (photo.mode === "circle" && photo.circle && photo.circle.valid) {
                        const c = straightView.toView(photo.circle.center)
                        labels.push({ x: c.x, y: c.y, text: "Ø " + photo.m.formatValue("length", photo.circle.diameter) })
                    }
                    return labels
                }
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    objectName: "photoLabel_" + index
                    x: modelData.x - width / 2
                    y: modelData.y - height / 2
                    width: labelText.implicitWidth + 16
                    height: 24
                    radius: 12
                    color: Theme.toast
                    opacity: 0.92
                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        text: parent.modelData.text
                        color: "white"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                    }
                }
            }
            Repeater {
                model: photo.points
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property point at: straightView.toView(modelData)
                    readonly property bool first: index === 0 && photo.mode === "outline" && !photo.closed && photo.points.length >= 3
                    objectName: "photoPoint_" + index
                    x: at.x - width / 2
                    y: at.y - height / 2
                    width: first ? 20 : 14
                    height: width
                    radius: width / 2
                    color: first ? Theme.accent : "white"
                    border.color: Theme.accent
                    border.width: 2.5
                }
            }
            // Taps add points; a point can be dragged to adjust it (with the magnifier).
            MouseArea {
                objectName: "photoTapArea"
                anchors.fill: parent
                preventStealing: true
                property int dragging: -1
                property point pressAt
                property bool moved: false
                function nearest(p, within) {
                    let best = -1, bestD = within
                    for (let i = 0; i < photo.points.length; ++i) {
                        const v = straightView.toView(photo.points[i])
                        const d = Math.hypot(v.x - p.x, v.y - p.y)
                        if (d <= bestD) {
                            best = i
                            bestD = d
                        }
                    }
                    return best
                }
                onPressed: (mouse) => {
                    pressAt = Qt.point(mouse.x, mouse.y)
                    moved = false
                    const near = nearest(pressAt, 22)
                    // The first point of an open outline closes it (a tap); others move.
                    dragging = near === 0 && photo.mode === "outline" && !photo.closed && photo.points.length >= 3 ? -1 : near
                }
                onPositionChanged: (mouse) => {
                    if (Math.hypot(mouse.x - pressAt.x, mouse.y - pressAt.y) > 8)
                        moved = true
                    if (dragging < 0 || !moved)
                        return
                    const pts = photo.points.slice()
                    pts[dragging] = straightView.toImage(Qt.point(mouse.x, mouse.y))
                    photo.points = pts
                    pointLoupe.target = Qt.point(mouse.x, mouse.y)
                    pointLoupe.visible = true
                }
                onReleased: (mouse) => {
                    pointLoupe.visible = false
                    if (dragging >= 0 && moved) {
                        dragging = -1
                        return
                    }
                    dragging = -1
                    if (moved || !straightView.inside(Qt.point(mouse.x, mouse.y)))
                        return
                    if (photo.mode === "outline" && !photo.closed && photo.points.length >= 3 && nearest(pressAt, 22) === 0)
                        photo.closeOutline()
                    else
                        photo.addPoint(straightView.toImage(Qt.point(mouse.x, mouse.y)))
                }
                onCanceled: {
                    pointLoupe.visible = false
                    dragging = -1
                }
            }
            Loupe {
                id: pointLoupe
                view: straightView
                visible: false
            }
        }

        RowLayout {
            id: measureBottom
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: 8
            Text {
                objectName: "photoMeasureHint"
                Layout.fillWidth: true
                text: photo.hintText
                font.pixelSize: 13
                color: Theme.mutedText
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }
            ActionButton {
                objectName: "photoClose"
                visible: photo.mode === "outline" && !photo.closed && photo.points.length >= 3
                text: "Close"
                outlined: true
                onClicked: photo.closeOutline()
            }
            ActionButton {
                objectName: "photoUndo"
                text: "Undo"
                iconName: "undo"
                iconOnly: photo.narrow
                outlined: true
                enabled: photo.points.length > 0
                onClicked: photo.undoPoint()
            }
            ActionButton {
                objectName: "photoClear"
                text: "Clear"
                iconName: "clear"
                iconOnly: photo.narrow
                outlined: true
                enabled: photo.points.length > 0
                onClicked: photo.clearPoints()
            }
        }
    }

    // ---------------------------------------------------------------- working
    Rectangle {
        anchors.fill: parent
        visible: photo.m && photo.m.photoBusy
        color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.7)
        MouseArea { anchors.fill: parent }
        MeasureCard {
            anchors.centerIn: parent
            width: busyRow.implicitWidth + 40
            height: 64
            Row {
                id: busyRow
                anchors.centerIn: parent
                spacing: 12
                BusyIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    running: parent.visible
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: photo.stage === "corners" ? "Straightening…" : "Reading the photo…"
                    font.pixelSize: 15
                    color: Theme.text
                }
            }
        }
    }

    // A photo fitted into the room, with what is drawn over it in view
    // coordinates (toView / toImage convert from and to the photo's pixels).
    component PhotoView: Item {
        id: pv
        property url source
        property size sourceSize
        property string imageName
        property bool checkered: false
        default property alias overlays: overlayLayer.data
        readonly property real fit: sourceSize.width > 0 && sourceSize.height > 0
            ? Math.min(width / sourceSize.width, height / sourceSize.height) : 1
        readonly property real imageX: (width - sourceSize.width * fit) / 2
        readonly property real imageY: (height - sourceSize.height * fit) / 2
        property alias image: img
        function toView(p) { return Qt.point(imageX + p.x * fit, imageY + p.y * fit) }
        function toImage(p) {
            return Qt.point(Math.max(0, Math.min(sourceSize.width, (p.x - imageX) / fit)),
                            Math.max(0, Math.min(sourceSize.height, (p.y - imageY) / fit)))
        }
        function inside(p) {
            return p.x >= imageX && p.y >= imageY && p.x <= imageX + sourceSize.width * fit && p.y <= imageY + sourceSize.height * fit
        }
        clip: true
        Rectangle {
            x: pv.imageX
            y: pv.imageY
            width: pv.sourceSize.width * pv.fit
            height: pv.sourceSize.height * pv.fit
            radius: 6
            color: pv.checkered ? "#DDE1E6" : "transparent"
        }
        Image {
            id: img
            objectName: pv.imageName
            x: pv.imageX
            y: pv.imageY
            width: pv.sourceSize.width * pv.fit
            height: pv.sourceSize.height * pv.fit
            source: pv.source
            // At most what the screen shows (the file stays on disk).
            sourceSize.width: Math.min(2048, Math.ceil(width * 3))
            asynchronous: true
            cache: false
            smooth: true
            mipmap: true
        }
        Item {
            id: overlayLayer
            anchors.fill: parent
        }
    }

    // A round magnifier of a PhotoView's image around `target` (view
    // coordinates), floating above it, with a cross at the point.
    component Loupe: Item {
        id: lp
        property Item view
        property point target
        readonly property real zoom: 3
        readonly property real size: 116
        width: size
        height: size
        z: 10
        x: Math.max(0, Math.min(view.width - size, target.x - size / 2))
        y: target.y - size - 36 >= 0 ? target.y - size - 36 : Math.min(view.height - size, target.y + 36)
        ShaderEffectSource {
            id: loupeSource
            anchors.fill: parent
            visible: false
            sourceItem: lp.visible ? lp.view.image : null
            readonly property real span: lp.size / lp.zoom
            sourceRect: Qt.rect(lp.target.x - lp.view.imageX - span / 2, lp.target.y - lp.view.imageY - span / 2, span, span)
            live: true
            smooth: true
        }
        MultiEffect {
            anchors.fill: parent
            source: loupeSource
            maskEnabled: true
            maskSource: loupeMask
        }
        Item {
            id: loupeMask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Rectangle { anchors.fill: parent; radius: width / 2; color: "black" }
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: "white"
            border.width: 3
        }
        Rectangle { anchors.centerIn: parent; width: 22; height: 1.5; color: Theme.accent }
        Rectangle { anchors.centerIn: parent; width: 1.5; height: 22; color: Theme.accent }
    }

    component SideField: TextField {
        Layout.preferredWidth: 84
        implicitHeight: Theme.controlHeight
        font.pixelSize: 15
        font.features: ({ "tnum": 1 })
        color: Theme.text
        horizontalAlignment: Text.AlignRight
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        validator: DoubleValidator { bottom: 0; top: 10000; decimals: 2; notation: DoubleValidator.StandardNotation }
        selectByMouse: true
        onTextEdited: photo.m.setReference("rectangle", parseFloat(sideA.text) || 0, parseFloat(sideB.text) || 0)
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: parent.activeFocus ? Theme.accent : Theme.panelBorder
            border.width: parent.activeFocus ? 1.5 : 1
        }
    }
}
