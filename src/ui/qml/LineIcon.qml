// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Shapes
import OpenShape

// FoldForm's own line icons (menus first): drawn on a 24-unit grid with
// round strokes, scaled to `size` without losing sharpness (the curve
// renderer). An unknown name draws nothing.
Item {
    id: icon

    property string name
    property color color: Theme.iconColor
    property real size: Theme.touch ? 20 : 18

    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        "home": "M3.5 10.5 L12 3.5 L20.5 10.5 M5.5 9 V19 a1.5 1.5 0 0 0 1.5 1.5 H17 a1.5 1.5 0 0 0 1.5 -1.5 V9 M10 20.5 V14.5 H14 V20.5",
        "new": "M14 3.5 H7 a2 2 0 0 0 -2 2 V18.5 a2 2 0 0 0 2 2 H17 a2 2 0 0 0 2 -2 V8.5 Z M14 3.5 V8.5 H19 M12 11.5 V17.5 M9 14.5 H15",
        "open": "M3.5 7 a2 2 0 0 1 2 -2 H9.5 L11.5 7 H18.5 a2 2 0 0 1 2 2 V17 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 Z M3.5 10 H20.5",
        "recent": "M20.5 12 A8.5 8.5 0 1 1 3.5 12 A8.5 8.5 0 1 1 20.5 12 Z M12 7.5 V12 L15 14",
        "import": "M12 3.5 V14.5 M7.5 10 L12 14.5 L16.5 10 M4 15.5 V18.5 a2 2 0 0 0 2 2 H18 a2 2 0 0 0 2 -2 V15.5",
        "save": "M5.5 3.5 H15.5 L20.5 8.5 V18.5 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 V5.5 a2 2 0 0 1 2 -2 Z M8 3.5 V8 H14 V3.5 M7.5 20.5 V14.5 a1 1 0 0 1 1 -1 H15.5 a1 1 0 0 1 1 1 V20.5",
        "saveAs": "M12.5 3.5 H6.5 a2 2 0 0 0 -2 2 V18.5 a2 2 0 0 0 2 2 H10 M12.5 3.5 L18 9 V10.5 M12.5 3.5 V9 H18 M13.5 20.5 L14 17.5 L19 12.5 a1.4 1.4 0 0 1 2 2 L16 19.5 Z",
        "share": "M12 14.5 V3.5 M7.5 7.5 L12 3.5 L16.5 7.5 M8.5 10.5 H6.5 a2 2 0 0 0 -2 2 V18.5 a2 2 0 0 0 2 2 H17.5 a2 2 0 0 0 2 -2 V12.5 a2 2 0 0 0 -2 -2 H15.5",
        "export": "M14 3.5 H20.5 V10 M20.5 3.5 L11.5 12.5 M18.5 14 V18.5 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 V7.5 a2 2 0 0 1 2 -2 H10",
        "preferences": "M4 7 H13.5 M18.5 7 H20 M18.5 7 A2.5 2.5 0 1 1 13.5 7 A2.5 2.5 0 1 1 18.5 7 Z M4 17 H5.5 M10.5 17 H20 M10.5 17 A2.5 2.5 0 1 1 5.5 17 A2.5 2.5 0 1 1 10.5 17 Z",
        "about": "M20.5 12 A8.5 8.5 0 1 1 3.5 12 A8.5 8.5 0 1 1 20.5 12 Z M12 11 V16.5 M12 7.7 V7.8",
        "file": "M14 3.5 H7 a2 2 0 0 0 -2 2 V18.5 a2 2 0 0 0 2 2 H17 a2 2 0 0 0 2 -2 V8.5 Z M14 3.5 V8.5 H19",
        "clear": "M5 7 H19 M9.5 7 V5.5 a1.5 1.5 0 0 1 1.5 -1.5 H13 a1.5 1.5 0 0 1 1.5 1.5 V7 M7 7 L7.8 18.6 a2 2 0 0 0 2 1.9 H14.2 a2 2 0 0 0 2 -1.9 L17 7",
        "plane": "M3.5 15.5 L9.5 8.5 H20.5 L14.5 15.5 Z",
        "axis": "M5 19 L19 5 M3 17 L7 21 M17 3 L21 7",
        "rename": "M4 20 L4.6 16.4 L15.5 5.5 a2.1 2.1 0 0 1 3 3 L7.6 19.4 Z M13.5 7.5 L16.5 10.5",
        "duplicate": "M9 8.5 a1.5 1.5 0 0 1 1.5 -1.5 H18.5 a1.5 1.5 0 0 1 1.5 1.5 V18.5 a1.5 1.5 0 0 1 -1.5 1.5 H10.5 a1.5 1.5 0 0 1 -1.5 -1.5 Z M15 4 H5.5 a1.5 1.5 0 0 0 -1.5 1.5 V15",
        "remove": "M20.5 12 A8.5 8.5 0 1 1 3.5 12 A8.5 8.5 0 1 1 20.5 12 Z M8.5 12 H15.5",
        "delete": "M5 7 H19 M9.5 7 V5.5 a1.5 1.5 0 0 1 1.5 -1.5 H13 a1.5 1.5 0 0 1 1.5 1.5 V7 M7 7 L7.8 18.6 a2 2 0 0 0 2 1.9 H14.2 a2 2 0 0 0 2 -1.9 L17 7 M10.5 11 V16.5 M13.5 11 V16.5",
        "chevron": "M9.5 6.5 L15 12 L9.5 17.5",
        "chevronDown": "M6.5 9.5 L12 15 L17.5 9.5",
        "chevronUp": "M6.5 14.5 L12 9 L17.5 14.5",
        "chevronLeft": "M14.5 6.5 L9 12 L14.5 17.5",
        "undo": "M9 14.5 L4.5 10 L9 5.5 M4.5 10 H14.5 a4.5 4.5 0 0 1 0 9 H11.5",
        "redo": "M15 14.5 L19.5 10 L15 5.5 M19.5 10 H9.5 a4.5 4.5 0 0 0 0 9 H12.5",
        "help": "M20.5 12 A8.5 8.5 0 1 1 3.5 12 A8.5 8.5 0 1 1 20.5 12 Z M9.6 9.6 a2.5 2.5 0 0 1 4.9 0.7 c0 1.7 -2.5 2.1 -2.5 3.7 M12 16.9 V17",
        // The sketch tools.
        "sketch_select": "M6 3.5 L18 13.5 L12.5 14.2 L15.6 20 L13.4 21 L10.4 15.2 L6.5 18.8 Z",
        "sketch_trim": "M8.5 9.5 A2.5 2.5 0 1 1 3.5 9.5 A2.5 2.5 0 1 1 8.5 9.5 Z M8.5 17.5 A2.5 2.5 0 1 1 3.5 17.5 A2.5 2.5 0 1 1 8.5 17.5 Z M8 11 L20.5 18.5 M8 16 L20.5 5.5",
        "sketch_line": "M5 19 L19 5 M5 19 m-1.5 0 a1.5 1.5 0 1 0 3 0 a1.5 1.5 0 1 0 -3 0 M19 5 m-1.5 0 a1.5 1.5 0 1 0 3 0 a1.5 1.5 0 1 0 -3 0",
        "sketch_rectangle": "M4.5 6.5 H19.5 V17.5 H4.5 Z",
        "sketch_circle": "M19.5 12 A7.5 7.5 0 1 1 4.5 12 A7.5 7.5 0 1 1 19.5 12 Z M12 12 V12.1",
        "sketch_arc": "M4.5 18 A9 9 0 0 1 19.5 18 M4.5 18 V18.1 M19.5 18 V18.1",
        "sketch_centerRectangle": "M4.5 6.5 H19.5 V17.5 H4.5 Z M12 10.5 V13.5 M10.5 12 H13.5",
        "sketch_polygon": "M12 3.5 L20 9.3 L17 18.7 H7 L4 9.3 Z",
        "sketch_tangentArc": "M3.5 18 H10 A6.5 6.5 0 0 0 16.5 11.5 V5",
        "sketch_slot": "M8 8 H16 A4 4 0 0 1 16 16 H8 A4 4 0 0 1 8 8 Z",
        // A pen tracing a wavy outline (the Trace tool).
        "sketch_trace": "M3.5 18.5 C5.5 13 8 18 10 14 S13 10.5 14.5 12 M14.5 12 L15.2 9.3 L19.6 4.9 a1.4 1.4 0 0 1 2 2 L17.2 11.3 Z",
        // A ruler (Measure: the value boxes' button, the top bar).
        "ruler": "M3.5 8 H20.5 V16 H3.5 Z M7 8 V11 M10.5 8 V12.5 M14 8 V11 M17.5 8 V12.5",
        // The tools (the create palette).
        "box": "M12 3.5 L19.5 7.5 V16.5 L12 20.5 L4.5 16.5 V7.5 Z M4.5 7.5 L12 11.5 L19.5 7.5 M12 11.5 V20.5",
        "sketch": "M4 20.5 H11 M5.5 18 L6.1 14.4 L15.5 5 a2.1 2.1 0 0 1 3 3 L9.1 17.4 Z M13.5 7 L16.5 10",
        "loft": "M5 17 A7 2.5 0 1 0 19 17 A7 2.5 0 1 0 5 17 Z M8.5 5.5 H15.5 M8.5 5.5 L5 17 M15.5 5.5 L19 17",
        "pushpull": "M4 15.5 L12 19.5 L20 15.5 L12 11.5 Z M12 3.5 V9 M9 6.5 L12 3.5 L15 6.5",
        "fillet": "M4.5 20 V13.5 a9 9 0 0 1 9 -9 H20 M4.5 4.5 H6.5 M4.5 4.5 V6.5",
        "chamfer": "M4.5 20 V10.5 L10.5 4.5 H20 M4.5 4.5 H6.5 M4.5 4.5 V6.5",
        "shell": "M4.5 5.5 V17.5 a2 2 0 0 0 2 2 H17.5 a2 2 0 0 0 2 -2 V5.5 M8 5.5 V15 a1 1 0 0 0 1 1 H15 a1 1 0 0 0 1 -1 V5.5 M4.5 5.5 H8 M16 5.5 H19.5",
        "offset": "M8.5 9.5 a1 1 0 0 1 1 -1 H14.5 a1 1 0 0 1 1 1 V14.5 a1 1 0 0 1 -1 1 H9.5 a1 1 0 0 1 -1 -1 Z M4.5 7 a2.5 2.5 0 0 1 2.5 -2.5 H17 a2.5 2.5 0 0 1 2.5 2.5 V17 a2.5 2.5 0 0 1 -2.5 2.5 H7 a2.5 2.5 0 0 1 -2.5 -2.5 Z",
        "hole": "M3.5 8.5 a2 2 0 0 1 2 -2 H18.5 a2 2 0 0 1 2 2 V15.5 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 Z M14.5 12 A2.5 2.5 0 1 1 9.5 12 A2.5 2.5 0 1 1 14.5 12 Z",
        "text": "M5 6.5 V5 H19 V6.5 M12 5 V19 M9 19 H15",
        "move": "M12 3.5 V20.5 M3.5 12 H20.5 M9.5 6 L12 3.5 L14.5 6 M9.5 18 L12 20.5 L14.5 18 M6 9.5 L3.5 12 L6 14.5 M18 9.5 L20.5 12 L18 14.5",
        "rotate": "M20 12 A8 8 0 1 1 17.3 6 M17.8 2.8 L17.3 6 L20.6 6.6",
        "mirror": "M12 3.5 V5.5 M12 8.5 V10.5 M12 13.5 V15.5 M12 18.5 V20.5 M8.5 6.5 L3.5 17.5 H8.5 Z M15.5 6.5 L20.5 17.5 H15.5 Z",
        "pattern": "M4.5 6 a1.5 1.5 0 0 1 1.5 -1.5 H8.5 a1.5 1.5 0 0 1 1.5 1.5 V8.5 a1.5 1.5 0 0 1 -1.5 1.5 H6 a1.5 1.5 0 0 1 -1.5 -1.5 Z M14 6 a1.5 1.5 0 0 1 1.5 -1.5 H18 a1.5 1.5 0 0 1 1.5 1.5 V8.5 a1.5 1.5 0 0 1 -1.5 1.5 H15.5 a1.5 1.5 0 0 1 -1.5 -1.5 Z M4.5 15.5 a1.5 1.5 0 0 1 1.5 -1.5 H8.5 a1.5 1.5 0 0 1 1.5 1.5 V18 a1.5 1.5 0 0 1 -1.5 1.5 H6 a1.5 1.5 0 0 1 -1.5 -1.5 Z M14 15.5 a1.5 1.5 0 0 1 1.5 -1.5 H18 a1.5 1.5 0 0 1 1.5 1.5 V18 a1.5 1.5 0 0 1 -1.5 1.5 H15.5 a1.5 1.5 0 0 1 -1.5 -1.5 Z",
        "align": "M4.5 3.5 V20.5 M8 7.5 H16.5 V10.5 H8 Z M8 13.5 H19.5 V16.5 H8 Z",
        "union": "M12 6.8 A6 6 0 1 0 12 17.2 A6 6 0 1 0 12 6.8 Z",
        "subtract": "M12 6.8 A6 6 0 1 0 12 17.2 A6 6 0 0 1 12 6.8 Z M12 6.8 A6 6 0 1 1 12 17.2",
        "intersect": "M12 6.8 A6 6 0 0 1 12 17.2 A6 6 0 0 1 12 6.8 Z M12 6.8 A6 6 0 1 0 12 17.2 M12 6.8 A6 6 0 1 1 12 17.2",
        // Measure (docs/MEASURE.md): the workspace and its instruments.
        "ruler": "M2.9 16.4 L16.4 2.9 L21.1 7.6 L7.6 21.1 Z M6.3 13 L8 14.8 M9.7 9.7 L12.1 12.1 M13 6.3 L14.8 8",
        "fold": "M3.5 19.5 H20.5 M3.5 19.5 L14.5 6.5 M9.5 19.5 A6 6 0 0 0 7.4 14.9",
        "level": "M3.5 9.5 a2 2 0 0 1 2 -2 H18.5 a2 2 0 0 1 2 2 V14.5 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 Z M9 7.5 V16.5 M15 7.5 V16.5 M10.4 12 a1.6 1.6 0 1 0 3.2 0 a1.6 1.6 0 1 0 -3.2 0",
        "camera": "M3.5 8.5 a2 2 0 0 1 2 -2 H7.8 L9.3 4.5 H14.7 L16.2 6.5 H18.5 a2 2 0 0 1 2 2 V17.5 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 Z M15.5 13 A3.5 3.5 0 1 1 8.5 13 A3.5 3.5 0 1 1 15.5 13 Z",
        "photo": "M3.5 6.5 a2 2 0 0 1 2 -2 H18.5 a2 2 0 0 1 2 2 V17.5 a2 2 0 0 1 -2 2 H5.5 a2 2 0 0 1 -2 -2 Z M3.5 16 L8.5 11 L12.5 15 L15 12.5 L20.5 18 M15.6 8.6 a1 1 0 1 0 2 0 a1 1 0 1 0 -2 0",
        "ar": "M3.5 7.5 V3.5 H7.5 M16.5 3.5 H20.5 V7.5 M20.5 16.5 V20.5 H16.5 M7.5 20.5 H3.5 V16.5 M12 7.3 L16.2 9.6 V14.4 L12 16.7 L7.8 14.4 V9.6 Z M7.8 9.6 L12 11.9 L16.2 9.6 M12 11.9 V16.7",
        "close": "M6.5 6.5 L17.5 17.5 M17.5 6.5 L6.5 17.5",
        "check": "M5 12.5 L10 17.5 L19 7",
        "plus": "M12 5.5 V18.5 M5.5 12 H18.5",
        "minus": "M5.5 12 H18.5",
        "card": "M3.5 7 a1.5 1.5 0 0 1 1.5 -1.5 H19 a1.5 1.5 0 0 1 1.5 1.5 V17 a1.5 1.5 0 0 1 -1.5 1.5 H5 a1.5 1.5 0 0 1 -1.5 -1.5 Z M3.5 9.5 H20.5 M6.5 15 H10.5",
        "distance": "M4.5 12 H19.5 M4.5 7.5 V16.5 M19.5 7.5 V16.5 M7.5 9.5 L5 12 L7.5 14.5 M16.5 9.5 L19 12 L16.5 14.5",
        "gauge": "M19.5 12 A7.5 7.5 0 1 1 4.5 12 A7.5 7.5 0 1 1 19.5 12 Z M6.7 17.3 L17.3 6.7",
        "keep": "M7 3.5 H17 a1 1 0 0 1 1 1 V20.5 L12 16.5 L6 20.5 V4.5 a1 1 0 0 1 1 -1 Z",
        "straighten": "M3.5 19 L7.5 7 H16.5 L20.5 19 Z M12 3.5 V10 M9.5 6 L12 3.5 L14.5 6",
        "hold": "M12 3.5 V6 M12 18 V20.5 M3.5 12 H6 M18 12 H20.5 M16.5 12 A4.5 4.5 0 1 1 7.5 12 A4.5 4.5 0 1 1 16.5 12 Z"
    })

    Shape {
        width: 24
        height: 24
        scale: icon.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        visible: icon.paths[icon.name] !== undefined
        ShapePath {
            strokeColor: icon.color
            strokeWidth: 1.7
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
