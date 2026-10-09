import QtQuick
import Orkc

// HSV colour wheel: angle = hue, radius = saturation, separate value (brightness).
// The wheel bitmap is painted once per size; dragging only moves the knob.
Item {
    id: root
    property real hue: 0.75
    property real saturation: 0.75
    property real value: 1.0
    readonly property color color: Qt.hsva(hue, saturation, value, 1)
    readonly property real radius: Math.min(width, height) / 2
    signal moved(color c)       // continuous while dragging
    signal committed(color c)   // on release

    function setColor(c) {
        const col = Qt.color(c)
        if (col.hsvSaturation > 0.001 && col.hsvHue >= 0) hue = col.hsvHue
        saturation = col.hsvSaturation
        value = col.hsvValue
    }

    implicitWidth: 240
    implicitHeight: 240

    Canvas {
        id: wheel
        anchors.centerIn: parent
        width: root.radius * 2
        height: width
        renderStrategy: Canvas.Cooperative
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            const r = width / 2
            ctx.reset()
            // Conical gradient sweeps counter-clockwise from +x, matching hue = atan2(-dy, dx).
            const g = ctx.createConicalGradient(r, r, 0)
            const stops = 12
            for (let i = 0; i <= stops; ++i) g.addColorStop(i / stops, Qt.hsva(i / stops, 1, 1, 1))
            ctx.fillStyle = g
            ctx.beginPath(); ctx.arc(r, r, r, 0, Math.PI * 2); ctx.fill()
            const w = ctx.createRadialGradient(r, r, 0, r, r, r)
            w.addColorStop(0, "rgba(255,255,255,1)")
            w.addColorStop(1, "rgba(255,255,255,0)")
            ctx.fillStyle = w
            ctx.beginPath(); ctx.arc(r, r, r, 0, Math.PI * 2); ctx.fill()
        }
    }
    // Value (brightness) as a black veil: cheap, no repaint while dragging the value slider.
    Rectangle {
        anchors.fill: wheel
        radius: width / 2
        color: "black"
        opacity: 1 - root.value
    }
    Rectangle {
        anchors.fill: wheel
        radius: width / 2
        color: "transparent"
        border.color: Theme.border
        border.width: 1
    }

    Rectangle {
        id: knob
        readonly property real a: root.hue * Math.PI * 2
        readonly property real d: root.saturation * (root.radius - 2)
        width: drag.pressed ? 26 : 22
        height: width
        radius: width / 2
        x: root.width / 2 + Math.cos(a) * d - width / 2
        y: root.height / 2 - Math.sin(a) * d - height / 2
        color: root.color
        border.color: "white"
        border.width: 3
        Behavior on width { NumberAnimation { duration: Theme.durFast } }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.35)
        }
    }

    MouseArea {
        id: drag
        anchors.fill: wheel
        cursorShape: Qt.CrossCursor
        function pick(mx, my) {
            const dx = mx - width / 2, dy = my - height / 2
            let a = Math.atan2(-dy, dx)
            if (a < 0) a += Math.PI * 2
            root.hue = a / (Math.PI * 2)
            root.saturation = Math.min(1, Math.hypot(dx, dy) / (width / 2))
            root.moved(root.color)
        }
        onPressed: (e) => pick(e.x, e.y)
        onPositionChanged: (e) => { if (pressed) pick(e.x, e.y) }
        onReleased: root.committed(root.color)
    }
}
