import QtQuick
import Orkc

// Segmented LED-bar meter. value may be null -> all segments dark.
Item {
    id: root
    property var value
    property real from: 0
    property real to: 100
    property color tone: Theme.accent
    property real segmentWidth: 5
    property real gap: 3
    implicitHeight: 14
    implicitWidth: 200

    readonly property int count: Math.max(4, Math.floor((width + gap) / (segmentWidth + gap)))
    readonly property real frac: Theme.isNum(value) ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    readonly property int lit: Theme.isNum(value) ? Math.max(1, Math.round(frac * count)) : 0

    Row {
        spacing: root.gap
        Repeater {
            model: root.count
            delegate: Rectangle {
                required property int index
                readonly property bool on: index < root.lit
                width: root.segmentWidth
                height: root.height
                radius: 1.5
                color: on ? root.tone : Theme.surface3
                opacity: on ? (0.45 + 0.55 * (index + 1) / Math.max(root.lit, 1)) : 1
                Behavior on color { ColorAnimation { duration: Theme.dur } }
                Behavior on opacity { NumberAnimation { duration: Theme.dur } }
            }
        }
    }
}
