import QtQuick
import Orkc

Canvas {
    id: root
    property var values: []
    property color stroke: Theme.accent
    property real minSpan: 4
    implicitHeight: 48
    onValuesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const v = values || []
        if (v.length < 2) return
        let lo = Math.min.apply(null, v), hi = Math.max.apply(null, v)
        if (hi - lo < minSpan) { const m = (hi + lo) / 2; lo = m - minSpan / 2; hi = m + minSpan / 2 }
        const px = (i) => i * (width - 2) / (v.length - 1) + 1
        const py = (x) => height - 3 - (x - lo) / (hi - lo) * (height - 6)
        const g = ctx.createLinearGradient(0, 0, 0, height)
        g.addColorStop(0, Qt.rgba(stroke.r, stroke.g, stroke.b, 0.28))
        g.addColorStop(1, Qt.rgba(stroke.r, stroke.g, stroke.b, 0))
        ctx.beginPath()
        ctx.moveTo(px(0), height)
        for (let i = 0; i < v.length; ++i) ctx.lineTo(px(i), py(v[i]))
        ctx.lineTo(px(v.length - 1), height)
        ctx.closePath()
        ctx.fillStyle = g
        ctx.fill()
        ctx.beginPath()
        for (let i = 0; i < v.length; ++i) i ? ctx.lineTo(px(i), py(v[i])) : ctx.moveTo(px(i), py(v[i]))
        ctx.lineWidth = 2
        ctx.lineJoin = "round"
        ctx.strokeStyle = stroke
        ctx.stroke()
    }
}
