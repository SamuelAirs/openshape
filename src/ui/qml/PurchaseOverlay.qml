// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// The App Store purchase "Unlimited projects" (iPhone / iPad): opened by an
// action that would add a second project to the free version (New, Import
// STEP, Duplicate, saving under a new name, a file from another app), by
// Home's "Free version" line and by About. After a purchase or restore the
// sheet closes and the action that opened it goes ahead
// (AppController::gateProject). The price is the App Store's, as it comes.
Rectangle {
    id: overlay

    required property AppController app
    readonly property string status: app.purchaseStatus
    // Buttons one above the other, full width: a phone, a Split View half,
    // the iPhone Duo's outer display.
    readonly property bool stacked: Theme.compact || width < 600

    color: "#66000000"
    visible: app.purchaseVisible
    focus: visible

    Keys.onEscapePressed: app.closePurchase()
    // Taps outside the card do nothing (Not now closes it).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // The card stays clear of a phone's Dynamic Island, rounded corners and
    // home indicator (Theme.safe*; zero on the desktop).
    Item {
        id: safeRect
        anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                  bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
    }

    Panel {
        id: card
        objectName: "purchasePanel"
        readonly property int pad: Theme.compact ? 16 : 24
        anchors.centerIn: safeRect
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 48), 460)
        height: Math.min(safeRect.height - (Theme.compact ? 16 : 48), content.implicitHeight + 2 * pad)

        // Scrolls only when the window is too short for the card (a phone
        // held sideways).
        Flickable {
            objectName: "purchaseScroll"
            anchors.fill: parent
            anchors.margins: card.pad
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 12

                Text {
                    objectName: "purchaseTitle"
                    Layout.fillWidth: true
                    text: "Unlimited projects"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: Theme.text
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "purchaseText"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                    color: Theme.text
                    text: "The free version of FoldForm has every tool, for one project. A one-time purchase unlocks "
                        + "as many projects as you like, on your iPhone and iPad with this Apple Account."
                }
                // How the purchase is going, or why it cannot.
                Text {
                    objectName: "purchaseStatusText"
                    readonly property bool problem: overlay.status === "failed" || overlay.status === "unavailable"
                    Layout.fillWidth: true
                    visible: text !== ""
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText // the store's messages are shown as they are
                    font.pixelSize: 13
                    color: problem ? Theme.error : Theme.mutedText
                    text: overlay.app.purchaseMessage !== "" ? overlay.app.purchaseMessage
                        : overlay.status === "loading" ? "Getting the price from the App Store…"
                        : overlay.status === "buying" ? "Buying… Follow the App Store's steps."
                        : overlay.status === "waiting" ? "Waiting for approval (Ask to Buy). FoldForm unlocks by itself once the purchase is approved."
                        : overlay.status === "restoring" ? "Restoring your purchase…"
                        : overlay.status === "unavailable" ? "The App Store can't be reached right now. Check the connection and try again later."
                        : ""
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    columns: overlay.stacked ? 1 : 3
                    rowSpacing: 8
                    columnSpacing: 8
                    ActionButton {
                        objectName: "purchaseUnlock"
                        Layout.fillWidth: overlay.stacked
                        accent: true
                        text: overlay.app.purchasePrice !== "" ? "Unlock for " + overlay.app.purchasePrice : "Unlock"
                        enabled: overlay.app.purchaseCanBuy
                        onClicked: overlay.app.buyUnlock()
                    }
                    ActionButton {
                        objectName: "purchaseRestore"
                        Layout.fillWidth: overlay.stacked
                        outlined: true
                        text: "Restore purchase"
                        enabled: overlay.status !== "buying" && overlay.status !== "restoring"
                        onClicked: overlay.app.restorePurchase()
                    }
                    ActionButton {
                        objectName: "purchaseNotNow"
                        Layout.fillWidth: overlay.stacked
                        outlined: overlay.stacked
                        text: "Not now"
                        onClicked: overlay.app.closePurchase()
                    }
                }
            }
        }
    }
}
