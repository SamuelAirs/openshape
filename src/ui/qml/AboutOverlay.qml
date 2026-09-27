// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Version, license, where the source is and what is bundled (MPL-2.0
// section 3.2 asks binaries to tell people where the source is).
Rectangle {
    id: overlay
    color: "#66000000"
    visible: false
    focus: visible

    // The source of exactly this build and what identifies it
    // (AppController.sourceCodeUrl / buildDescription).
    property string sourceUrl: "https://github.com/SamuelAirs/openshape"
    property string buildText: ""

    // Licenses: the license texts and the LGPL notice (LicensesOverlay).
    signal licensesRequested()

    function open() { visible = true }

    Keys.onEscapePressed: visible = false
    MouseArea {
        anchors.fill: parent
        onClicked: overlay.visible = false
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
        anchors.centerIn: safeRect
        opacity: 1
        width: Math.min(safeRect.width - (Theme.compact ? 16 : 48), 560)
        height: Math.min(safeRect.height - (Theme.compact ? 16 : 48), content.implicitHeight + 2 * (Theme.compact ? 16 : 24))

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.compact ? 16 : 24
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: content
                width: parent.width
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "aboutVersion"
                        Layout.fillWidth: true
                        text: "OpenShape " + Qt.application.version
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Theme.text
                        wrapMode: Text.WordWrap
                    }
                    ActionButton { text: "Close"; onClicked: overlay.visible = false }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 13
                    text: "Direct, precise solid CAD for makers and 3D printing."
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 13
                    textFormat: Text.StyledText
                    linkColor: Theme.accent
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    text: "Free software under the <a href=\"https://mozilla.org/MPL/2.0/\">Mozilla Public License 2.0</a>. "
                        + "Source code: <a href=\"https://github.com/SamuelAirs/openshape\">github.com/SamuelAirs/openshape</a>"
                        + (overlay.sourceUrl !== "https://github.com/SamuelAirs/openshape"
                           ? " (<a href=\"" + overlay.sourceUrl + "\">this version</a>)" : "") + "."
                }
                Text {
                    objectName: "aboutBuild"
                    visible: overlay.buildText !== ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 12
                    text: "This copy: " + overlay.buildText
                }
                SectionLabel { text: "Built with"; Layout.leftMargin: 0 }
                Repeater {
                    model: [
                        ["Open CASCADE Technology", "LGPL-2.1 with the OCCT exception", "exact solid geometry"],
                        ["Qt 6", "LGPL-3.0", "user interface and drawing"],
                        ["PlaneGCS (from FreeCAD)", "LGPL-2.1", "sketch constraints"],
                        ["Eigen", "MPL-2.0", "linear algebra"],
                        ["libzip", "BSD-3-Clause", "project files"],
                        ["nlohmann/json", "MIT", "project files"],
                        ["Noto Sans (font)", "SIL Open Font License 1.1", "letters for raised and cut-in text"]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            text: modelData[0]
                            color: Theme.text
                            font.pixelSize: 13
                            Layout.preferredWidth: Theme.compact ? 130 : 190
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            text: modelData[1] + " · " + modelData[2]
                            color: Theme.mutedText
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Text {
                    objectName: "aboutLicenseFiles"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.mutedText
                    font.pixelSize: 12
                    textFormat: Text.StyledText
                    linkColor: Theme.accent
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                    // Licenses (every platform) shows the texts built into the
                    // app; the Windows package also carries them as files, with
                    // the libraries only it bundles (scripts/package-windows.sh).
                    text: "These libraries and the ones they use keep their own licenses: Licenses shows each license text, "
                        + "where each library's source code is and how to rebuild OpenShape with modified versions of the LGPL libraries."
                        + (Qt.platform.os === "windows"
                           ? " The Windows package also has them as files next to OpenShape.exe (LICENSE.txt, "
                             + "THIRD_PARTY_LICENSES.txt with every bundled library, PlaneGCS-COPYING.LIB.txt and NotoSans-OFL.txt)."
                           : "")
                }
                ActionButton {
                    objectName: "aboutLicenses"
                    text: "Licenses"
                    onClicked: overlay.licensesRequested()
                }
            }
        }
    }
}
