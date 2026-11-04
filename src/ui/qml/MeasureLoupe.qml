// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Effects
import OpenShape

// A round magnifier of a PhotoView's image around `target` (view
// coordinates), floating above it, with a cross at the point.
Item {
    id: lp
    property MeasurePhotoView view
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
