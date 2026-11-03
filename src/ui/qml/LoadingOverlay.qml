// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Effects
import OpenShape

// While a project opens (AppController::loading: it is read and rebuilt on a
// worker thread, the window keeps drawing): a card with its preview, its name
// and a moving bar, so a big model never looks frozen (Sam on build 58). It
// takes every touch, click and key at once; the card itself fades in only
// after a moment, so a quick open does not flash it.
Item {
    id: overlay

    required property AppController app
    readonly property bool active: app.loading
    property bool shown: false

    visible: active || scrim.opacity > 0
    Keys.onPressed: (event) => event.accepted = true

    onActiveChanged: {
        if (active) {
            appearDelay.restart()
        } else {
            appearDelay.stop()
            shown = false
        }
    }
    // Keys come here once the card shows (not at once: a menu that started
    // the open may still be closing, and taking focus then upsets it).
    Timer {
        id: appearDelay
        interval: 160
        onTriggered: {
            overlay.shown = true
            overlay.forceActiveFocus()
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: overlay.active
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: (wheel) => wheel.accepted = true
    }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Theme.scrim
        opacity: overlay.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }

    // The card stays clear of the status bar, the side bar and the home
    // indicator (Theme.safe*; zero on the desktop).
    Item {
        id: safeRect
        anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                  bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
    }

    RectangularShadow {
        anchors.fill: card
        radius: card.radius
        offset.y: 12
        blur: 36
        spread: -4
        color: Theme.shadow
        opacity: card.opacity
        scale: card.scale
    }

    Rectangle {
        id: card
        objectName: "loadingCard"
        anchors.centerIn: safeRect
        width: Math.min(safeRect.width - 32, 300)
        height: content.implicitHeight + 2 * padding
        readonly property int padding: 18
        radius: Theme.cardRadius
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1
        opacity: overlay.shown ? 1 : 0
        scale: overlay.shown ? 1 : 0.96
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

        Column {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: card.padding }
            spacing: 0

            Rectangle {
                id: preview
                width: parent.width
                height: thumbnail.status === Image.Ready ? Math.round(width * 0.6) : 0
                visible: height > 0
                radius: 12
                color: Theme.previewWell
                Image {
                    id: thumbnail
                    anchors { fill: parent; margins: 8 }
                    source: overlay.app.loadingThumbnail
                    sourceSize: Qt.size(256, 256)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    mipmap: true
                }
            }
            Item { width: 1; height: preview.visible ? 16 : 2 }
            Text {
                objectName: "loadingLabel"
                width: parent.width
                text: overlay.app.loadingLabel.toUpperCase()
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1.1
                color: Theme.mutedText
            }
            Item { width: 1; height: 4 }
            Text {
                objectName: "loadingName"
                width: parent.width
                text: overlay.app.loadingName
                textFormat: Text.PlainText // file names are shown as they are
                font.pixelSize: 17
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideMiddle
            }
            Item { width: 1; height: 14 }
            // Indeterminate: a short bar sweeps the track while it works.
            Rectangle {
                id: track
                width: parent.width
                height: 4
                radius: 2
                color: Theme.track
                clip: true
                Rectangle {
                    id: sweep
                    width: track.width * 0.32
                    height: track.height
                    radius: 2
                    color: Theme.accent
                    x: -width
                    SequentialAnimation on x {
                        running: overlay.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: -sweep.width; to: track.width; duration: 1150; easing.type: Easing.InOutCubic }
                        PauseAnimation { duration: 120 }
                    }
                }
            }
        }
    }
}
