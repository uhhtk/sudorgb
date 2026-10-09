import QtQuick
import Orkc

// Duty-vs-liquid-temperature curve editor with draggable points.
// points: [[tempC, duty%], ...]. Edits keep points sorted & monotone.
Item {
    id: root
    property var points: [[20, 30], [35, 50], [45, 80], [55, 100]]
    property real tMin: 20
    property real tMax: 60
    property int dutyFloor: 0
    property var currentTemp
    property var currentDuty
    property color tone: Theme.accent
    signal edited(var points)       // on release
    implicitHeight: 220

    readonly property real padL: 36
    readonly property real padB: 24
    readonly property real padT: 8
    readonly property real padR: 8
    function px(t) { return padL + (t - tMin) / (tMax - tMin) * (width - padL - padR) }
    function py(d) { return padT + (1 - d / 100) * (height - padT - padB) }
    function tAt(x) { return tMin + (x - padL) / (width - padL - padR) * (tMax - tMin) }
    function dAt(y) { return (1 - (y - padT) / (height - padT - padB)) * 100 }

    onPointsChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onCurrentTempChanged: canvas.requestPaint()
    onToneChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.font = "11px '" + Theme.font + "'"
            ctx.lineWidth = 1
            // grid
            for (let d = 0; d <= 100; d += 25) {
                ctx.strokeStyle = Theme.border
                ctx.beginPath(); ctx.moveTo(root.padL, root.py(d)); ctx.lineTo(width - root.padR, root.py(d)); ctx.stroke()
                ctx.fillStyle = Theme.textFaint
                ctx.fillText(d + "%", 2, root.py(d) + 4)
            }
            for (let t = root.tMin; t <= root.tMax; t += 10) {
                ctx.strokeStyle = Theme.border
                ctx.beginPath(); ctx.moveTo(root.px(t), root.padT); ctx.lineTo(root.px(t), height - root.padB); ctx.stroke()
                ctx.fillStyle = Theme.textFaint
                ctx.fillText(Theme.temp(t, 0) + "°", root.px(t) - 8, height - 6)
            }
            if (root.dutyFloor > 0) {
                ctx.fillStyle = Qt.rgba(1, 1, 1, 0.03)
                ctx.fillRect(root.padL, root.py(root.dutyFloor), width - root.padL - root.padR, height - root.padB - root.py(root.dutyFloor))
            }
            const p = root.points
            if (!p || p.length < 1) return
            // fill
            const g = ctx.createLinearGradient(0, root.padT, 0, height - root.padB)
            g.addColorStop(0, Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.30))
            g.addColorStop(1, Qt.rgba(root.tone.r, root.tone.g, root.tone.b, 0.0))
            ctx.beginPath()
            ctx.moveTo(root.px(root.tMin), height - root.padB)
            ctx.lineTo(root.px(root.tMin), root.py(p[0][1]))
            for (let i = 0; i < p.length; ++i) ctx.lineTo(root.px(p[i][0]), root.py(p[i][1]))
            ctx.lineTo(root.px(root.tMax), root.py(p[p.length - 1][1]))
            ctx.lineTo(root.px(root.tMax), height - root.padB)
            ctx.closePath()
            ctx.fillStyle = g
            ctx.fill()
            // line
            ctx.beginPath()
            ctx.moveTo(root.px(root.tMin), root.py(p[0][1]))
            for (let i = 0; i < p.length; ++i) ctx.lineTo(root.px(p[i][0]), root.py(p[i][1]))
            ctx.lineTo(root.px(root.tMax), root.py(p[p.length - 1][1]))
            ctx.lineWidth = 2.5
            ctx.strokeStyle = root.tone
            ctx.stroke()
            // current liquid temperature marker
            if (Theme.isNum(root.currentTemp) && root.currentTemp >= root.tMin && root.currentTemp <= root.tMax) {
                ctx.setLineDash([4, 4])
                ctx.lineWidth = 1
                ctx.strokeStyle = Theme.textDim
                ctx.beginPath(); ctx.moveTo(root.px(root.currentTemp), root.padT); ctx.lineTo(root.px(root.currentTemp), height - root.padB); ctx.stroke()
                ctx.setLineDash([])
            }
        }
    }

    Repeater {
        model: root.points ? root.points.length : 0
        delegate: Rectangle {
            id: handle
            required property int index
            readonly property var pt: root.points[index]
            width: drag.pressed ? 18 : 14
            height: width
            radius: width / 2
            x: root.px(pt[0]) - width / 2
            y: root.py(pt[1]) - height / 2
            color: drag.pressed || drag.containsMouse ? "white" : Theme.surface
            border.color: root.tone
            border.width: 3
            Behavior on width { NumberAnimation { duration: Theme.durFast } }

            Rectangle {
                visible: drag.pressed
                color: Theme.surface3
                radius: 6
                width: tip.implicitWidth + 12; height: 22
                anchors.bottom: parent.top; anchors.bottomMargin: 6
                anchors.horizontalCenter: parent.horizontalCenter
                Text {
                    id: tip
                    anchors.centerIn: parent
                    text: Theme.temp(handle.pt[0], 0) + Theme.tempUnit + " · " + handle.pt[1] + "%"
                    color: Theme.text
                    font.family: Theme.mono
                    font.pixelSize: Theme.fsTiny
                }
            }
            MouseArea {
                id: drag
                anchors.fill: parent
                anchors.margins: -8
                hoverEnabled: true
                cursorShape: Qt.SizeAllCursor
                onPositionChanged: (e) => {
                    if (!pressed) return
                    const g = mapToItem(root, e.x, e.y)
                    const pts = root.points.map(p => [p[0], p[1]])
                    const i = handle.index
                    const lo = i > 0 ? pts[i - 1][0] + 1 : root.tMin
                    const hi = i < pts.length - 1 ? pts[i + 1][0] - 1 : root.tMax
                    pts[i][0] = Math.round(Math.max(lo, Math.min(hi, root.tAt(g.x))))
                    let d = Math.round(Math.max(root.dutyFloor, Math.min(100, root.dAt(g.y))))
                    // keep monotone: not below previous, not above next
                    if (i > 0) d = Math.max(d, pts[i - 1][1])
                    if (i < pts.length - 1) d = Math.min(d, pts[i + 1][1])
                    pts[i][1] = d
                    root.points = pts
                }
                onReleased: root.edited(root.points)
            }
        }
    }
}
