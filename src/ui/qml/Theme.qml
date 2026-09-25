pragma Singleton
import QtQuick

// Visual constants. One place to tune the calm, neutral look.
QtObject {
    readonly property color background: "#ECEEF1"
    readonly property color panel: "#F9FAFB"
    readonly property color panelBorder: "#D8DCE2"
    readonly property color text: "#1F2329"
    readonly property color mutedText: "#6A717C"
    readonly property color accent: "#2977ED"
    readonly property color accentPressed: "#1B5FCC"
    readonly property color hover: "#E7EBF0"
    readonly property color pressed: "#DADFE6"
    readonly property color checked: "#DCE8FB"
    readonly property color fieldIdle: "#EEF1F4"
    readonly property color error: "#D93F42"
    readonly property color toast: "#2A2F36"

    readonly property int margin: 16
    readonly property int panelPadding: 6
    // Touch-friendly: never smaller than ~36 logical px.
    readonly property int controlHeight: 36
}
