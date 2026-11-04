// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Shapes
import OpenShape

// Scan (docs/SCAN.md): photos around an object standing on a sheet of A4 or
// Letter paper become a mesh and a body. 1. A short how-to and the photos
// (the camera again and again, the library several at once, files on the
// desktop). 2. Each photo's paper corners (Vision or FoldForm's own finder),
// a strip to review them; a photo whose corners look wrong is fixed with the
// corner handles and the magnifier, or removed. 3. Build (a worker thread:
// progress, Cancel). 4. The result: its size, volume, what the engine
// warns about, a turning preview; Add to design makes it a body.
Item {
    id: scan

    property MeasureController m
    property Item screen

    readonly property var photos: m ? m.scanPhotos : []
    readonly property var result: m ? m.scanResult : ({})
    readonly property bool hasResult: result && result.valid === true
    readonly property string stage: !m ? "intro" : m.scanBuilding ? "building" : hasResult ? "result"
                                  : photos.length > 0 ? "photos" : "intro"
    // The photo being reviewed (its corners), or -1.
    property int reviewIndex: -1
    readonly property bool narrow: width < 560
    onStageChanged: reviewIndex = -1

    readonly property var reading: !hasResult ? { valid: false, items: [], caption: "", source: "Scan" }
        : { valid: true, caption: "scanned", source: "Scan",
            items: [{ label: "Width", quantity: "length", value: result.width },
                    { label: "Depth", quantity: "length", value: result.depth },
                    { label: "Height", quantity: "length", value: result.height }] }
    readonly property var actions: hasResult ? [{ key: "addToDesign", label: "Add to design", icon: "box" }] : []
    function runAction(key) {
        if (key === "addToDesign") {
            if (m.scanAddToDesign())
                screen.close()
            else
                screen.toast("The scan couldn't become a body", true)
        }
    }

    function anchorOf(item) {
        const p = item.mapToItem(null, 0, 0)
        return Qt.rect(p.x, p.y, item.width, item.height)
    }
    function openFiles() {
        const prepared = m.takeNextScanFiles()
        if (prepared.length > 0)
            m.scanAddFiles(prepared)
        else
            filesDialog.open()
    }
    function volumeText(mm3) {
        if (!m)
            return ""
        if (m.unit === "in")
            return (mm3 / 16387.064).toFixed(2) + " in³"
        return (mm3 / 1000).toFixed(1) + " cm³"
    }

    FileDialog {
        id: filesDialog
        title: "Photos of the object, all around"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Images (*.jpg *.jpeg *.png *.heic *.bmp *.webp *.tif *.tiff)", "All files (*)"]
        onAccepted: scan.m.scanAddFiles(selectedFiles)
    }

    // ---------------------------------------------------------------- how-to
    MeasureNotice {
        objectName: "scanIntro"
        anchors.fill: parent
        visible: scan.stage === "intro"
        iconName: "scan"
        title: "Scan an object"
        text: "Stand it on a sheet of " + (scan.m && scan.m.scanPaper === "letter" ? "Letter" : "A4")
              + " paper on a plain table. Walk around it and take 8 to 12 photos from about 45° above, "
              + "each with all four corners of the paper, in even light."
        MeasureSegmented {
            Layout.fillWidth: true
            namePrefix: "scanPaper_"
            showIcons: false
            model: [{ key: "a4", label: "A4 paper" }, { key: "letter", label: "Letter paper" }]
            current: scan.m ? scan.m.scanPaper : "a4"
            onPicked: (key) => scan.m.setScanPaper(key)
        }
        ActionButton {
            id: introTake
            objectName: "scanTake"
            Layout.fillWidth: true
            visible: scan.m && scan.m.cameraAvailable
            accent: true
            text: "Take photos"
            iconName: "camera"
            onClicked: scan.m.scanTakePhoto(scan.anchorOf(introTake))
        }
        ActionButton {
            id: introChoose
            objectName: "scanChoose"
            Layout.fillWidth: true
            visible: scan.m && scan.m.canPickPhoto
            accent: !(scan.m && scan.m.cameraAvailable)
            outlined: scan.m && scan.m.cameraAvailable
            text: "Choose photos"
            iconName: "photo"
            onClicked: scan.m.scanChoosePhotos(scan.anchorOf(introChoose))
        }
        ActionButton {
            objectName: "scanOpen"
            Layout.fillWidth: true
            visible: scan.m && !scan.m.canPickPhoto
            accent: true
            text: "Open photos…"
            iconName: "open"
            onClicked: scan.openFiles()
        }
        Text {
            Layout.fillWidth: true
            visible: scan.m && scan.m.scanError !== ""
            text: scan.m ? scan.m.scanError : ""
            color: Theme.error
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // ---------------------------------------------------------------- the photos
    Item {
        id: photosStage
        anchors.fill: parent
        visible: scan.stage === "photos" && scan.reviewIndex < 0

        RowLayout {
            id: photosTop
            width: parent.width
            spacing: 10
            MeasureSegmented {
                Layout.preferredWidth: Math.min(260, parent.width * 0.55)
                namePrefix: "scanPaper_"
                showIcons: false
                model: [{ key: "a4", label: "A4" }, { key: "letter", label: "Letter" }]
                current: scan.m ? scan.m.scanPaper : "a4"
                onPicked: (key) => scan.m.setScanPaper(key)
            }
            Item { Layout.fillWidth: true }
            Text {
                objectName: "scanCount"
                text: scan.photos.length + (scan.photos.length === 1 ? " photo" : " photos")
                font.pixelSize: 15
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
                color: Theme.text
            }
        }

        GridView {
            id: grid
            objectName: "scanGrid"
            anchors { top: photosTop.bottom; topMargin: 12; left: parent.left; right: parent.right; bottom: photosBottom.top; bottomMargin: 12 }
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(2, Math.floor(width / (scan.narrow ? 112 : 150)))
            cellWidth: width / columns
            cellHeight: cellWidth * 0.78
            model: scan.photos
            delegate: AbstractButton {
                id: thumb
                required property var modelData
                required property int index
                objectName: "scanThumb_" + index
                padding: 0
                width: grid.cellWidth
                height: grid.cellHeight
                focusPolicy: Qt.NoFocus
                onClicked: scan.reviewIndex = index
                readonly property real fitK: Math.min((width - 8) / modelData.width, (height - 8) / modelData.height)
                readonly property real ox: (width - modelData.width * fitK) / 2
                readonly property real oy: (height - modelData.height * fitK) / 2
                readonly property bool trouble: !modelData.found || (scan.hasResult && !modelData.used)
                background: Item {}
                contentItem: Item {
                    Rectangle {
                        x: thumb.ox - 1
                        y: thumb.oy - 1
                        width: thumb.modelData.width * thumb.fitK + 2
                        height: thumb.modelData.height * thumb.fitK + 2
                        radius: 9
                        color: Theme.card
                        border.color: thumb.trouble ? "#E0A030" : Theme.cardBorder
                        border.width: thumb.trouble ? 2 : 1
                    }
                    Image {
                        x: thumb.ox
                        y: thumb.oy
                        width: thumb.modelData.width * thumb.fitK
                        height: thumb.modelData.height * thumb.fitK
                        source: scan.visible ? thumb.modelData.url : ""
                        sourceSize.width: 320
                        asynchronous: true
                        cache: false
                        opacity: thumb.pressed ? 0.8 : 1
                    }
                    // The paper as found.
                    Shape {
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: thumb.modelData.found ? Theme.accent : "#E0A030"
                            strokeWidth: 1.5
                            fillColor: "transparent"
                            joinStyle: ShapePath.RoundJoin
                            PathPolyline {
                                path: {
                                    const c = thumb.modelData.corners
                                    const pts = c.map(p => Qt.point(thumb.ox + p.x * thumb.fitK, thumb.oy + p.y * thumb.fitK))
                                    return pts.length ? pts.concat([pts[0]]) : []
                                }
                            }
                        }
                    }
                    Rectangle {
                        objectName: "scanThumbBadge_" + thumb.index
                        anchors { right: parent.right; top: parent.top; margins: 8 }
                        width: 22
                        height: 22
                        radius: 11
                        color: thumb.trouble ? "#E0A030" : thumb.modelData.source === "hand" ? Theme.accent : "#1A8F57"
                        border.color: "white"
                        border.width: 1.5
                        LineIcon {
                            anchors.centerIn: parent
                            name: thumb.trouble ? "about" : thumb.modelData.source === "hand" ? "rename" : "check"
                            size: 13
                            color: "white"
                        }
                    }
                }
            }
        }

        ColumnLayout {
            id: photosBottom
            anchors.bottom: parent.bottom
            width: parent.width
            spacing: 8
            // Detail from the photos: multi-view stereo, a few seconds more.
            AbstractButton {
                id: stereoSwitch
                objectName: "scanStereo"
                Layout.fillWidth: true
                implicitHeight: Theme.touch ? 44 : 34
                visible: scan.m && scan.m.scanStereoAvailable
                checkable: true
                checked: scan.m ? scan.m.scanDetailFromStereo : true
                focusPolicy: Qt.NoFocus
                onToggled: scan.m.scanDetailFromStereo = checked
                background: Item {}
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 40
                        height: 24
                        radius: 12
                        color: stereoSwitch.checked ? Theme.accent : Theme.track
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Rectangle {
                            x: stereoSwitch.checked ? parent.width - width - 3 : 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18
                            height: 18
                            radius: 9
                            color: "white"
                            Behavior on x { NumberAnimation { duration: 120 } }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: "Detail from the photos (stereo)" + (scan.narrow ? "" : ": a few seconds more")
                        font.pixelSize: 13
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                }
            }
            Text {
                objectName: "scanHint"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: scan.m && scan.m.scanError !== "" ? Theme.error : Theme.mutedText
                text: {
                    if (!scan.m)
                        return ""
                    if (scan.m.scanError !== "")
                        return scan.m.scanError
                    if (scan.m.scanBusy)
                        return "Reading the photos…"
                    const missing = scan.photos.filter(p => !p.found).length
                    if (missing > 0)
                        return missing + (missing === 1 ? " photo needs" : " photos need") + " its paper's corners: tap it to place them."
                    if (scan.photos.length < 8)
                        return "Add more from all around (8 to 12 work best). Tap a photo to check its corners."
                    return "Ready to build. Tap a photo to check its corners."
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    id: takeMore
                    objectName: "scanTakeAnother"
                    visible: scan.m && scan.m.cameraAvailable
                    text: scan.narrow ? "Take" : "Take another"
                    iconName: "camera"
                    outlined: true
                    enabled: scan.photos.length < 16
                    onClicked: scan.m.scanTakePhoto(scan.anchorOf(takeMore))
                }
                ActionButton {
                    id: chooseMore
                    objectName: "scanChooseMore"
                    visible: scan.m && scan.m.canPickPhoto
                    text: scan.narrow ? "Add" : "Choose more"
                    iconName: "photo"
                    outlined: true
                    enabled: scan.photos.length < 16
                    onClicked: scan.m.scanChoosePhotos(scan.anchorOf(chooseMore))
                }
                ActionButton {
                    objectName: "scanOpenMore"
                    visible: scan.m && !scan.m.canPickPhoto
                    text: "Add photos…"
                    iconName: "open"
                    outlined: true
                    enabled: scan.photos.length < 16
                    onClicked: scan.openFiles()
                }
                ActionButton {
                    objectName: "scanNew"
                    text: scan.narrow ? "" : "Start over"
                    iconName: "clear"
                    iconOnly: scan.narrow
                    outlined: true
                    onClicked: scan.m.scanClear()
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "scanBuild"
                    text: "Build"
                    iconName: "scan"
                    accent: true
                    enabled: scan.m && scan.photos.length >= 3 && !scan.m.scanBusy
                    onClicked: scan.m.scanBuild()
                }
            }
        }
    }

    // ---------------------------------------------------------------- one photo's corners
    Item {
        id: review
        anchors.fill: parent
        visible: scan.stage === "photos" && scan.reviewIndex >= 0
        readonly property var entry: scan.reviewIndex >= 0 && scan.reviewIndex < scan.photos.length ? scan.photos[scan.reviewIndex] : null

        RowLayout {
            id: reviewTop
            width: parent.width
            spacing: 8
            ActionButton {
                objectName: "scanReviewDone"
                text: "Done"
                iconName: "chevronLeft"
                outlined: true
                onClicked: scan.reviewIndex = -1
            }
            Text {
                Layout.fillWidth: true
                text: "Photo " + (scan.reviewIndex + 1) + " of " + scan.photos.length
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
            }
            ActionButton {
                objectName: "scanRemove"
                text: scan.narrow ? "" : "Remove"
                iconName: "delete"
                iconOnly: scan.narrow
                outlined: true
                onClicked: {
                    const i = scan.reviewIndex
                    scan.reviewIndex = -1
                    scan.m.scanRemovePhoto(i)
                }
            }
        }
        MeasurePhotoView {
            id: reviewView
            objectName: "scanReviewView"
            anchors { top: reviewTop.bottom; topMargin: 10; left: parent.left; right: parent.right; bottom: reviewHint.top; bottomMargin: 8 }
            source: review.visible && review.entry ? review.entry.url : ""
            sourceSize: review.entry ? Qt.size(review.entry.width, review.entry.height) : Qt.size(1, 1)
            imageName: "scanReviewImage"
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 2
                    fillColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline {
                        path: {
                            const c = review.entry ? review.entry.corners : []
                            if (c.length !== 4)
                                return []
                            const pts = c.map(p => reviewView.toView(p))
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
                    readonly property point at: review.entry ? reviewView.toView(review.entry.corners[index]) : Qt.point(0, 0)
                    objectName: "scanCorner_" + index
                    x: at.x - width / 2
                    y: at.y - height / 2
                    width: 44
                    height: 44
                    Rectangle {
                        anchors.centerIn: parent
                        width: cornerDrag.pressed ? 30 : 24
                        height: width
                        radius: width / 2
                        color: Qt.rgba(1, 1, 1, 0.35)
                        border.color: "white"
                        border.width: 2.5
                        Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: Theme.accent }
                    }
                    MouseArea {
                        id: cornerDrag
                        anchors.fill: parent
                        preventStealing: true
                        property point grab
                        onPressed: (mouse) => {
                            grab = Qt.point(mouse.x - handle.width / 2, mouse.y - handle.height / 2)
                            reviewLoupe.index = handle.index
                        }
                        onPositionChanged: (mouse) => {
                            const p = mapToItem(reviewView, mouse.x - grab.x, mouse.y - grab.y)
                            const img = reviewView.toImage(p)
                            scan.m.scanSetCorner(scan.reviewIndex, handle.index, img.x, img.y)
                        }
                        onReleased: reviewLoupe.index = -1
                        onCanceled: reviewLoupe.index = -1
                    }
                }
            }
            MeasureLoupe {
                id: reviewLoupe
                objectName: "scanLoupe"
                property int index: -1
                view: reviewView
                target: index >= 0 && review.entry ? reviewView.toView(review.entry.corners[index]) : Qt.point(0, 0)
                visible: index >= 0
            }
        }
        Text {
            id: reviewHint
            anchors.bottom: parent.bottom
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            color: review.entry && review.entry.problem !== "" ? "#B7791F" : Theme.mutedText
            text: !review.entry ? ""
                : review.entry.problem !== "" ? review.entry.problem
                : !review.entry.found ? "The paper wasn't found here: drag the handles onto its four corners."
                : review.entry.source === "hand" ? "Corners placed by hand."
                : "Found the paper. Drag a handle if a corner is off; two fingers zoom."
        }
    }

    // ---------------------------------------------------------------- building
    MeasureCard {
        objectName: "scanBuilding"
        visible: scan.stage === "building"
        anchors.centerIn: parent
        width: Math.min(parent.width - 8, 420)
        height: buildColumn.implicitHeight + 48
        ColumnLayout {
            id: buildColumn
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Building the scan"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Text {
                objectName: "scanStageText"
                Layout.fillWidth: true
                text: scan.m ? scan.m.scanStage : ""
                font.pixelSize: 14
                color: Theme.mutedText
                elide: Text.ElideRight
            }
            Rectangle {
                objectName: "scanProgressBar"
                Layout.fillWidth: true
                height: 8
                radius: 4
                color: Theme.track
                Rectangle {
                    width: parent.width * (scan.m ? scan.m.scanProgress : 0)
                    height: parent.height
                    radius: 4
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: Math.round((scan.m ? scan.m.scanProgress : 0) * 100) + " %"
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.mutedText
                }
                ActionButton {
                    objectName: "scanCancel"
                    text: "Cancel"
                    outlined: true
                    onClicked: scan.m.scanCancel()
                }
            }
        }
    }

    // ---------------------------------------------------------------- the result
    Flickable {
        id: resultStage
        anchors.fill: parent
        visible: scan.stage === "result"
        readonly property bool wide: width >= 560 && width > height * 1.15
        // A short window scrolls the result rather than cut it.
        contentWidth: width
        contentHeight: wide ? height : previewCard.height + 12 + infoColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        MeasureCard {
            id: previewCard
            objectName: "scanPreviewCard"
            x: 0
            y: 0
            width: resultStage.wide ? resultStage.width * 0.55 : resultStage.width
            height: resultStage.wide ? resultStage.height : Math.max(140, resultStage.height - infoColumn.implicitHeight - 12)
            clip: true
            ScanPreview {
                id: preview
                objectName: "scanPreview"
                anchors.fill: parent
                anchors.margins: 12
                controller: scan.m
                color: Theme.accent
                NumberAnimation on angle {
                    running: scan.visible && scan.stage === "result" && scan.screen && scan.screen.active
                    from: 0
                    to: 360
                    duration: 14000
                    loops: Animation.Infinite
                }
            }
            Text {
                anchors { left: parent.left; bottom: parent.bottom; margins: 12 }
                text: scan.hasResult ? Number(scan.result.triangles).toLocaleString(Qt.locale(), "f", 0) + " triangles" : ""
                font.pixelSize: 12
                font.features: ({ "tnum": 1 })
                color: Theme.shortcutText
            }
        }
        ColumnLayout {
            id: infoColumn
            x: resultStage.wide ? previewCard.width + 16 : 0
            y: resultStage.wide ? 0 : previewCard.height + 12
            width: resultStage.wide ? resultStage.width - previewCard.width - 16 : resultStage.width
            spacing: 8
            Text {
                objectName: "scanSize"
                Layout.fillWidth: true
                Layout.maximumWidth: infoColumn.width
                text: !scan.hasResult || !scan.m ? ""
                    : scan.m.formatNumber("length", scan.result.width) + " × " + scan.m.formatNumber("length", scan.result.depth)
                      + " × " + scan.m.formatNumber("length", scan.result.height) + " " + scan.m.unitSymbol("length")
                font.pixelSize: 20
                font.weight: Font.DemiBold
                font.features: ({ "tnum": 1 })
                color: Theme.text
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "scanFacts"
                Layout.fillWidth: true
                Layout.maximumWidth: infoColumn.width
                text: !scan.hasResult ? ""
                    : "Volume " + scan.volumeText(scan.result.volume) + " · " + scan.result.used + " of " + scan.result.photos + " photos used"
                      + (scan.result.stereo ? " · detail from stereo" : "")
                font.pixelSize: 14
                font.features: ({ "tnum": 1 })
                color: Theme.mutedText
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: scan.hasResult ? scan.result.warnings : []
                delegate: RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 8
                    LineIcon {
                        Layout.alignment: Qt.AlignTop
                        name: "about"
                        size: 16
                        color: "#B7791F"
                    }
                    Text {
                        objectName: "scanWarning_" + parent.index
                        Layout.fillWidth: true
                        Layout.maximumWidth: infoColumn.width - 24
                        text: parent.modelData
                        font.pixelSize: 13
                        color: Theme.text
                        wrapMode: Text.WordWrap
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                ActionButton {
                    objectName: "scanBack"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: "Photos"
                    iconName: "chevronLeft"
                    outlined: true
                    onClicked: scan.m.scanDiscardResult()
                }
                ActionButton {
                    objectName: "scanStartOver"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: "New scan"
                    iconName: "clear"
                    outlined: true
                    onClicked: scan.m.scanClear()
                }
            }
        }
    }
}
