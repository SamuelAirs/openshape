// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import OpenShape

// A photo fitted into the room, with what is drawn over it in view
// coordinates (toView / toImage convert from and to the photo's
// pixels). Two fingers zoom (pinch, up to 8x) and pan it; the wheel or a
// trackpad zooms at the pointer; Fit goes back.
Item {
    id: pv
    property url source
    property size sourceSize
    property string imageName
    property bool checkered: false
    default property alias overlays: overlayLayer.data
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    readonly property real baseFit: sourceSize.width > 0 && sourceSize.height > 0
        ? Math.min(width / sourceSize.width, height / sourceSize.height) : 1
    readonly property real fit: baseFit * zoom
    readonly property real imageX: (width - sourceSize.width * fit) / 2 + panX
    readonly property real imageY: (height - sourceSize.height * fit) / 2 + panY
    property alias image: img
    function toView(p) { return Qt.point(imageX + p.x * fit, imageY + p.y * fit) }
    function toImage(p) {
        return Qt.point(Math.max(0, Math.min(sourceSize.width, (p.x - imageX) / fit)),
                        Math.max(0, Math.min(sourceSize.height, (p.y - imageY) / fit)))
    }
    function rawImage(p) { return Qt.point((p.x - imageX) / fit, (p.y - imageY) / fit) }
    function inside(p) {
        return p.x >= imageX && p.y >= imageY && p.x <= imageX + sourceSize.width * fit && p.y <= imageY + sourceSize.height * fit
    }
    // Zooms to `z` with the photo's point `imagePoint` under `anchor` (view coordinates).
    function zoomAt(z, anchor, imagePoint) {
        zoom = Math.max(1, Math.min(8, z))
        const f = baseFit * zoom
        const mx = Math.max(0, (sourceSize.width * f - width) / 2)
        const my = Math.max(0, (sourceSize.height * f - height) / 2)
        panX = Math.max(-mx, Math.min(mx, anchor.x - imagePoint.x * f - (width - sourceSize.width * f) / 2))
        panY = Math.max(-my, Math.min(my, anchor.y - imagePoint.y * f - (height - sourceSize.height * f) / 2))
    }
    function resetView() {
        zoom = 1
        panX = 0
        panY = 0
    }
    onSourceChanged: resetView()
    onWidthChanged: if (zoom === 1) resetView()
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
        // Read once at a size that stays sharp when zoomed (the file stays on disk).
        sourceSize.width: Math.min(2048, Math.max(1, pv.sourceSize.width))
        asynchronous: true
        cache: false
        smooth: true
        mipmap: true
    }
    Item {
        id: overlayLayer
        anchors.fill: parent
    }
    // Above the taps and handles: one finger passes through, two zoom and pan.
    Item {
        objectName: pv.imageName + "Gestures"
        anchors.fill: parent
        z: 20
        PinchHandler {
            id: pinch
            target: null
            minimumPointCount: 2
            maximumPointCount: 2
            property real startZoom: 1
            property point startImage
            onActiveChanged: {
                if (active) {
                    startZoom = pv.zoom
                    startImage = pv.rawImage(centroid.position)
                }
            }
            onActiveScaleChanged: if (active) pv.zoomAt(startZoom * activeScale, centroid.position, startImage)
            onCentroidChanged: if (active) pv.zoomAt(startZoom * activeScale, centroid.position, startImage)
        }
        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const at = point.position
                pv.zoomAt(pv.zoom * Math.pow(2, event.angleDelta.y / 480), at, pv.rawImage(at))
            }
        }
    }
    // Back to the whole photo.
    AbstractButton {
        objectName: pv.imageName + "Fit"
        visible: pv.zoom > 1.01
        z: 21
        anchors { right: parent.right; top: parent.top; margins: 8 }
        implicitWidth: fitRow.implicitWidth + 24
        implicitHeight: 36
        focusPolicy: Qt.NoFocus
        onClicked: pv.resetView()
        background: Rectangle { radius: 18; color: Theme.toast; opacity: parent.pressed ? 0.8 : 0.92 }
        contentItem: Item {
            Row {
                id: fitRow
                anchors.centerIn: parent
                spacing: 6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Fit · " + pv.zoom.toFixed(1) + "×"
                    color: "white"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.features: ({ "tnum": 1 })
                }
            }
        }
    }
}
