// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import OpenShape

// Measure (docs/MEASURE.md): one workspace over the design with the
// instruments that measure real things - the Ruler (the screen at true
// size), the Fold (the iPhone Duo's hinge), the Level (gravity), Photo (a
// photo straightened on a card or a sheet of paper) and Camera (ARKit) -
// a big readout with Keep, and the kept measurements (the tray), which
// feed the design: openFor("length" / "angle") opens it for a value box,
// and its button types the value there (useValue).
//
// Every instrument shows what it measures as `reading` ({valid, caption,
// source, items: [{label, quantity, value}]}) and offers `actions`
// ([{key, label, icon}], run by runAction(key)): Make sketch, Make box.
Rectangle {
    id: screen

    required property AppController app
    readonly property MeasureController m: app.measure

    property bool active: false
    // "length" or "angle" when opened for a value box (openFor), else "".
    property string forKind: ""
    property string current: "ruler"
    // An angle was asked for before the hinge reported: the Fold takes
    // over once it does (unless another instrument was picked meanwhile).
    property bool foldPending: false
    // Which of the reading's items the readout shows big.
    property int chosen: 0
    // The last value handed to a value box (the acceptance run reads it).
    property string lastUsedText: ""

    signal useValue(string text)

    function open() {
        forKind = ""
        show()
    }
    function openFor(kind) {
        forKind = kind === "angle" ? "angle" : "length"
        show()
        if (forKind === "angle") {
            current = m.hingeAvailable ? "fold" : "level"
            foldPending = !m.hingeAvailable
        } else {
            current = "ruler"
        }
    }
    function close() {
        active = false
    }
    function show() {
        chipCard.close()
        calibration.close()
        foldPending = false
        active = true
        m.updateScale()
        forceActiveFocus()
    }
    function select(key) {
        foldPending = false
        current = key
    }
    // A short message; `problem`: no check mark.
    function toast(text, problem) {
        toastBox.show(text, problem === true)
    }
    function openCalibration() {
        calibration.open()
    }
    // Keeps a value; says what it was kept as. Returns the new id.
    function keep(quantity, value, source) {
        const id = m.addMeasurement(quantity, value, source)
        if (id !== "")
            toast("Kept as " + m.measurement(id).name)
        return id
    }
    function quantityFits(quantity) {
        return forKind === "" || quantity === forKind
    }
    // Hands a value to the box that asked: the workspace closes first, so
    // the box (a sketch dimension's editor) keeps the focus it takes.
    function use(quantity, value) {
        const text = m.valueText(quantity, value)
        if (text === "")
            return
        lastUsedText = text
        close()
        useValue(text)
    }
    // A fresh start (the acceptance run, before it counts on anything).
    function resetForTest() {
        chipCard.close()
        calibration.close()
        current = "ruler"
        forKind = ""
        chosen = 0
        lastUsedText = ""
        ruler.mode = "length"
        ruler.jawA = 0
        ruler.jawB = 40
        ruler.ringDiameter = 24
        ruler.ringX = 0.5
        ruler.ringY = 0.42
        fold.mode = "angle"
        fold.calibrating = false
        photo.mode = "distance"
        photo.clearPoints()
        camera.mode = "distance"
        scanInstrument.reviewIndex = -1
        measureKeypad.close()
    }
    // The app's numeric keypad for a field of the workspace (a reference's
    // sides): its client as NumericKeypad describes it.
    readonly property var keypadClient: measureKeypad.client
    function openKeypad(client) {
        measureKeypad.attach(client)
    }
    function closeKeypad() {
        measureKeypad.close()
    }
    function keypadHardwareKey(event) {
        return measureKeypad.hardwareKey(event)
    }

    color: Theme.background
    visible: active
    focus: visible
    onActiveChanged: {
        if (active) {
            m.startSensors()
        } else {
            m.stopSensors()
            chipCard.close()
            calibration.close()
            measureKeypad.close()
        }
    }
    onCurrentChanged: chosen = 0
    Component.onCompleted: m.attachWindow(Window.window)

    Connections {
        target: screen.m
        function onSensorsChanged() {
            if (screen.foldPending && screen.m.hingeAvailable && screen.active) {
                screen.foldPending = false
                screen.current = "fold"
            }
        }
    }

    Keys.onEscapePressed: {
        if (measureKeypad.open)
            measureKeypad.close()
        else if (chipCard.visible)
            chipCard.close()
        else if (calibration.visible)
            calibration.close()
        else
            close()
    }
    // Nothing reaches the design underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // Fold only where a hinge reports, Camera only where ARKit is.
    readonly property var instruments: {
        const list = [{ key: "ruler", label: "Ruler", icon: "ruler" }]
        if (m.hingeAvailable)
            list.push({ key: "fold", label: "Fold", icon: "fold" })
        list.push({ key: "level", label: "Level", icon: "level" })
        list.push({ key: "photo", label: "Photo", icon: "photo" })
        if (m.arAvailable)
            list.push({ key: "camera", label: "Camera", icon: "ar" })
        list.push({ key: "scan", label: "Scan", icon: "scan" })
        return list
    }
    onInstrumentsChanged: {
        if (!instruments.some(i => i.key === current))
            current = "ruler"
    }

    readonly property Item currentInstrument: current === "fold" ? fold : current === "level" ? level
                                            : current === "photo" ? photo : current === "camera" ? camera
                                            : current === "scan" ? scanInstrument : ruler
    readonly property var reading: currentInstrument ? currentInstrument.reading : null
    readonly property var readingItems: reading && reading.valid ? reading.items : []
    readonly property var item: readingItems.length > 0 ? readingItems[Math.min(chosen, readingItems.length - 1)] : null
    // What Use types: the item shown if it fits the value box, else the first that does.
    readonly property var useItem: {
        if (forKind === "")
            return null
        if (item && item.quantity === forKind)
            return item
        for (const it of readingItems)
            if (it.quantity === forKind)
                return it
        return null
    }
    readonly property var actions: currentInstrument && currentInstrument.actions ? currentInstrument.actions : []
    // The instrument shows its reading huge (the Fold, the Level): in a
    // narrow window the readout does not repeat the number.
    readonly property bool slimReadout: !side && currentInstrument !== null && currentInstrument.showsReading === true

    // ---------------------------------------------------------------- layout
    // Wide windows (an iPad, the desktop, the iPhone Duo unfolded): the
    // instrument at the left, the readout and the kept measurements in a
    // column at the right. Narrow ones (a phone, the Duo's outer display,
    // the Duo turned tall): one above the other.
    readonly property bool side: width >= 760 && width >= height * 1.1
    readonly property int pad: Theme.compact ? 12 : 20

    Item {
        id: safeArea
        anchors { fill: parent; topMargin: Theme.safeTop; rightMargin: Theme.safeRight
                  bottomMargin: Theme.safeBottom; leftMargin: Theme.safeLeft }
    }

    Item {
        id: content
        anchors.fill: safeArea
        anchors.margins: screen.pad
        anchors.topMargin: Theme.safeTop > 0 ? 6 : screen.pad

        // ---- header: title, the instruments, close
        Item {
            id: header
            width: parent.width
            height: screen.side ? Math.max(segments.height, closeButton.height)
                                : closeButton.height + 10 + segments.height

            Text {
                id: title
                objectName: "measureTitle"
                anchors.left: parent.left
                y: (closeButton.height - height) / 2
                text: "Measure"
                font.pixelSize: screen.side ? 24 : 26
                font.weight: Font.Bold
                color: Theme.text
            }
            Text {
                anchors.left: title.right
                anchors.leftMargin: 10
                anchors.baseline: title.baseline
                visible: screen.forKind !== "" && (!screen.side || parent.width > 1000)
                text: screen.forKind === "angle" ? "an angle for the value box" : "a length for the value box"
                font.pixelSize: 14
                color: Theme.mutedText
            }
            MeasureSegmented {
                id: segments
                objectName: "measureTabs"
                namePrefix: "measureTab_"
                model: screen.instruments
                current: screen.current
                x: screen.side ? Math.max(title.width + 24, (parent.width - width) / 2) : 0
                y: screen.side ? (closeButton.height - height) / 2 : closeButton.height + 10
                width: screen.side ? Math.min(screen.instruments.length * 112, parent.width - title.width - closeButton.width - 64)
                                   : parent.width
                onPicked: (key) => screen.select(key)
            }
            AbstractButton {
                id: closeButton
                objectName: "measureClose"
                anchors.right: parent.right
                width: 44
                height: 44
                text: "Close"
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                onClicked: screen.close()
                background: MeasureCard {
                    radius: 22
                    color: closeButton.pressed ? Theme.pressed : closeButton.hovered ? Theme.hover : Theme.card
                }
                contentItem: Item {
                    LineIcon { anchors.centerIn: parent; name: "close"; size: 18; color: Theme.text }
                }
                ToolTip.visible: hovered && !Theme.touch
                ToolTip.text: "Back to the design (Esc)"
                ToolTip.delay: 500
            }
        }

        // ---- the instrument
        Item {
            id: instrumentArea
            objectName: "measureInstrumentArea"
            anchors {
                top: header.bottom
                topMargin: screen.side ? 16 : 12
                left: parent.left
                right: screen.side ? sideColumn.left : parent.right
                rightMargin: screen.side ? 20 : 0
                bottom: screen.side ? parent.bottom : readout.visible ? readout.top : tray.visible ? tray.top : parent.bottom
                bottomMargin: screen.side ? 0 : 12
            }

            MeasureRuler {
                id: ruler
                objectName: "measureRuler"
                anchors.fill: parent
                m: screen.m
                screen: screen
                visible: screen.current === "ruler"
            }
            MeasureFold {
                id: fold
                objectName: "measureFold"
                anchors.fill: parent
                m: screen.m
                screen: screen
                visible: screen.current === "fold"
            }
            MeasureLevel {
                id: level
                objectName: "measureLevel"
                anchors.fill: parent
                m: screen.m
                screen: screen
                visible: screen.current === "level"
            }
            MeasurePhoto {
                id: photo
                objectName: "measurePhoto"
                anchors.fill: parent
                m: screen.m
                screen: screen
                visible: screen.current === "photo"
            }
            MeasureCamera {
                id: camera
                objectName: "measureCamera"
                anchors.fill: parent
                m: screen.m
                screen: screen
                visible: screen.current === "camera"
            }
            MeasureScan {
                id: scanInstrument
                objectName: "measureScan"
                anchors.fill: parent
                m: screen.m
                screen: screen
                visible: screen.current === "scan"
            }
        }

        // ---- wide: the readout and the kept measurements at the right
        Item {
            id: sideColumn
            visible: screen.side
            anchors { top: header.bottom; topMargin: 16; right: parent.right; bottom: parent.bottom }
            width: screen.side ? Math.min(340, Math.max(280, content.width * 0.3)) : 0
        }

        // ---- the readout: the value big, Keep, Use, the instrument's actions
        // (in a narrow window only once there is something to say: a photo's
        // first steps get the room).
        MeasureCard {
            id: readout
            objectName: "measureReadout"
            visible: screen.side || screen.forKind !== "" || (screen.reading !== null && (screen.reading.valid || screen.reading.caption !== ""))
            x: screen.side ? sideColumn.x : 0
            y: screen.side ? sideColumn.y : (tray.visible ? tray.y - 12 : content.height) - height
            width: screen.side ? sideColumn.width : content.width
            height: readoutColumn.implicitHeight + 2 * readoutColumn.y

            ColumnLayout {
                id: readoutColumn
                x: 16
                y: screen.side ? 18 : 14
                width: parent.width - 32
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            objectName: "measureReadoutCaption"
                            Layout.fillWidth: true
                            text: screen.item ? (screen.reading.caption !== "" ? screen.item.label + " · " + screen.reading.caption : screen.item.label)
                                : screen.reading && screen.reading.caption !== "" ? screen.reading.caption : "Nothing measured yet"
                            font.pixelSize: 13
                            color: Theme.mutedText
                            elide: Text.ElideRight
                        }
                        Row {
                            Layout.fillWidth: true
                            visible: !screen.slimReadout
                            spacing: 6
                            clip: true
                            Text {
                                objectName: "measureReadoutValue"
                                text: screen.item ? screen.m.formatNumber(screen.item.quantity, screen.item.value) : "—"
                                font.pixelSize: screen.side ? 46 : 40
                                font.weight: Font.DemiBold
                                font.features: ({ "tnum": 1 })
                                color: screen.item ? Theme.text : Theme.disabledText
                            }
                            Text {
                                objectName: "measureReadoutUnit"
                                anchors.baseline: parent.children[0].baseline
                                visible: text !== ""
                                text: screen.item ? screen.m.unitText(screen.item.quantity, screen.item.value) : ""
                                font.pixelSize: screen.side ? 20 : 18
                                color: Theme.mutedText
                            }
                        }
                    }
                    ActionButton {
                        objectName: "measureKeep"
                        Layout.alignment: Qt.AlignVCenter
                        visible: !screen.side
                        text: "Keep"
                        iconName: "keep"
                        outlined: true
                        enabled: screen.item !== null
                        onClicked: screen.keep(screen.item.quantity, screen.item.value, screen.reading.source)
                    }
                }
                // The reading's other values: a tap shows it big (wrapped at
                // the side; one row that scrolls in a narrow window).
                Flow {
                    Layout.fillWidth: true
                    visible: screen.side && screen.readingItems.length > 1
                    spacing: 6
                    Repeater {
                        model: screen.side ? screen.readingItems : []
                        delegate: ItemChip {}
                    }
                }
                Flickable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: itemRow.height
                    visible: !screen.side && screen.readingItems.length > 1
                    contentWidth: itemRow.width
                    contentHeight: itemRow.height
                    clip: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: itemRow
                        spacing: 6
                        Repeater {
                            model: screen.side ? [] : screen.readingItems
                            delegate: ItemChip {}
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: screen.side || screen.forKind !== "" || screen.actions.length > 0
                    columns: screen.side ? 1 : Math.max(1, (screen.forKind !== "" ? 1 : 0) + screen.actions.length)
                    rowSpacing: 8
                    columnSpacing: 8
                    ActionButton {
                        objectName: "measureKeepWide"
                        Layout.fillWidth: true
                        visible: screen.side
                        text: "Keep"
                        iconName: "keep"
                        outlined: true
                        enabled: screen.item !== null
                        onClicked: screen.keep(screen.item.quantity, screen.item.value, screen.reading.source)
                    }
                    Repeater {
                        model: screen.actions
                        delegate: ActionButton {
                            required property var modelData
                            objectName: "measureAction_" + modelData.key
                            Layout.fillWidth: true
                            text: modelData.label
                            iconName: modelData.icon || ""
                            outlined: true
                            enabled: modelData.enabled === undefined || modelData.enabled
                            onClicked: screen.currentInstrument.runAction(modelData.key)
                        }
                    }
                    ActionButton {
                        objectName: "measureUse"
                        Layout.fillWidth: true
                        visible: screen.forKind !== ""
                        accent: true
                        enabled: screen.useItem !== null
                        text: screen.useItem ? "Use " + screen.m.formatValue(screen.useItem.quantity, screen.useItem.value)
                                             : screen.forKind === "angle" ? "Use an angle" : "Use a length"
                        onClicked: screen.use(screen.useItem.quantity, screen.useItem.value)
                    }
                }
            }
        }

        // ---- the kept measurements
        MeasureTray {
            id: tray
            objectName: "measureTray"
            m: screen.m
            vertical: screen.side
            visible: screen.side || screen.m.measurements.length > 0
            x: screen.side ? sideColumn.x : 0
            y: screen.side ? readout.y + readout.height + 16 : content.height - height
            width: screen.side ? sideColumn.width : content.width
            height: screen.side ? Math.max(0, content.height - y) : implicitHeight
            onPicked: (id) => chipCard.open(id)
        }
    }

    // ---------------------------------------------------------------- toast
    Rectangle {
        id: toastBox
        objectName: "measureToast"
        property alias text: toastText.text
        x: screen.side ? content.x + instrumentArea.x + (instrumentArea.width - width) / 2 : (screen.width - width) / 2
        y: screen.side ? content.y + content.height - height - 64
                       : content.y + (readout.visible ? readout.y : tray.visible ? tray.y : content.height) - height - 10
        width: Math.min(toastText.implicitWidth + 36, screen.width - 32)
        height: 40
        radius: 20
        color: Theme.toast
        opacity: 0
        visible: opacity > 0
        z: 5
        Row {
            anchors.centerIn: parent
            spacing: 8
            LineIcon { anchors.verticalCenter: parent.verticalCenter; visible: !toastBox.problem; name: "check"; size: 16; color: "white" }
            Text {
                id: toastText
                objectName: "measureToastText"
                anchors.verticalCenter: parent.verticalCenter
                color: "white"
                font.pixelSize: 14
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
        }
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Timer { id: toastTimer; interval: 2400; onTriggered: toastBox.opacity = 0 }
        property bool problem: false
        function show(message, isProblem) {
            text = message
            problem = isProblem
            opacity = 0.96
            toastTimer.restart()
        }
    }

    // ---------------------------------------------------------------- a kept measurement
    Rectangle {
        id: chipCard
        objectName: "measureChipCard"
        property string itemId: ""
        readonly property var entry: itemId !== "" ? screen.m.measurements.find(e => e.id === itemId) || null : null
        property bool renaming: false
        anchors.fill: parent
        z: 10
        visible: false
        color: Theme.scrim
        function open(id) {
            itemId = id
            renaming = false
            visible = true
        }
        function close() {
            visible = false
            renaming = false
            itemId = ""
        }
        MouseArea {
            anchors.fill: parent
            onClicked: chipCard.close()
            onWheel: (wheel) => wheel.accepted = true
        }
        MeasureCard {
            objectName: "measureChipPanel"
            lifted: true
            anchors.centerIn: parent
            width: Math.min(parent.width - 32, 380)
            height: cardColumn.implicitHeight + 40
            MouseArea { anchors.fill: parent } // taps on the card stay on it
            ColumnLayout {
                id: cardColumn
                x: 20
                y: 20
                width: parent.width - 40
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        objectName: "measureChipName"
                        Layout.fillWidth: true
                        visible: !chipCard.renaming
                        text: chipCard.entry ? chipCard.entry.name : ""
                        textFormat: Text.PlainText
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    TextField {
                        id: renameField
                        objectName: "measureRenameField"
                        Layout.fillWidth: true
                        visible: chipCard.renaming
                        implicitHeight: Theme.controlHeight
                        font.pixelSize: 15
                        color: Theme.text
                        selectByMouse: true
                        maximumLength: 40
                        onAccepted: renameSave.clicked()
                        background: Rectangle {
                            radius: 8
                            color: "white"
                            border.color: Theme.accent
                            border.width: 1.5
                        }
                    }
                    AbstractButton {
                        objectName: "measureChipClose"
                        visible: !chipCard.renaming
                        implicitWidth: 44
                        implicitHeight: 44
                        focusPolicy: Qt.NoFocus
                        onClicked: chipCard.close()
                        contentItem: Item { LineIcon { anchors.centerIn: parent; name: "close"; size: 16 } }
                        background: Rectangle { radius: 22; color: parent.pressed ? Theme.pressed : "transparent" }
                    }
                }
                Row {
                    spacing: 6
                    Text {
                        objectName: "measureChipValue"
                        text: chipCard.entry ? screen.m.formatNumber(chipCard.entry.quantity, chipCard.entry.value) : ""
                        font.pixelSize: 40
                        font.weight: Font.DemiBold
                        font.features: ({ "tnum": 1 })
                        color: Theme.text
                    }
                    Text {
                        anchors.baseline: parent.children[0].baseline
                        text: chipCard.entry ? screen.m.unitText(chipCard.entry.quantity, chipCard.entry.value) : ""
                        font.pixelSize: 18
                        color: Theme.mutedText
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: chipCard.entry ? "Measured with the " + chipCard.entry.source + (chipCard.entry.source === "Camera" ? " (AR)" : "") : ""
                    font.pixelSize: 13
                    color: Theme.mutedText
                }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    columns: chipCard.renaming ? 2 : (useChip.visible ? 3 : 2)
                    columnSpacing: 8
                    rowSpacing: 8
                    ActionButton {
                        id: useChip
                        objectName: "measureChipUse"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        visible: !chipCard.renaming && screen.forKind !== "" && chipCard.entry !== null
                        accent: true
                        enabled: chipCard.entry !== null && screen.quantityFits(chipCard.entry.quantity)
                        text: "Use"
                        onClicked: {
                            const e = chipCard.entry
                            chipCard.close()
                            screen.use(e.quantity, e.value)
                        }
                    }
                    ActionButton {
                        objectName: "measureChipRename"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        visible: !chipCard.renaming
                        outlined: true
                        text: "Rename"
                        iconName: "rename"
                        onClicked: {
                            renameField.text = chipCard.entry ? chipCard.entry.name : ""
                            chipCard.renaming = true
                            renameField.forceActiveFocus()
                            renameField.selectAll()
                        }
                    }
                    ActionButton {
                        objectName: "measureChipDelete"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        visible: !chipCard.renaming
                        outlined: true
                        text: "Delete"
                        iconName: "delete"
                        onClicked: {
                            const id = chipCard.itemId
                            chipCard.close()
                            screen.m.removeMeasurement(id)
                        }
                    }
                    ActionButton {
                        objectName: "measureRenameCancel"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        visible: chipCard.renaming
                        outlined: true
                        text: "Cancel"
                        onClicked: chipCard.renaming = false
                    }
                    ActionButton {
                        id: renameSave
                        objectName: "measureRenameSave"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        visible: chipCard.renaming
                        accent: true
                        text: "Save"
                        enabled: renameField.text.trim() !== ""
                        onClicked: {
                            if (screen.m.renameMeasurement(chipCard.itemId, renameField.text))
                                chipCard.renaming = false
                        }
                    }
                }
                // A length or an angle kept in the open project, under a name
                // any value box takes (docs/MEASURE.md, "Measurements in the
                // project"): "shelf_depth - 2".
                ActionButton {
                    objectName: "measureChipToProject"
                    Layout.fillWidth: true
                    visible: !chipCard.renaming && chipCard.entry !== null
                             && (chipCard.entry.quantity === "length" || chipCard.entry.quantity === "angle")
                             && typeof screen.app.addProjectMeasurement === "function"
                    outlined: true
                    text: "Add to project"
                    iconName: "plus"
                    onClicked: {
                        const e = chipCard.entry
                        const name = screen.app.addProjectMeasurement(e.name, e.quantity, e.value, e.source)
                        chipCard.close()
                        if (name !== "")
                            screen.toast("In the project as " + name + ": type it in any value box")
                        else
                            screen.toast("It couldn't be added to the project", true)
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- the keypad
    // Docked along the bottom of a narrow window, at the bottom right of a
    // wide one (over the readout's column), clear of the safe areas.
    NumericKeypad {
        id: measureKeypad
        objectName: "measureKeypad"
        app: screen.app
        z: 9
        docked: !screen.side
        placed: true
        width: docked ? screen.width - Theme.safeLeft - Theme.safeRight - 16 : implicitWidth
        x: docked ? Theme.safeLeft + 8 : screen.width - Theme.safeRight - width - screen.pad
        y: screen.height - Theme.safeBottom - height - 8
        onClientChanged: if (client === null) screen.forceActiveFocus()
    }

    // ---------------------------------------------------------------- check the scale with a card
    CardCalibration {
        id: calibration
        objectName: "cardCalibration"
        anchors.fill: parent
        z: 8
        m: screen.m
        onCalibrated: screen.toast("Scale checked with a card")
    }

    // One of the reading's values, small: a tap shows it big.
    component ItemChip: AbstractButton {
        id: other
        required property var modelData
        required property int index
        readonly property bool selected: index === Math.min(screen.chosen, screen.readingItems.length - 1)
        objectName: "measureReadoutItem_" + index
        height: Theme.touch ? 36 : 30
        width: otherLabel.implicitWidth + 22
        focusPolicy: Qt.NoFocus
        onClicked: screen.chosen = index
        background: Rectangle {
            radius: height / 2
            color: other.selected ? Theme.checked : other.pressed ? Theme.pressed : Theme.fieldIdle
        }
        contentItem: Text {
            id: otherLabel
            text: other.modelData.label + "  " + screen.m.formatValue(other.modelData.quantity, other.modelData.value)
            font.pixelSize: 12
            font.weight: other.selected ? Font.DemiBold : Font.Normal
            font.features: ({ "tnum": 1 })
            color: other.selected ? Theme.accent : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
