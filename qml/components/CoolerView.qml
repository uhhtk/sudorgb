import QtQuick
import Orkc

// Virtual Kraken head: LCD preview inside a 24-LED ring that mirrors the pump
// ring's configured effect (static colours shown as-is; spectrum rotates,
// breathing pulses, the way the hardware does).
Item {
    id: root
    property var ring: ({})            // Kraken desired lighting for "ring"
    property int ledCount: 24
    property alias lcdMode: lcd.mode
    property alias lcdSource: lcd.source
    property alias liquidTemp: lcd.liquidTemp
    property alias busy: lcd.busy
    property alias caption: lcd.caption
    property color maskColor: Theme.surface
    implicitWidth: 320
    implicitHeight: 320

    readonly property real d: Math.min(width, height)
    readonly property string mode: ring && ring.mode ? ring.mode : "none"
    readonly property var cols: (ring && ring.colors) ? ring.colors : []
    readonly property real level: ring && ring.brightness !== undefined ? ring.brightness / 100 : 1
    readonly property real period: ring && ring.speed === "slow" ? 12 : ring && ring.speed === "fast" ? 3 : 6

    function ledColor(i) {
        const n = ledCount
        if (mode === "none" || mode === "off") return Theme.surface3
        if (mode === "spectrum") return Qt.hsva(i / n, 1, 1, 1)
        if (cols.length === 0) return Theme.surface3
        if (mode === "gradient" && cols.length > 1) {
            const pos = i / Math.max(n - 1, 1) * (cols.length - 1)
            const j = Math.min(Math.floor(pos), cols.length - 2)
            return Qt.tint(cols[j], Theme.withAlpha(Qt.color(cols[j + 1]), pos - j))
        }
        return cols[0]
    }

    Item {
        id: ringItem
        anchors.centerIn: parent
        width: root.d; height: root.d
        opacity: root.mode === "none" || root.mode === "off" ? 1 : root.level
        RotationAnimation on rotation {
            running: root.mode === "spectrum" && Theme.durSlow > 0 && root.visible
            from: 0; to: -360; loops: Animation.Infinite; duration: root.period * 1000
        }
        SequentialAnimation on opacity {
            running: root.mode === "breathing" && Theme.durSlow > 0 && root.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.2 * root.level; duration: root.period * 500; easing.type: Easing.InOutSine }
            NumberAnimation { to: root.level; duration: root.period * 500; easing.type: Easing.InOutSine }
        }
        Repeater {
            model: root.ledCount
            delegate: Rectangle {
                required property int index
                readonly property real a: index / root.ledCount * Math.PI * 2 - Math.PI / 2
                readonly property real r: root.d / 2 - 7
                width: Math.max(4, root.d * 0.022); height: Math.max(8, root.d * 0.05); radius: width / 2
                x: root.d / 2 + Math.cos(a) * r - width / 2
                y: root.d / 2 + Math.sin(a) * r - height / 2
                rotation: index / root.ledCount * 360
                color: root.ledColor(index)
                Behavior on color { ColorAnimation { duration: Theme.dur } }
            }
        }
    }

    LcdPreview {
        id: lcd
        anchors.centerIn: parent
        width: root.d - root.d * 0.16
        height: width
        maskColor: root.maskColor
    }
}
