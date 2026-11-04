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
// reference lying on it (a playing card, A4 or Letter paper, or a rectangle
// whose sides are typed). 1. Take or choose the photo (the desktop: open an
// image file). 2. Corners: four handles on the reference's corners (found
// by Vision where it can), with a magnifier while one is dragged. 3.
// Straighten: the photo as seen from straight above, in millimeters, with
// millimeter rulers along its top and left and a grid to switch on. 4. Tap
// points on it: Distance (two; its angle shown, snapped straight near 0 or
// 90 degrees), Outline (around a shape, closed on the first point: each
// side and the area) or Circle (three on the rim). Every one stays on the
// photo as a dimension (A, B, C...), listed under it; Keep all keeps them.
// Make sketch puts an outline or a circle on the ground plane at real size.
// Find outline (in Outline) finds a part lying on contrasting paper and
// makes it an outline dimension of lines, arcs and holes.
// A set of views ("Views": a top, a front and a side photo of one part):
// + View keeps the photo measured and starts the next; Place views puts
// each on its plane in the design, its origin point at the plane's origin.
Item {
    id: photo

    property MeasureController m
    property Item screen

    readonly property string stage: !m ? "start" : m.straightUrl.toString() !== "" ? "measure"
                                  : m.photoUrl.toString() !== "" ? "corners" : "start"
    // Measuring on the straightened photo: "distance", "outline", "circle".
    property string mode: "distance"
    // The selected dimension's points (the one being drawn, or picked), in
    // the straightened photo's pixels (Qt.point), and whether its outline is
    // closed. Writing them changes that dimension (or starts one of `mode`).
    property var points: []
    property bool closed: false
    // Every dimension on this photo: [{kind, points, closed, letter, name,
    // found}], and which one `points` is (-1: none). `found` (Find outline,
    // MeasureController.foundOutline) marks an outline that was found: which
    // of its points are an arc's middle (through) and its holes; dragging
    // keeps it, a change in the number of points (Undo) drops it.
    property var dims: []
    property int active: -1
    property int letterCount: 0
    // The point put at the design's origin (straightened px), or null; the
    // next tap sets it while `settingOrigin`.
    property var origin: null
    property bool settingOrigin: false
    property bool showGrid: false
    // A distance near 0 or 90 degrees (to the reference's edges) is made
    // straight while this is on.
    property bool snap: true
    readonly property real snapDegrees: 1.5
    // The view's name and plane ("XY" the ground, "XZ" Front, "YZ" Right).
    property string viewName: "Top"
    property string viewPlane: "XY"
    // The second end of a distance while it is placed (snapped), or null.
    property var placeAt: null
    property bool placeSnapped: false
    property bool loading: false
    readonly property bool narrow: width < 560

    onModeChanged: {
        if (loading)
            return
        if (activeDim && !isComplete(activeDim))
            removeDim(active)
        else
            deselect()
        placeAt = null
    }
    onStageChanged: {
        if (stage !== "measure" && !loading) {
            clearPoints()
            origin = null
            settingOrigin = false
        }
        if (screen)
            screen.closeKeypad()
    }
    onPointsChanged: syncActive()
    onClosedChanged: syncActive()

    // ---- the dimensions
    readonly property var activeDim: active >= 0 && active < dims.length ? dims[active] : null
    readonly property var found: activeDim && activeDim.found ? activeDim.found : null
    // Find outline's result: the selected found outline takes it (Find
    // outline again), else it is a new closed outline dimension.
    function setFoundOutline(f) {
        if (!(activeDim && activeDim.found))
            setOutlinePoints(f.points)
        const list = dims.slice()
        const d = Object.assign({}, list[active])
        d.points = f.points.slice()
        d.closed = true
        d.found = f
        list[active] = d
        loading = true
        points = d.points
        closed = true
        dims = list
        loading = false
    }
    function isComplete(d) {
        return d.kind === "distance" ? d.points.length === 2 : d.kind === "circle" ? d.points.length === 3
                                     : d.closed && d.points.length >= 3
    }
    readonly property int completeCount: {
        let n = 0
        for (let i = 0; i < dims.length; ++i)
            if (isComplete(dims[i]))
                ++n
        return n
    }
    function letterFor(n) {
        const a = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        return n < 26 ? a[n] : a[Math.floor(n / 26) - 1] + a[n % 26]
    }
    function displayName(d) {
        return d.name !== undefined && d.name !== "" ? d.name : d.letter
    }
    // `points` and `closed` written: the selected dimension follows (a new
    // one of `mode` when none is selected; none left when emptied).
    function syncActive() {
        if (loading)
            return
        const list = dims.slice()
        if (active < 0) {
            if (points.length === 0)
                return
            list.push({ kind: mode, points: points, closed: closed, letter: letterFor(letterCount), name: "" })
            loading = true
            letterCount = letterCount + 1
            dims = list
            active = list.length - 1
            loading = false
            return
        }
        if (points.length === 0) {
            list.splice(active, 1)
            loading = true
            active = -1
            closed = false
            dims = list
            if (list.length === 0)
                letterCount = 0
            loading = false
            return
        }
        const d = Object.assign({}, list[active])
        d.points = points
        d.closed = closed
        if (d.found && (!closed || d.found.through.length !== points.length))
            delete d.found
        list[active] = d
        dims = list
    }
    function deselect() {
        loading = true
        active = -1
        points = []
        closed = false
        loading = false
    }
    function select(i) {
        if (i < 0 || i >= dims.length)
            return
        if (activeDim && !isComplete(activeDim) && active !== i) {
            const dropped = active
            removeDim(dropped)
            if (i > dropped)
                --i
        }
        loading = true
        active = i
        mode = dims[i].kind
        points = dims[i].points
        closed = dims[i].closed
        loading = false
        placeAt = null
    }
    function removeDim(i) {
        if (i < 0 || i >= dims.length)
            return
        const list = dims.slice()
        list.splice(i, 1)
        loading = true
        if (active === i) {
            active = -1
            points = []
            closed = false
        } else if (active > i) {
            active = active - 1
        }
        dims = list
        if (list.length === 0)
            letterCount = 0
        loading = false
    }
    function renameDim(i, name) {
        if (i < 0 || i >= dims.length)
            return
        const list = dims.slice()
        const d = Object.assign({}, list[i])
        d.name = name.trim().slice(0, 24)
        list[i] = d
        dims = list
    }
    // Every dimension goes (Clear).
    function clearPoints() {
        loading = true
        dims = []
        active = -1
        points = []
        closed = false
        letterCount = 0
        placeAt = null
        loading = false
    }
    function undoPoint() {
        if (active < 0 && dims.length > 0)
            select(dims.length - 1)
        if (active < 0)
            return
        if (closed)
            closed = false
        else
            points = points.slice(0, Math.max(0, points.length - 1))
    }
    // A tap on the straightened photo (its pixels).
    function addPoint(p) {
        if (settingOrigin) {
            origin = Qt.point(p.x, p.y)
            settingOrigin = false
            return
        }
        if (activeDim && (isComplete(activeDim) || activeDim.kind !== mode))
            deselect()
        if (mode === "distance" && points.length === 1)
            p = snapEnd(points[0], p).p
        // The same point twice (a tap on the last one) adds nothing.
        const last = points.length > 0 ? points[points.length - 1] : null
        if (last && Math.hypot(p.x - last.x, p.y - last.y) < 1e-6)
            return
        points = points.concat([p])
    }
    function closeOutline() {
        if (mode === "outline" && points.length >= 3)
            closed = true
    }
    // An outline found elsewhere (straightened-image px): a new closed one.
    function setOutlinePoints(pts) {
        if (mode !== "outline")
            mode = "outline"
        deselect()
        points = pts.slice()
        closed = pts.length >= 3
    }
    // A dimension's point moved (a drag): a distance's end snaps straight
    // to its other end as when it was placed.
    function movePoint(dimIndex, index, p) {
        if (dimIndex !== active)
            select(dimIndex)
        const pts = points.slice()
        if (mode === "distance" && pts.length === 2)
            p = snapEnd(pts[1 - index], p).p
        pts[index] = p
        points = pts
    }

    // ---- straightness: a distance's angle to the reference's edges
    // Degrees from the straightened view's horizontal (the reference's
    // long edge), 0 (across) to 90 (up and down), whichever way it slopes.
    function angleOf(a, b) {
        const dx = Math.abs(b.x - a.x), dy = Math.abs(b.y - a.y)
        return dx < 1e-12 && dy < 1e-12 ? 0 : Math.atan2(dy, dx) * 180 / Math.PI
    }
    function offStraight(deg) {
        return Math.min(deg, 90 - deg)
    }
    function isStraight(a, b) {
        return offStraight(angleOf(a, b)) < 0.05
    }
    // The second end `b` of a line from `a`, made straight within snapDegrees.
    function snapEnd(a, b) {
        if (!snap || Math.hypot(b.x - a.x, b.y - a.y) < 1e-9)
            return { p: b, snapped: false }
        const deg = angleOf(a, b)
        if (deg <= snapDegrees)
            return { p: Qt.point(b.x, a.y), snapped: true }
        if (Math.abs(90 - deg) <= snapDegrees)
            return { p: Qt.point(a.x, b.y), snapped: true }
        return { p: b, snapped: false }
    }
    function updatePreview(p) {
        if (mode === "distance" && points.length === 1 && !(activeDim && isComplete(activeDim))) {
            const s = snapEnd(points[0], p)
            placeAt = s.p
            placeSnapped = s.snapped
        } else {
            placeAt = p
            placeSnapped = false
        }
    }
    readonly property bool placingDistance: placeAt !== null && mode === "distance" && points.length === 1
    readonly property real placeAngle: placingDistance ? angleOf(points[0], placeAt) : 0

    // ---- a dimension's values
    function isStraight0(deg) {
        return offStraight(deg) < 0.05
    }
    // Its main value {quantity, value} (what its label and Keep all show).
    function mainValue(d) {
        if (!m)
            return null
        if (d.kind === "distance" && d.points.length === 2)
            return { quantity: "length", value: m.distanceMm(d.points[0], d.points[1]), prefix: "" }
        if (d.kind === "circle" && d.points.length === 3) {
            const c = m.circleFrom3(d.points[0], d.points[1], d.points[2])
            return c.valid ? { quantity: "length", value: c.diameter, prefix: "Ø " } : null
        }
        if (d.kind === "outline" && d.points.length >= 2) {
            const r = d.found ? m.profileReport(d.points, d.found) : m.outlineReport(d.points, d.closed)
            return d.closed ? { quantity: "area", value: r.area, prefix: "" } : { quantity: "length", value: r.perimeter, prefix: "" }
        }
        return null
    }
    function valueText(d) {
        const v = mainValue(d)
        return v ? v.prefix + m.formatValue(v.quantity, v.value) : ""
    }

    readonly property var outline: m && mode === "outline" && points.length >= 2
                                   ? (found ? m.profileReport(points, found) : m.outlineReport(points, closed)) : null
    readonly property var circle: m && mode === "circle" && points.length === 3 ? m.circleFrom3(points[0], points[1], points[2]) : null
    readonly property string surface: m ? "on the " + m.referenceName.toLowerCase() + "'s surface" : ""
    readonly property string activeName: activeDim ? displayName(activeDim) + " · " : ""

    readonly property var reading: {
        const none = { valid: false, items: [], caption: stage === "measure" ? hintText : "", source: "Photo" }
        // Setting the origin: the readout says how.
        if (!m || stage !== "measure" || settingOrigin)
            return none
        if (mode === "distance" && points.length === 2)
            return { valid: true, caption: activeName + surface, source: "Photo",
                     items: [{ label: "Distance", quantity: "length", value: m.distanceMm(points[0], points[1]) },
                             { label: "Angle", quantity: "angle", value: angleOf(points[0], points[1]) }] }
        if (mode === "circle" && circle && circle.valid)
            return { valid: true, caption: activeName + surface, source: "Photo",
                     items: [{ label: "Diameter", quantity: "length", value: circle.diameter },
                             { label: "Radius", quantity: "length", value: circle.diameter / 2 }] }
        if (mode === "outline" && outline && outline.items)
            return { valid: true, caption: activeName + surface, source: "Photo", items: outline.items }
        if (mode === "outline" && outline) {
            const items = closed ? [{ label: "Area", quantity: "area", value: outline.area },
                                    { label: "Perimeter", quantity: "length", value: outline.perimeter }]
                                 : [{ label: "Length", quantity: "length", value: outline.perimeter }]
            for (let i = 0; i < outline.sides.length; ++i)
                items.push({ label: "Side " + (i + 1), quantity: "length", value: outline.sides[i] })
            return { valid: true, caption: activeName + surface, source: "Photo", items: items }
        }
        return none
    }
    // The design can take the straightened photo as a reference image
    // (AppController::addReferenceImage, where it is there).
    readonly property bool canPlace: screen && screen.app && typeof screen.app.addReferenceImage === "function"
    // Views in the set, the one measured counted once.
    readonly property int setSize: !m ? 0 : m.photoViews.length + (m.photoViewIndex < 0 && stage === "measure" ? 1 : 0)
    readonly property var actions: {
        if (stage !== "measure")
            return []
        const list = []
        if (mode === "outline" && closed)
            list.push({ key: "outlineSketch", label: "Make sketch", icon: "sketch" })
        if (mode === "circle" && circle && circle.valid)
            list.push({ key: "circleSketch", label: "Make sketch", icon: "sketch_circle" })
        if (canPlace && m.photoViews.length > 0)
            list.push({ key: "placeViews", label: "Place " + setSize + " views", icon: "photo" })
        else if (canPlace)
            list.push({ key: "placeImage", label: "Place in design", icon: "photo" })
        return list
    }
    function runAction(key) {
        if (key === "placeViews") {
            saveCurrent()
            placeSheet.visible = true
            return
        }
        if (key === "placeImage") {
            const file = m.keepStraightImage()
            // The origin point (when one was tapped) at the plane's origin.
            const s = m.straightSize, mm = m.straightMmPerPixel
            const cx = origin ? (s.width / 2 - origin.x) * mm : 0
            const cy = origin ? (origin.y - s.height / 2) * mm : 0
            if (file !== "" && screen.app.addReferenceImage(file, mm, viewPlane, cx, cy))
                screen.close()
            else
                screen.toast("The photo couldn't be placed in the design", true)
            return
        }
        let made = false
        if (key === "outlineSketch")
            made = found ? m.makeProfileSketch(points, found) : m.makeOutlineSketch(m.straightToMm(points), true)
        else if (key === "circleSketch" && circle && circle.valid)
            made = m.makeCircleSketch(circle.diameter)
        if (made)
            screen.close()
        else
            screen.toast("The sketch couldn't be made", true)
    }
    // Every finished dimension as a kept measurement, named after the view
    // and its letter ("Top A").
    function keepAll() {
        const names = []
        for (let i = 0; i < dims.length; ++i) {
            const d = dims[i]
            const v = isComplete(d) ? mainValue(d) : null
            if (!v)
                continue
            const name = viewName + " " + displayName(d)
            if (m.addMeasurement(v.quantity, v.value, "Photo", name) !== "")
                names.push(name)
        }
        if (names.length > 0)
            screen.toast(names.length === 1 ? "Kept " + names[0] : "Kept " + names.length + ": " + names.join(", "))
        else
            screen.toast("Nothing finished to keep", true)
    }

    readonly property string edgesName: m ? "the " + m.referenceName.toLowerCase() + "'s edges" : "the edges"
    readonly property string hintText: settingOrigin ? "Tap the point to put at the design's origin, e.g. the part's bottom-left corner"
        : mode === "distance" ? (points.length === 0 || points.length >= 2 ? (points.length === 2 ? "Tap again to measure another" : "Tap the two ends of what to measure")
                                 : placingDistance ? (placeSnapped ? "Straight: snapped to " + edgesName + (snap ? " (Snap off for a slanted line)" : "")
                                                                   : placeAngle.toFixed(1) + "° to " + edgesName)
                                 : snap ? "Tap the other end. Near 0° or 90° it snaps straight" : "Tap the other end")
        : mode === "circle" ? (points.length < 3 ? "Tap three points on the rim (" + points.length + " of 3)" : "Tap again for another circle")
        : found ? "Drag a point to adjust the found outline"
        : closed ? "Closed. Tap to start another outline"
        : points.length === 0 ? "Find outline, or tap the shape's corners one after another"
        : points.length < 3 ? "Tap the corners of the shape, one after another"
        : "Tap the first point (or Close) to close the outline"

    // ---- the set of views
    readonly property var presetViews: [{ key: "Top", plane: "XY" }, { key: "Front", plane: "XZ" }, { key: "Side", plane: "YZ" }]
    function planeFor(name) {
        for (const v of presetViews)
            if (v.key === name)
                return v.plane
        return viewPlane
    }
    function planeLabel(plane) {
        return plane === "XZ" ? "the Front plane" : plane === "YZ" ? "the Right plane" : "the ground"
    }
    // The next view's name: the first of Top, Front, Side not in the set, else "View N".
    function nextViewName() {
        const taken = m.photoViews.map(v => v.name)
        for (const v of presetViews)
            if (taken.indexOf(v.key) < 0)
                return v.key
        return "View " + (m.photoViews.length + 1)
    }
    function stateOf() {
        const kept = []
        for (let i = 0; i < dims.length; ++i) {
            const d = dims[i]
            if (isComplete(d))
                kept.push(Object.assign({ kind: d.kind, points: d.points.map(p => Qt.point(p.x, p.y)), closed: d.closed, letter: d.letter, name: d.name },
                                        d.found ? { found: d.found } : {}))
        }
        return { dims: kept, letterCount: letterCount, origin: origin ? Qt.point(origin.x, origin.y) : null }
    }
    function restoreState(s) {
        loading = true
        const list = []
        const saved = s && s.dims ? s.dims : []
        for (let i = 0; i < saved.length; ++i) {
            const d = saved[i]
            const pts = []
            for (let k = 0; k < d.points.length; ++k)
                pts.push(Qt.point(d.points[k].x, d.points[k].y))
            const item = { kind: d.kind, points: pts, closed: d.closed === true, letter: d.letter, name: d.name || "" }
            if (d.found && d.found.through && d.found.through.length === pts.length)
                item.found = d.found
            list.push(item)
        }
        dims = list
        active = -1
        points = []
        closed = false
        letterCount = s && s.letterCount ? s.letterCount : list.length
        origin = s && s.origin ? Qt.point(s.origin.x, s.origin.y) : null
        settingOrigin = false
        placeAt = null
        loading = false
    }
    // The photo measured into the set (or its entry brought up to date).
    function saveCurrent() {
        if (stage !== "measure")
            return -1
        return m.savePhotoView(viewName, viewPlane, stateOf())
    }
    // + View: this photo kept in the set under its name, then the next one.
    function addView() {
        const kept = viewName
        if (saveCurrent() < 0)
            return
        m.newPhotoView()
        viewName = nextViewName()
        viewPlane = planeFor(viewName)
        screen.toast("Kept the " + kept + " view. Now a photo for " + viewName)
    }
    function openView(index) {
        if (index === m.photoViewIndex)
            return
        saveCurrent()
        loading = true
        const opened = m.openPhotoView(index)
        loading = false
        if (!opened)
            return
        const v = m.photoViews[index]
        viewName = v.name
        viewPlane = v.plane
        restoreState(v.state)
    }
    // The chips of the views strip: the set, and the photo measured when it
    // is not in it yet (once there is a set or a straightened photo).
    readonly property var viewChips: {
        if (!m)
            return []
        const list = []
        const views = m.photoViews
        for (let i = 0; i < views.length; ++i)
            list.push({ index: i, name: views[i].name, current: i === m.photoViewIndex, url: views[i].straightUrl })
        if (m.photoViewIndex < 0 && (stage === "measure" || views.length > 0))
            list.push({ index: -1, name: viewName, current: true, url: stage === "measure" ? m.straightUrl : "" })
        return list
    }
    function placeViews(sketches) {
        placeSheet.visible = false
        saveCurrent()
        const placed = m.placePhotoViews(sketches)
        if (placed === 0) {
            screen.toast("The views couldn't be placed in the design", true)
            return
        }
        if (screen.app.sketchMode)
            screen.app.finishSketch()
        screen.app.setView("iso")
        screen.app.fitAll()
        screen.close()
    }
    function resetForTest() {
        placeSheet.visible = false
        viewSheet.visible = false
        dimSheet.close()
        mode = "distance"
        clearPoints()
        origin = null
        settingOrigin = false
        showGrid = false
        snap = true
        viewName = "Top"
        viewPlane = "XY"
    }

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

    // ---- Find outline: its result becomes a closed outline dimension.
    Connections {
        target: photo.m
        function onOutlineFound() {
            const f = photo.m.foundOutline
            if (!f.valid) {
                if (photo.screen && f.message)
                    photo.screen.toast(f.message, true)
                return
            }
            photo.setFoundOutline(f)
        }
    }

    FileDialog {
        id: imageDialog
        title: "Open a photo to measure on"
        nameFilters: ["Images (*.jpg *.jpeg *.png *.heic *.bmp *.webp *.tif *.tiff)", "All files (*)"]
        onAccepted: photo.m.loadPhoto(selectedFile)
    }

    // ---------------------------------------------------------------- 1. a photo
    ViewsStrip {
        id: startViews
        objectName: "photoStartViews"
        width: parent.width
        visible: photo.stage === "start" && photo.m && photo.m.photoViews.length > 0
    }
    MeasureNotice {
        objectName: "photoStart"
        anchors { top: startViews.visible ? startViews.bottom : parent.top; topMargin: startViews.visible ? 8 : 0
                  left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: photo.stage === "start"
        iconName: "photo"
        title: photo.m && photo.m.photoViews.length > 0 ? "The " + photo.viewName + " view" : "Measure on a photo"
        text: photo.m && photo.m.photoViews.length > 0
              ? "Photograph the part from its " + photo.viewName.toLowerCase() + " side, the card or the paper flat beside it on the same surface."
              : "Lay a playing card or a sheet of paper flat beside the thing to measure and photograph both, from any angle. FoldForm straightens the photo on the reference."
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
                onPicked: (key) => {
                    photo.m.setReference(key, photo.m.parseLengthMm(sideA.text), photo.m.parseLengthMm(sideB.text))
                    if (key === "rectangle" && Theme.touch && sideA.text === "")
                        sideA.edit()
                    else if (key !== "rectangle")
                        photo.screen.closeKeypad()
                }
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
            SideField { id: sideA; objectName: "photoSideA"; placeholderText: "Long"; next: sideB; label: "Long side" }
            Text { text: "×"; font.pixelSize: 15; color: Theme.mutedText }
            SideField { id: sideB; objectName: "photoSideB"; placeholderText: "Short"; label: "Short side" }
            Text { text: photo.m ? photo.m.unitSymbol("length") : "mm"; font.pixelSize: 13; color: Theme.mutedText }
            Item { Layout.fillWidth: true }
        }

        MeasurePhotoView {
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
            MeasureLoupe {
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
    // View px per millimeter on the straightened photo, and where the
    // rulers' and the grid's zero is (the origin point, else the
    // reference's first corner) in the view.
    readonly property real pxPerMm: m && m.straightMmPerPixel > 0 ? straightView.fit / m.straightMmPerPixel : 0
    readonly property point zeroPx: origin ? origin : m && m.straightReference.length === 4 ? m.straightReference[0] : Qt.point(0, 0)
    readonly property point zeroView: straightView.toView(zeroPx)
    // The rulers' numbers (mm between them: room for the text) and ticks.
    readonly property int rulerLabelStep: {
        for (const s of [5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000])
            if (s * pxPerMm >= 46)
                return s
        return 10000
    }
    readonly property int rulerMinorStep: {
        for (const s of [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000])
            if (s * pxPerMm >= 6 && rulerLabelStep % s === 0)
                return s
        return rulerLabelStep
    }
    // The grid: 10 mm lines, 5 or 1 mm ones between them when zoomed in
    // (50 and 100 mm when the photo is small on screen).
    readonly property int gridStep: pxPerMm >= 8 ? 1 : 5 * pxPerMm >= 8 ? 5 : 10 * pxPerMm >= 6 ? 10 : 50
    readonly property int gridMajor: gridStep <= 10 ? 10 : 100

    Item {
        id: measureStage
        anchors.fill: parent
        visible: photo.stage === "measure"

        RowLayout {
            id: measureTop
            width: parent.width
            spacing: 8
            MeasureSegmented {
                objectName: "photoModes"
                Layout.preferredWidth: photo.narrow ? Math.max(196, measureTop.width * 0.52) : 420
                Layout.maximumWidth: 420
                namePrefix: "photoMode_"
                model: [{ key: "distance", label: "Distance", icon: "distance" }, { key: "outline", label: "Outline", icon: "sketch_polygon" },
                        { key: "circle", label: "Circle", icon: "sketch_circle" }]
                current: photo.mode
                onPicked: (key) => photo.mode = key
            }
            ViewsStrip {
                id: measureViews
                objectName: "photoViews"
                Layout.fillWidth: true
            }
            ActionButton {
                objectName: "photoBack"
                visible: !photo.narrow
                text: "Back to corners"
                iconName: "chevronLeft"
                outlined: true
                onClicked: photo.m.unstraighten()
            }
        }

        Item {
            id: viewBox
            objectName: "photoViewBox"
            anchors { top: measureTop.bottom; topMargin: 10; left: parent.left; right: parent.right; bottom: measureBottom.top; bottomMargin: 10 }
            readonly property int rulerSize: 22

            PhotoRuler {
                id: topRuler
                objectName: "photoRulerTop"
                horizontal: true
                x: viewBox.rulerSize
                width: parent.width - x
                height: viewBox.rulerSize
            }
            PhotoRuler {
                id: leftRuler
                objectName: "photoRulerLeft"
                horizontal: false
                y: viewBox.rulerSize
                width: viewBox.rulerSize
                height: parent.height - y
            }
            Rectangle {
                width: viewBox.rulerSize
                height: viewBox.rulerSize
                color: Theme.panel
                border.color: Theme.panelBorder
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: "mm"
                    font.pixelSize: 9
                    color: Theme.mutedText
                }
            }

            MeasurePhotoView {
                id: straightView
                objectName: "photoStraightView"
                x: viewBox.rulerSize
                y: viewBox.rulerSize
                width: parent.width - x
                height: parent.height - y
                source: photo.stage === "measure" && photo.visible ? photo.m.straightUrl : ""
                sourceSize: photo.m ? photo.m.straightSize : Qt.size(1, 1)
                imageName: "photoStraightImage"
                checkered: true

                // The grid in millimeters over the photo.
                Shape {
                    id: gridShape
                    objectName: "photoGridLines"
                    anchors.fill: parent
                    visible: photo.showGrid && photo.pxPerMm > 0
                    readonly property int step: photo.gridStep
                    // [minor lines, major lines] as PathMultiline paths.
                    readonly property var lines: {
                        if (!visible)
                            return [[], []]
                        const v = straightView, ppm = photo.pxPerMm, z = photo.zeroView
                        const x0 = Math.max(0, v.imageX), x1 = Math.min(v.width, v.imageX + v.sourceSize.width * v.fit)
                        const y0 = Math.max(0, v.imageY), y1 = Math.min(v.height, v.imageY + v.sourceSize.height * v.fit)
                        const minor = [], major = []
                        const s = step * ppm
                        if (s <= 0 || x1 <= x0 || y1 <= y0)
                            return [minor, major]
                        const ratio = Math.round(photo.gridMajor / step)
                        for (let k = Math.ceil((x0 - z.x) / s); k <= Math.floor((x1 - z.x) / s) && minor.length + major.length < 1200; ++k) {
                            const x = z.x + k * s
                            ;(k % ratio === 0 ? major : minor).push([Qt.point(x, y0), Qt.point(x, y1)])
                        }
                        for (let k = Math.ceil((y0 - z.y) / s); k <= Math.floor((y1 - z.y) / s) && minor.length + major.length < 2400; ++k) {
                            const y = z.y + k * s
                            ;(k % ratio === 0 ? major : minor).push([Qt.point(x0, y), Qt.point(x1, y)])
                        }
                        return [minor, major]
                    }
                    ShapePath {
                        strokeColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.28)
                        strokeWidth: 1
                        fillColor: "transparent"
                        PathMultiline { paths: gridShape.lines[0] }
                    }
                    ShapePath {
                        strokeColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.62)
                        strokeWidth: 1
                        fillColor: "transparent"
                        PathMultiline { paths: gridShape.lines[1] }
                    }
                }
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
                // Every dimension: its line, outline or circle (the selected
                // one stronger; a straight distance green).
                Repeater {
                    model: photo.dims
                    delegate: Item {
                        id: dimItem
                        required property var modelData
                        required property int index
                        readonly property bool selected: index === photo.active
                        readonly property bool straightLine: modelData.kind === "distance" && modelData.points.length === 2
                                                             && photo.isStraight(modelData.points[0], modelData.points[1])
                        readonly property color stroke: straightLine ? "#1FA463" : Theme.accent
                        anchors.fill: parent
                        opacity: selected || photo.active < 0 ? 1 : 0.8
                        Shape {
                            anchors.fill: parent
                            preferredRendererType: Shape.CurveRenderer
                            visible: dimItem.modelData.kind !== "circle"
                            ShapePath {
                                strokeColor: dimItem.stroke
                                strokeWidth: dimItem.selected ? 2.5 : 2
                                fillColor: dimItem.modelData.kind === "outline" && dimItem.modelData.closed
                                           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, dimItem.selected ? 0.18 : 0.1) : "transparent"
                                joinStyle: ShapePath.RoundJoin
                                capStyle: ShapePath.RoundCap
                                PathPolyline {
                                    path: {
                                        const d = dimItem.modelData
                                        if (d.found && photo.m) {
                                            const path = photo.m.profilePath(d.points, d.found.through).map(p => straightView.toView(p))
                                            return path.length > 0 ? path.concat([path[0]]) : []
                                        }
                                        const pts = d.points.map(p => straightView.toView(p))
                                        return d.closed && pts.length > 0 ? pts.concat([pts[0]]) : pts
                                    }
                                }
                            }
                        }
                        // Find outline: the holes found (drawn; the sketch gets them).
                        Repeater {
                            model: dimItem.modelData.found ? dimItem.modelData.found.holes : []
                            delegate: Shape {
                                required property var modelData
                                required property int index
                                objectName: dimItem.selected ? "photoHole_" + index : "photoHole_" + dimItem.index + "_" + index
                                anchors.fill: parent
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: dimItem.stroke
                                    strokeWidth: dimItem.selected ? 2.5 : 2
                                    fillColor: Qt.rgba(1, 1, 1, 0.35)
                                    joinStyle: ShapePath.RoundJoin
                                    PathPolyline {
                                        path: {
                                            const pts = photo.m.profilePath(modelData.points, modelData.through).map(p => straightView.toView(p))
                                            return pts.length > 0 ? pts.concat([pts[0]]) : []
                                        }
                                    }
                                }
                            }
                        }
                        Rectangle {
                            readonly property var c: dimItem.modelData.kind === "circle" && dimItem.modelData.points.length === 3 && photo.m
                                                     ? photo.m.circleFrom3(dimItem.modelData.points[0], dimItem.modelData.points[1], dimItem.modelData.points[2]) : null
                            readonly property point at: c && c.valid ? straightView.toView(c.center) : Qt.point(0, 0)
                            readonly property real r: c && c.valid ? c.radius * straightView.fit : 0
                            objectName: dimItem.selected ? "photoCircle" : "photoCircle_" + dimItem.index
                            visible: c !== null && c.valid
                            x: at.x - r
                            y: at.y - r
                            width: 2 * r
                            height: 2 * r
                            radius: r
                            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, dimItem.selected ? 0.16 : 0.08)
                            border.color: Theme.accent
                            border.width: dimItem.selected ? 2.5 : 2
                        }
                    }
                }
                // The origin point: a cross in a ring.
                Item {
                    objectName: "photoOrigin"
                    visible: photo.origin !== null
                    readonly property point at: photo.origin ? straightView.toView(photo.origin) : Qt.point(0, 0)
                    x: at.x - 14
                    y: at.y - 14
                    width: 28
                    height: 28
                    Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: "transparent"; border.color: "#E5484D"; border.width: 2 }
                    Rectangle { anchors.centerIn: parent; width: 28; height: 2; color: "#E5484D" }
                    Rectangle { anchors.centerIn: parent; width: 2; height: 28; color: "#E5484D" }
                    Text {
                        x: 26
                        y: -4
                        text: "0"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: "#E5484D"
                        style: Text.Outline
                        styleColor: "white"
                    }
                }
                // Placing a distance's second end: guides through the first
                // (straight across and down), the line to the finger.
                Shape {
                    id: guides
                    objectName: "photoPlaceGuides"
                    anchors.fill: parent
                    visible: photo.placingDistance
                    readonly property point from: photo.points.length > 0 ? straightView.toView(photo.points[0]) : Qt.point(0, 0)
                    ShapePath {
                        strokeColor: Qt.rgba(1, 1, 1, 0.75)
                        strokeWidth: 1
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [3, 4]
                        fillColor: "transparent"
                        PathMultiline {
                            paths: {
                                const f = guides.from
                                return [[Qt.point(0, f.y), Qt.point(straightView.width, f.y)],
                                        [Qt.point(f.x, 0), Qt.point(f.x, straightView.height)]]
                            }
                        }
                    }
                    ShapePath {
                        strokeColor: photo.placeSnapped ? "#1FA463" : Theme.accent
                        strokeWidth: 2.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathPolyline {
                            path: photo.placingDistance ? [guides.from, straightView.toView(photo.placeAt)] : []
                        }
                    }
                }
                Rectangle {
                    objectName: "photoPlacePoint"
                    visible: photo.placeAt !== null && !photo.settingOrigin
                    readonly property point at: photo.placeAt ? straightView.toView(photo.placeAt) : Qt.point(0, 0)
                    x: at.x - 8
                    y: at.y - 8
                    width: 16
                    height: 16
                    radius: 8
                    color: "transparent"
                    border.color: photo.placeSnapped ? "#1FA463" : Theme.accent
                    border.width: 2.5
                }
                // The points of the selected dimension.
                Repeater {
                    model: photo.points
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property point at: straightView.toView(modelData)
                        readonly property bool first: index === 0 && photo.mode === "outline" && !photo.closed && photo.points.length >= 3
                        readonly property bool arcMiddle: photo.found !== null && photo.found.through[index] === true
                        objectName: "photoPoint_" + index
                        x: at.x - width / 2
                        y: at.y - height / 2
                        width: first ? 20 : arcMiddle ? 11 : 14
                        height: width
                        radius: width / 2
                        color: first ? Theme.accent : "white"
                        border.color: Theme.accent
                        border.width: 2.5
                    }
                }
                // Taps (or a finger's press, slide and lift) add points; a
                // point can be dragged to adjust it (with the magnifier).
                MouseArea {
                    id: tapArea
                    objectName: "photoTapArea"
                    anchors.fill: parent
                    // A second finger (a pinch) takes over: no point is added.
                    preventStealing: false
                    hoverEnabled: !Theme.touch
                    property int dragDim: -1
                    property int dragIndex: -1
                    property bool closing: false
                    property point pressAt
                    property bool moved: false
                    // The nearest point of any dimension within `within` px: {dim, index}.
                    function nearest(p, within) {
                        let best = null, bestD = within
                        for (let d = 0; d < photo.dims.length; ++d) {
                            const pts = d === photo.active ? photo.points : photo.dims[d].points
                            for (let i = 0; i < pts.length; ++i) {
                                const v = straightView.toView(pts[i])
                                const dist = Math.hypot(v.x - p.x, v.y - p.y)
                                if (dist <= bestD) {
                                    best = { dim: d, index: i }
                                    bestD = dist
                                }
                            }
                        }
                        return best
                    }
                    function reset() {
                        pointLoupe.visible = false
                        dragDim = -1
                        closing = false
                        photo.placeAt = null
                        photo.placeSnapped = false
                    }
                    onPressed: (mouse) => {
                        pressAt = Qt.point(mouse.x, mouse.y)
                        moved = false
                        closing = false
                        dragDim = -1
                        const near = photo.settingOrigin ? null : nearest(pressAt, 22)
                        if (near && near.dim === photo.active && near.index === 0 && photo.mode === "outline" && !photo.closed && photo.points.length >= 3) {
                            closing = true // the first point of an open outline closes it (a tap)
                        } else if (near) {
                            dragDim = near.dim
                            dragIndex = near.index
                        } else {
                            photo.updatePreview(straightView.toImage(pressAt))
                        }
                    }
                    onPositionChanged: (mouse) => {
                        const at = Qt.point(mouse.x, mouse.y)
                        if (!pressed) {
                            // The mouse: the line follows the pointer.
                            if (photo.mode === "distance" && photo.points.length === 1 && straightView.inside(at))
                                photo.updatePreview(straightView.toImage(at))
                            return
                        }
                        if (Math.hypot(at.x - pressAt.x, at.y - pressAt.y) > 8)
                            moved = true
                        if (!moved)
                            return
                        if (dragDim >= 0) {
                            photo.movePoint(dragDim, dragIndex, straightView.toImage(at))
                            dragDim = photo.active
                            pointLoupe.target = straightView.toView(photo.points[dragIndex])
                            pointLoupe.visible = true
                            return
                        }
                        if (closing)
                            return
                        photo.updatePreview(straightView.toImage(at))
                        pointLoupe.target = straightView.toView(photo.placeAt)
                        pointLoupe.visible = true
                    }
                    onExited: if (!pressed) photo.placeAt = null
                    onReleased: (mouse) => {
                        const at = Qt.point(mouse.x, mouse.y)
                        if (dragDim >= 0 && moved) {
                            reset()
                            return
                        }
                        if (closing) {
                            reset()
                            photo.closeOutline()
                            return
                        }
                        // A tap on a point measures from it exactly; else where the finger lifted.
                        const p = dragDim >= 0 ? (dragDim === photo.active ? photo.points[dragIndex] : photo.dims[dragDim].points[dragIndex])
                                : straightView.inside(at) ? straightView.toImage(at) : null
                        reset()
                        if (p)
                            photo.addPoint(Qt.point(p.x, p.y))
                    }
                    onCanceled: reset()
                }
                // The labels: a dimension's letter and value (and a
                // distance's angle, or "straight"); a tap selects it, a tap
                // on the selected one opens it (rename, delete).
                Repeater {
                    model: photo.dims
                    delegate: Rectangle {
                        id: label
                        required property var modelData
                        required property int index
                        readonly property bool selected: index === photo.active
                        readonly property var pts: modelData.points.map(p => straightView.toView(p))
                        readonly property bool isDistance: modelData.kind === "distance" && pts.length === 2
                        readonly property real angle: isDistance ? photo.angleOf(modelData.points[0], modelData.points[1]) : 0
                        readonly property bool straight: isDistance && photo.isStraight0(angle)
                        readonly property point at: {
                            if (pts.length === 0)
                                return Qt.point(-100, -100)
                            if (modelData.kind === "circle" && pts.length === 3 && photo.m) {
                                const c = photo.m.circleFrom3(modelData.points[0], modelData.points[1], modelData.points[2])
                                if (c.valid)
                                    return straightView.toView(c.center)
                            }
                            if (isDistance)
                                return Qt.point((pts[0].x + pts[1].x) / 2, (pts[0].y + pts[1].y) / 2)
                            let sx = 0, sy = 0
                            for (const p of pts) {
                                sx += p.x
                                sy += p.y
                            }
                            return Qt.point(sx / pts.length, sy / pts.length)
                        }
                        objectName: "photoLabel_" + index
                        visible: valueLabel.text !== ""
                        x: at.x - width / 2
                        y: at.y - height / 2
                        width: labelRow.implicitWidth + 12
                        height: 24
                        radius: 12
                        color: selected ? Theme.accent : Theme.toast
                        opacity: 0.94
                        Row {
                            id: labelRow
                            anchors.centerIn: parent
                            spacing: 5
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(18, letterText.implicitWidth + 8)
                                height: 18
                                radius: 9
                                color: "white"
                                Text {
                                    id: letterText
                                    anchors.centerIn: parent
                                    text: label.modelData.letter
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                    color: label.selected ? Theme.accent : Theme.toast
                                }
                            }
                            Text {
                                id: valueLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text: photo.valueText(label.modelData)
                                color: "white"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                font.features: ({ "tnum": 1 })
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: label.isDistance
                                text: label.straight ? (photo.narrow ? "✓" : "✓ straight") : label.angle.toFixed(1) + "°"
                                color: label.straight ? "#7EE2A8" : Qt.rgba(1, 1, 1, 0.8)
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                font.features: ({ "tnum": 1 })
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            onClicked: {
                                if (label.selected && photo.isComplete(label.modelData))
                                    dimSheet.open(label.index)
                                else
                                    photo.select(label.index)
                            }
                        }
                    }
                }
                // Each side's length on the selected open or closed outline.
                Repeater {
                    model: {
                        if (!photo.m || photo.mode !== "outline" || !photo.outline)
                            return []
                        const labels = []
                        const v = photo.points.map(p => straightView.toView(p))
                        // A found outline on a phone: its sides are in the readout (labels would crowd it).
                        const n = photo.found && photo.narrow ? 0 : photo.outline.sides.length
                        for (let i = 0; i < n; ++i) {
                            const a = v[i], b = v[(i + 1) % v.length]
                            const at = photo.outline.labelAt ? straightView.toView(photo.outline.labelAt[i]) : Qt.point((a.x + b.x) / 2, (a.y + b.y) / 2)
                            const arc = photo.outline.arcs && photo.outline.arcs[i]
                            labels.push({ x: at.x, y: at.y, text: arc ? "R " + photo.m.formatValue("length", photo.outline.radii[i])
                                                                      : photo.m.formatValue("length", photo.outline.sides[i]) })
                        }
                        for (const hole of photo.outline.holes || []) {
                            if (!hole.circle)
                                continue
                            const c = straightView.toView(hole.center)
                            labels.push({ x: c.x, y: c.y, text: "Ø " + photo.m.formatValue("length", hole.diameter) })
                        }
                        return labels
                    }
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        objectName: "photoSideLabel_" + index
                        x: modelData.x - width / 2
                        y: modelData.y - height / 2
                        width: sideText.implicitWidth + 12
                        height: 20
                        radius: 10
                        color: Theme.toast
                        opacity: 0.85
                        Text {
                            id: sideText
                            anchors.centerIn: parent
                            text: parent.modelData.text
                            color: "white"
                            font.pixelSize: 11
                            font.features: ({ "tnum": 1 })
                        }
                    }
                }
                // The angle while a distance is placed, by the finger.
                Rectangle {
                    id: angleTag
                    objectName: "photoPlaceAngle"
                    readonly property real angle: photo.placeAngle
                    readonly property bool straight: photo.placeSnapped
                    readonly property point at: photo.placingDistance ? straightView.toView(photo.placeAt) : Qt.point(0, 0)
                    visible: photo.placingDistance
                    width: angleText.implicitWidth + 20
                    height: 26
                    radius: 13
                    color: straight ? "#1A8F57" : Theme.toast
                    x: Math.max(0, Math.min(straightView.width - width, at.x - width / 2))
                    y: {
                        if (pointLoupe.visible)
                            return pointLoupe.y > at.y ? Math.min(straightView.height - height, pointLoupe.y + pointLoupe.height + 6)
                                                       : Math.max(0, pointLoupe.y - height - 6)
                        return at.y - height - 18 >= 0 ? at.y - height - 18 : at.y + 18
                    }
                    Text {
                        id: angleText
                        anchors.centerIn: parent
                        text: angleTag.straight ? "Straight · " + Math.round(angleTag.angle) + "°" : angleTag.angle.toFixed(1) + "°"
                        color: "white"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                    }
                }
                // Grid, Snap (straight lines) and Origin, at the bottom left.
                Row {
                    id: viewTools
                    objectName: "photoViewTools"
                    anchors { left: parent.left; bottom: parent.bottom; margins: 6 }
                    spacing: 6
                    z: 5
                    ToolToggle {
                        objectName: "photoGrid"
                        text: "Grid"
                        iconName: "grid"
                        checked: photo.showGrid
                        onClicked: photo.showGrid = !photo.showGrid
                    }
                    ToolToggle {
                        objectName: "photoSnap"
                        text: "Snap"
                        iconName: "snap"
                        checked: photo.snap
                        tip: "Distances near 0° or 90° snap straight"
                        onClicked: photo.snap = !photo.snap
                    }
                    ToolToggle {
                        objectName: "photoOriginButton"
                        text: "Origin"
                        iconName: "origin"
                        checked: photo.settingOrigin || photo.origin !== null
                        tip: "Tap a point to put it at the design's origin (Place views)"
                        onClicked: photo.settingOrigin = !photo.settingOrigin
                    }
                }
                MeasureLoupe {
                    id: pointLoupe
                    view: straightView
                    visible: false
                }
                // ---- Find outline: what gives the best result.
                Rectangle {
                    objectName: "photoOutlineTip"
                    visible: photo.mode === "outline" && photo.points.length === 0 && !photo.dims.some(d => d.found)
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                    height: tipText.implicitHeight + 12
                    radius: 10
                    color: Theme.toast
                    opacity: 0.92
                    z: 6
                    Text {
                        id: tipText
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        verticalAlignment: Text.AlignVCenter
                        text: "Best: the part on plain paper of a contrasting colour, the card beside it, the photo from straight above"
                        color: "white"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
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
                visible: photo.dims.length === 0
                text: photo.hintText
                font.pixelSize: 13
                color: Theme.mutedText
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }
            // Every dimension, a tap selects it (again: rename, delete); Keep all.
            Flickable {
                id: dimsFlick
                objectName: "photoDims"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeight
                visible: photo.dims.length > 0
                contentWidth: dimsRow.width
                contentHeight: height
                clip: true
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: dimsRow
                    height: parent.height
                    spacing: 6
                    Repeater {
                        model: photo.dims
                        delegate: AbstractButton {
                            id: dimChip
                            required property var modelData
                            required property int index
                            readonly property bool selected: index === photo.active
                            objectName: "photoDim_" + index
                            height: dimsRow.height
                            width: chipRow.implicitWidth + 20
                            focusPolicy: Qt.NoFocus
                            onClicked: {
                                if (selected && photo.isComplete(modelData))
                                    dimSheet.open(index)
                                else
                                    photo.select(index)
                            }
                            background: Rectangle {
                                radius: height / 2
                                color: dimChip.selected ? Theme.checked : dimChip.pressed ? Theme.pressed : Theme.fieldIdle
                            }
                            contentItem: Item {
                                Row {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: 5
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: photo.displayName(dimChip.modelData)
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                        color: dimChip.selected ? Theme.accent : Theme.text
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: photo.isComplete(dimChip.modelData) ? photo.valueText(dimChip.modelData) : "…"
                                        font.pixelSize: 12
                                        font.features: ({ "tnum": 1 })
                                        color: dimChip.selected ? Theme.accent : Theme.text
                                    }
                                    LineIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: dimChip.selected && photo.isComplete(dimChip.modelData)
                                        name: "rename"
                                        size: 13
                                        color: Theme.accent
                                    }
                                }
                            }
                        }
                    }
                    ActionButton {
                        objectName: "photoKeepAll"
                        height: dimsRow.height
                        // One finished dimension: the readout's Keep does it.
                        visible: photo.completeCount > 1
                        text: "Keep all"
                        iconName: "keep"
                        outlined: true
                        onClicked: photo.keepAll()
                    }
                }
            }
            // ---- Find outline (on the worker thread: photoBusy).
            ActionButton {
                objectName: "photoFindOutline"
                visible: photo.mode === "outline"
                text: photo.narrow && photo.dims.length > 0 ? "Find" : "Find outline"
                iconName: "sketch_trace"
                accent: photo.points.length === 0 && !photo.dims.some(d => d.found)
                outlined: !accent
                enabled: photo.m && !photo.m.photoBusy
                onClicked: photo.m.findOutline()
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
                iconOnly: photo.narrow || photo.dims.length > 0
                outlined: true
                enabled: photo.dims.length > 0
                onClicked: photo.undoPoint()
            }
            ActionButton {
                objectName: "photoClear"
                text: "Clear"
                iconName: "clear"
                iconOnly: photo.narrow || photo.dims.length > 0
                outlined: true
                enabled: photo.dims.length > 0
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
                    text: photo.stage === "corners" ? "Straightening…" : photo.stage === "measure" ? "Finding the outline…" : "Reading the photo…"
                    font.pixelSize: 15
                    color: Theme.text
                }
            }
        }
    }

    // ---------------------------------------------------------------- a dimension
    Sheet {
        id: dimSheet
        objectName: "photoDimSheet"
        property int index: -1
        readonly property var dim: index >= 0 && index < photo.dims.length ? photo.dims[index] : null
        function open(i) {
            index = i
            dimName.text = dim ? photo.displayName(dim) : ""
            visible = true
        }
        function close() {
            visible = false
            index = -1
        }
        onDismissed: close()
        SheetTitle {
            closeName: "photoDimSheetClose"
            text: dimSheet.dim ? "Dimension " + dimSheet.dim.letter : ""
            onCloseClicked: dimSheet.close()
        }
        Text {
            objectName: "photoDimValue"
            Layout.fillWidth: true
            text: dimSheet.dim ? photo.valueText(dimSheet.dim) : ""
            font.pixelSize: 30
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
            color: Theme.text
        }
        Text {
            Layout.fillWidth: true
            text: "Name (kept as \"" + photo.viewName + " " + (dimName.text.trim() !== "" ? dimName.text.trim() : (dimSheet.dim ? dimSheet.dim.letter : "")) + "\")"
            font.pixelSize: 13
            color: Theme.mutedText
            wrapMode: Text.WordWrap
        }
        TextField {
            id: dimName
            objectName: "photoDimName"
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            font.pixelSize: 15
            color: Theme.text
            selectByMouse: true
            maximumLength: 24
            onAccepted: dimSave.clicked()
            background: Rectangle {
                radius: 8
                color: "white"
                border.color: dimName.activeFocus ? Theme.accent : Theme.panelBorder
                border.width: dimName.activeFocus ? 1.5 : 1
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            ActionButton {
                objectName: "photoDimDelete"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                outlined: true
                text: "Delete"
                iconName: "delete"
                onClicked: {
                    const i = dimSheet.index
                    dimSheet.close()
                    photo.removeDim(i)
                }
            }
            ActionButton {
                objectName: "photoDimKeep"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                outlined: true
                text: "Keep"
                iconName: "keep"
                onClicked: {
                    const d = dimSheet.dim
                    const v = d ? photo.mainValue(d) : null
                    if (v)
                        photo.screen.keep(v.quantity, v.value, "Photo")
                }
            }
            ActionButton {
                id: dimSave
                objectName: "photoDimSave"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                accent: true
                text: "Save"
                onClicked: {
                    const d = dimSheet.dim
                    if (d)
                        photo.renameDim(dimSheet.index, dimName.text.trim() === d.letter ? "" : dimName.text)
                    dimSheet.close()
                }
            }
        }
    }

    // ---------------------------------------------------------------- a view: its name, plane
    Sheet {
        id: viewSheet
        objectName: "photoViewSheet"
        onDismissed: done()
        onVisibleChanged: if (visible) viewNameField.text = photo.viewName
        function done() {
            visible = false
            if (photo.m.photoViewIndex >= 0)
                photo.saveCurrent()
        }
        SheetTitle {
            text: "View"
            closeName: "photoViewSheetClose"
            onCloseClicked: viewSheet.done()
        }
        MeasureSegmented {
            Layout.fillWidth: true
            namePrefix: "photoViewPreset_"
            showIcons: false
            model: [{ key: "Top", label: "Top" }, { key: "Front", label: "Front" }, { key: "Side", label: "Side" }]
            current: photo.viewName
            onPicked: (key) => {
                photo.viewName = key
                photo.viewPlane = photo.planeFor(key)
                viewNameField.text = key
            }
        }
        TextField {
            id: viewNameField
            objectName: "photoViewName"
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            placeholderText: "Name"
            font.pixelSize: 15
            color: Theme.text
            selectByMouse: true
            maximumLength: 40
            onTextEdited: if (text.trim() !== "") photo.viewName = text.trim()
            onAccepted: viewSheet.done()
            background: Rectangle {
                radius: 8
                color: "white"
                border.color: viewNameField.activeFocus ? Theme.accent : Theme.panelBorder
                border.width: viewNameField.activeFocus ? 1.5 : 1
            }
        }
        Text {
            Layout.fillWidth: true
            text: "In the design, on"
            font.pixelSize: 13
            color: Theme.mutedText
        }
        MeasureSegmented {
            Layout.fillWidth: true
            namePrefix: "photoViewPlane_"
            showIcons: false
            model: [{ key: "XY", label: "Ground" }, { key: "XZ", label: "Front" }, { key: "YZ", label: "Right" }]
            current: photo.viewPlane
            onPicked: (key) => photo.viewPlane = key
        }
        Text {
            Layout.fillWidth: true
            text: photo.origin ? "Origin: the point tapped goes to the plane's origin."
                               : "Origin: tap Origin under the photo, then the point that goes to the plane's origin (else the photo's middle)."
            font.pixelSize: 13
            color: Theme.mutedText
            wrapMode: Text.WordWrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            ActionButton {
                objectName: "photoViewClearOrigin"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                visible: photo.origin !== null
                outlined: true
                text: "Clear origin"
                onClicked: photo.origin = null
            }
            ActionButton {
                objectName: "photoViewCorners"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                visible: photo.stage === "measure"
                outlined: true
                text: "Back to corners"
                iconName: "chevronLeft"
                onClicked: {
                    viewSheet.visible = false
                    photo.m.unstraighten()
                }
            }
            ActionButton {
                objectName: "photoViewRemove"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                visible: photo.m && photo.m.photoViewIndex >= 0
                outlined: true
                text: "Remove view"
                iconName: "delete"
                onClicked: {
                    viewSheet.visible = false
                    photo.m.removePhotoView(photo.m.photoViewIndex)
                }
            }
            ActionButton {
                objectName: "photoViewDone"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                accent: true
                text: "Done"
                onClicked: viewSheet.done()
            }
        }
    }

    // ---------------------------------------------------------------- the views into the design
    Sheet {
        id: placeSheet
        objectName: "photoPlaceSheet"
        onDismissed: visible = false
        readonly property int outlines: {
            let n = 0
            for (const v of (photo.m ? photo.m.photoViews : []))
                for (const d of (v.state && v.state.dims ? v.state.dims : []))
                    if (d.kind === "outline" && d.closed)
                        ++n
            return n
        }
        SheetTitle {
            text: "Place views in the design"
            closeName: "photoPlaceSheetClose"
            onCloseClicked: placeSheet.visible = false
        }
        Text {
            Layout.fillWidth: true
            text: "Each photo at its true size on its plane, its origin point at the design's origin."
            font.pixelSize: 13
            color: Theme.mutedText
            wrapMode: Text.WordWrap
        }
        Repeater {
            model: photo.m ? photo.m.photoViews : []
            delegate: RowLayout {
                required property var modelData
                required property int index
                objectName: "photoPlaceRow_" + index
                Layout.fillWidth: true
                spacing: 10
                Image {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 34
                    source: parent.modelData.straightUrl
                    sourceSize.width: 88
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: parent.parent.modelData.name + " · on " + photo.planeLabel(parent.parent.modelData.plane)
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        readonly property var st: parent.parent.modelData.state || {}
                        readonly property bool outlined: (st.dims || []).some(d => d.kind === "outline" && d.closed)
                        text: st.origin ? "Origin: the point tapped" : outlined ? "Origin: the outline's middle" : "Origin: the photo's middle"
                        font.pixelSize: 12
                        color: Theme.mutedText
                        elide: Text.ElideRight
                    }
                }
            }
        }
        ActionButton {
            objectName: "photoPlaceAll"
            Layout.fillWidth: true
            visible: placeSheet.outlines > 0
            accent: true
            iconName: "sketch"
            text: "Photos and " + placeSheet.outlines + (placeSheet.outlines === 1 ? " outline as a sketch" : " outlines as sketches")
            onClicked: photo.placeViews(true)
        }
        ActionButton {
            objectName: "photoPlacePhotos"
            Layout.fillWidth: true
            accent: placeSheet.outlines === 0
            outlined: placeSheet.outlines > 0
            iconName: "photo"
            text: placeSheet.outlines > 0 ? "Photos only" : "Place the photos"
            onClicked: photo.placeViews(false)
        }
    }

    // ---------------------------------------------------------------- parts
    // The views: the set's photos (a tap measures one again; the current
    // one opens its name and plane) and + View (keep this one, take the next).
    component ViewsStrip: Flickable {
        id: strip
        implicitHeight: Theme.controlHeight
        height: Theme.controlHeight
        contentWidth: stripRow.width
        contentHeight: height
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        Row {
            id: stripRow
            height: strip.height
            spacing: 6
            Repeater {
                model: photo.viewChips
                delegate: AbstractButton {
                    id: viewChip
                    required property var modelData
                    required property int index
                    objectName: "photoViewChip_" + index
                    height: stripRow.height
                    width: viewRow.implicitWidth + 16
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        if (modelData.current)
                            viewSheet.visible = true
                        else
                            photo.openView(modelData.index)
                    }
                    background: Rectangle {
                        radius: 10
                        color: viewChip.modelData.current ? Theme.checked : viewChip.pressed ? Theme.pressed : Theme.fieldIdle
                        border.color: viewChip.modelData.current ? Theme.accent : "transparent"
                        border.width: 1
                    }
                    contentItem: Item {
                        Row {
                            id: viewRow
                            anchors.centerIn: parent
                            spacing: 6
                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !photo.narrow && viewChip.modelData.url.toString() !== ""
                                width: 30
                                height: 24
                                source: viewChip.modelData.url
                                sourceSize.width: 60
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: false
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: viewChip.modelData.name
                                font.pixelSize: 13
                                font.weight: viewChip.modelData.current ? Font.DemiBold : Font.Normal
                                color: viewChip.modelData.current ? Theme.accent : Theme.text
                            }
                            LineIcon {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: viewChip.modelData.current
                                name: "chevronDown"
                                size: 14
                                color: Theme.accent
                            }
                        }
                    }
                }
            }
            ActionButton {
                objectName: "photoAddView"
                height: stripRow.height
                visible: photo.stage === "measure"
                outlined: true
                iconName: "plus"
                iconOnly: photo.narrow
                text: photo.narrow ? "View" : "Add a view"
                onClicked: photo.addView()
            }
        }
    }

    // A ruler along the straightened photo's top or left edge: millimeters
    // from the origin point (else the reference's corner), up and right
    // positive as on the design's planes; it follows zoom and pan.
    component PhotoRuler: Rectangle {
        id: ruler
        property bool horizontal: true
        readonly property real ppm: photo.pxPerMm
        readonly property real zero: horizontal ? photo.zeroView.x : photo.zeroView.y
        readonly property int minor: photo.rulerMinorStep
        readonly property int labelStep: photo.rulerLabelStep
        readonly property real length: horizontal ? width : height
        // {lines: PathMultiline paths, labels: [{at, text}]}
        readonly property var ticks: {
            const lines = [], labels = []
            const s = minor * ppm
            if (!(s > 0) || !ruler.visible)
                return { lines: lines, labels: labels }
            const half = labelStep / 2
            const edge = horizontal ? height : width
            for (let k = Math.ceil(-zero / s); k <= Math.floor((length - zero) / s) && lines.length < 800; ++k) {
                const at = zero + k * s
                const mm = k * minor
                const size = mm % labelStep === 0 ? 9 : (half % minor === 0 && mm % half === 0) ? 6 : 3.5
                lines.push(horizontal ? [Qt.point(at, edge), Qt.point(at, edge - size)] : [Qt.point(edge, at), Qt.point(edge - size, at)])
                if (mm % labelStep === 0)
                    labels.push({ at: at, text: String(horizontal ? mm : -mm) })
            }
            return { lines: lines, labels: labels }
        }
        visible: photo.stage === "measure"
        color: Theme.panel
        border.color: Theme.panelBorder
        border.width: 1
        clip: true
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: Theme.mutedText
                strokeWidth: 1
                fillColor: "transparent"
                PathMultiline { paths: ruler.ticks.lines }
            }
        }
        Repeater {
            model: ruler.ticks.labels
            delegate: Text {
                required property var modelData
                text: modelData.text
                font.pixelSize: 9
                font.features: ({ "tnum": 1 })
                color: Theme.mutedText
                rotation: ruler.horizontal ? 0 : -90
                x: ruler.horizontal ? modelData.at + 2 : (ruler.width - 8) / 2 - implicitWidth / 2
                y: ruler.horizontal ? 1 : modelData.at + 2 + implicitWidth / 2 - implicitHeight / 2
            }
        }
    }

    // A small toggle over the photo (Grid, Snap, Origin): an icon and a word.
    component ToolToggle: AbstractButton {
        id: tool
        property string iconName
        property string tip
        // Just the icon where the photo is small (a phone, the Duo's outer display).
        readonly property bool iconOnly: photo.narrow || straightView.height < 320
        checkable: false
        implicitHeight: Theme.touch ? 44 : 32
        implicitWidth: iconOnly ? implicitHeight : toolRow.implicitWidth + 18
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        background: Rectangle {
            radius: height / 2
            color: tool.checked ? Theme.accent : Theme.toast
            opacity: tool.pressed ? 0.8 : 0.9
        }
        contentItem: Item {
            Row {
                id: toolRow
                anchors.centerIn: parent
                spacing: 5
                LineIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: tool.iconName
                    size: 15
                    color: "white"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !tool.iconOnly
                    text: tool.text
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: "white"
                }
            }
        }
        ToolTip.visible: hovered && !Theme.touch && (tip !== "" || iconOnly)
        ToolTip.text: tip !== "" ? text + ": " + tip : text
        ToolTip.delay: 500
    }

    // A card over the whole workspace (a dimension, a view, placing views).
    component Sheet: Rectangle {
        id: sheet
        default property alias content: sheetColumn.data
        signal dismissed()
        parent: photo.screen ? photo.screen : photo
        anchors.fill: parent
        z: 10
        visible: false
        color: Theme.scrim
        MouseArea {
            anchors.fill: parent
            onClicked: sheet.dismissed()
            onWheel: (wheel) => wheel.accepted = true
        }
        MeasureCard {
            objectName: sheet.objectName + "Panel"
            lifted: true
            // In the middle of the safe area (the Duo's side bar, a phone's notch).
            readonly property real safeWidth: parent.width - Theme.safeLeft - Theme.safeRight
            readonly property real safeHeight: parent.height - Theme.safeTop - Theme.safeBottom
            x: Theme.safeLeft + (safeWidth - width) / 2
            y: Theme.safeTop + (safeHeight - height) / 2
            width: Math.min(safeWidth - 32, 400)
            height: Math.min(safeHeight - 32, sheetColumn.implicitHeight + 40)
            clip: true
            MouseArea { anchors.fill: parent } // taps on the card stay on it
            Flickable {
                anchors.fill: parent
                contentHeight: sheetColumn.implicitHeight + 40
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ColumnLayout {
                    id: sheetColumn
                    x: 20
                    y: 20
                    width: parent.width - 40
                    spacing: 10
                }
            }
        }
    }
    component SheetTitle: RowLayout {
        id: sheetTitle
        property string text
        property string closeName
        signal closeClicked()
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: sheetTitle.text
            font.pixelSize: 17
            font.weight: Font.DemiBold
            color: Theme.text
            elide: Text.ElideRight
        }
        AbstractButton {
            objectName: sheetTitle.closeName
            implicitWidth: 44
            implicitHeight: 44
            focusPolicy: Qt.NoFocus
            onClicked: sheetTitle.closeClicked()
            contentItem: Item { LineIcon { anchors.centerIn: parent; name: "close"; size: 16 } }
            background: Rectangle { radius: 22; color: parent.pressed ? Theme.pressed : "transparent" }
        }
    }

    component SideField: TextField {
        id: side
        property Item next: null
        property string label
        // The keypad types into it on touch (as the value boxes do); a
        // hardware keyboard still types.
        readonly property bool keypadOpen: photo.screen && photo.screen.keypadClient === client
        readonly property var client: ({
            name: "measureSide",
            mode: () => "length",
            hasNext: () => side.next !== null,
            text: () => side.text,
            replacing: () => side.text.length > 0 && side.selectedText === side.text,
            setText: (text, replacing) => {
                side.text = text
                if (replacing)
                    side.selectAll()
                else
                    side.cursorPosition = text.length
                side.apply()
            },
            next: () => { if (side.next) side.next.edit() },
            done: () => photo.screen.closeKeypad(),
            target: () => side.mapToItem(null, 0, 0, side.width, side.height),
            keepClear: "",
            display: () => side.text,
            displayName: () => side.label
        })
        function apply() {
            photo.m.setReference("rectangle", photo.m.parseLengthMm(sideA.text), photo.m.parseLengthMm(sideB.text))
        }
        function edit() {
            forceActiveFocus()
            selectAll()
            if (Theme.touch)
                photo.screen.openKeypad(client)
        }
        Layout.preferredWidth: 92
        implicitHeight: Theme.controlHeight
        font.pixelSize: 15
        font.features: ({ "tnum": 1 })
        color: Theme.text
        horizontalAlignment: Text.AlignRight
        readOnly: Theme.touch // the keypad, not the system keyboard
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: !Theme.touch
        onTextEdited: apply()
        Keys.onPressed: (event) => {
            if (Theme.touch && keypadOpen && photo.screen.keypadHardwareKey(event))
                event.accepted = true
        }
        TapHandler {
            enabled: Theme.touch
            onTapped: side.edit()
        }
        background: Rectangle {
            radius: 8
            color: "white"
            border.color: side.activeFocus || side.keypadOpen ? Theme.accent : Theme.panelBorder
            border.width: side.activeFocus || side.keypadOpen ? 1.5 : 1
        }
    }
}
