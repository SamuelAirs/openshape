// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import OpenShape

// The view cube (interact::ViewCube): a small cube that turns with the view,
// its faces named after the standard views and its edges in the colors of the
// axis lines through the origin (X red, Y green, Z blue). A tap on a face
// looks at it head-on, a tap on a corner looks down that corner's diagonal
// (an isometric view). A drag that starts on it orbits the view, also while a
// sketch tool or an arrow is armed: on a phone one finger draws and two pan
// and zoom, so this is the way to turn the model without leaving the tool.
// The drag never reaches the view's own gestures (the MouseArea keeps it).
Item {
    id: cube

    required property AppController app
    readonly property var colors: ["#D64545", "#3D9A4F", "#3F6FDC"]
    readonly property var layout: app.viewCube
    readonly property real half: width / 2
    // The design size of a face's label square; each face item is mapped onto
    // its face (ViewCubeFace: origin, across, down).
    readonly property real unit: 100
    // What the pointer is over (desktop) or pressing: a ViewCubeTarget id.
    property int hovered: -1
    property int pressedTarget: -1
    readonly property int lit: pressedTarget >= 0 ? pressedTarget : hovered

    // At least 44 px everywhere (Apple's minimum touch target); smaller in a
    // compact window (a phone). A corner may reach a few px past the square
    // (ViewCube::kViewHalfExtent), never onto the controls 8 px away.
    width: Theme.compact ? 80 : 92
    height: width

    function boxU(x) { return (x - half) / half }
    function boxV(y) { return (y - half) / half }

    Repeater {
        model: 6
        delegate: Item {
            id: faceItem
            required property int index
            readonly property var face: cube.layout.faces[index]
            readonly property real s: cube.half / cube.unit
            objectName: "viewCubeFace_" + face.name
            // A face turned away is hidden by the ones in front (a cube is
            // convex): only faces turned toward the viewer are drawn.
            visible: face.facing > 0.002
            width: cube.unit
            height: cube.unit
            transform: Matrix4x4 {
                matrix: Qt.matrix4x4(faceItem.face.ax * faceItem.s, faceItem.face.dx * faceItem.s, 0, cube.half + faceItem.face.ox * cube.half,
                                     faceItem.face.ay * faceItem.s, faceItem.face.dy * faceItem.s, 0, cube.half + faceItem.face.oy * cube.half,
                                     0, 0, 1, 0,
                                     0, 0, 0, 1)
            }

            Rectangle {
                anchors.fill: parent
                antialiasing: true
                // Faces turned further away are a shade darker: depth at a glance.
                color: cube.lit === faceItem.face.id ? Theme.checked
                     : Qt.tint(Theme.panel, Qt.rgba(0.30, 0.34, 0.42, 0.32 * (1 - faceItem.face.facing)))
            }
            // The corners: taps there look down the cube's diagonal.
            Repeater {
                model: 4
                delegate: Rectangle {
                    required property int index
                    readonly property int cornerId: faceItem.face.corners[index]
                    readonly property real side: cube.unit * 0.225
                    objectName: "viewCubeCorner_" + cornerId
                    x: index === 1 || index === 2 ? cube.unit - side : 0
                    y: index >= 2 ? cube.unit - side : 0
                    width: side
                    height: side
                    antialiasing: true
                    color: cube.lit === cornerId ? Qt.rgba(0.16, 0.47, 0.93, 0.45) : Qt.rgba(0.30, 0.34, 0.42, 0.08)
                }
            }
            Text {
                anchors.centerIn: parent
                text: faceItem.face.name
                opacity: faceItem.face.labelOpacity
                visible: opacity > 0
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
        }
    }

    // Edges in their axis' color (only those of faces turned toward the viewer).
    Repeater {
        model: 12
        delegate: Rectangle {
            id: edgeItem
            required property int index
            readonly property var edge: cube.layout.edges[index]
            readonly property real x1: cube.half + edge.x1 * cube.half
            readonly property real y1: cube.half + edge.y1 * cube.half
            readonly property real x2: cube.half + edge.x2 * cube.half
            readonly property real y2: cube.half + edge.y2 * cube.half
            visible: edge.visible
            x: x1
            y: y1 - height / 2
            width: Math.hypot(x2 - x1, y2 - y1)
            height: 1.5
            radius: 0.75
            antialiasing: true
            color: cube.colors[edge.axis]
            transformOrigin: Item.Left
            rotation: Math.atan2(y2 - y1, x2 - x1) * 180 / Math.PI
        }
    }

    // Taps and drags. Covers the whole square, so a drag that starts beside
    // the cube's corners orbits too and nothing falls through to the view.
    MouseArea {
        id: input
        objectName: "viewCubeInput"
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: !Theme.touch
        preventStealing: true
        property point start
        property point last
        property bool dragging: false
        // Like the view's own drag threshold (InputProfile): a finger wobbles.
        readonly property real threshold: Theme.touch ? 10 : 4

        onPressed: (mouse) => {
            start = Qt.point(mouse.x, mouse.y)
            last = start
            dragging = false
            cube.pressedTarget = mouse.button === Qt.LeftButton ? cube.app.viewCubeTarget(cube.boxU(mouse.x), cube.boxV(mouse.y)) : -1
        }
        onPositionChanged: (mouse) => {
            if (!pressed) {
                cube.hovered = cube.app.viewCubeTarget(cube.boxU(mouse.x), cube.boxV(mouse.y))
                return
            }
            if (!(pressedButtons & Qt.LeftButton))
                return
            if (!dragging && Math.hypot(mouse.x - start.x, mouse.y - start.y) < threshold)
                return
            if (!dragging) {
                dragging = true
                cube.pressedTarget = -1
                cube.hovered = -1
                last = start
            }
            cube.app.orbitView(mouse.x - last.x, mouse.y - last.y)
            last = Qt.point(mouse.x, mouse.y)
        }
        onReleased: (mouse) => {
            if (!dragging && mouse.button === Qt.LeftButton)
                cube.app.tapViewCube(cube.boxU(mouse.x), cube.boxV(mouse.y))
            dragging = false
            cube.pressedTarget = -1
        }
        onCanceled: {
            dragging = false
            cube.pressedTarget = -1
        }
        onExited: cube.hovered = -1
        onWheel: (wheel) => wheel.accepted = true
    }
}
