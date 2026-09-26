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

    function open() { visible = true }

    Keys.onEscapePressed: visible = false
    MouseArea {
        anchors.fill: parent
        onClicked: overlay.visible = false
        onWheel: (wheel) => wheel.accepted = true
    }

    Panel {
        anchors.centerIn: parent
        opacity: 1
        width: Math.min(parent.width - 48, 560)
        height: Math.min(parent.height - 48, content.implicitHeight + 48)

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
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
                        text: "OpenShape " + Qt.application.version
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Item { Layout.fillWidth: true }
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
                        + "Source code: <a href=\"https://github.com/SamuelAirs/openshape\">github.com/SamuelAirs/openshape</a>."
                }
                SectionLabel { text: "Built with"; Layout.leftMargin: 0 }
                Repeater {
                    model: [
                        ["Open CASCADE Technology", "LGPL-2.1 with the OCCT exception", "exact solid geometry"],
                        ["Qt 6", "LGPL-3.0", "user interface and drawing"],
                        ["PlaneGCS (from FreeCAD)", "LGPL-2.1", "sketch constraints"],
                        ["Eigen", "MPL-2.0", "linear algebra"],
                        ["libzip", "BSD-3-Clause", "project files"],
                        ["nlohmann/json", "MIT", "project files"]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            text: modelData[0]
                            color: Theme.text
                            font.pixelSize: 13
                            Layout.preferredWidth: 190
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
                    // Only the Windows package (scripts/package-windows.sh) carries
                    // the license files; other builds point to the repository.
                    text: "These libraries and the ones they use (such as FreeType, HarfBuzz, ICU and zlib) keep their own licenses. "
                        + (Qt.platform.os === "windows"
                           ? "The full license texts and where to get each library's source code come with OpenShape "
                             + "(LICENSE.txt, THIRD_PARTY_LICENSES.txt and PlaneGCS-COPYING.LIB.txt next to OpenShape.exe)."
                           : "Each library's license and source are listed in "
                             + "<a href=\"https://github.com/SamuelAirs/openshape/blob/main/THIRD_PARTY.md\">THIRD_PARTY.md</a> "
                             + "in the source code repository.")
                }
            }
        }
    }
}
