import QtQuick
import QtQuick.Shapes

// Stroke icons on a 24x24 grid, rendered with Qt Quick Shapes (GPU curve renderer).
Item {
    id: root
    property string name: "chip"
    property color color: "#ffffff"
    property real size: 18
    property real stroke: 1.8
    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        "dashboard": "M3 3h7v7H3z M14 3h7v7h-7z M14 14h7v7h-7z M3 14h7v7H3z",
        "lighting": "M12 2v2 M12 20v2 M4.93 4.93l1.41 1.41 M17.66 17.66l1.41 1.41 M2 12h2 M20 12h2 M6.34 17.66l-1.41 1.41 M19.07 4.93l-1.41 1.41 M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8z",
        "lcd": "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 8a4 4 0 1 0 0 8a4 4 0 1 0 0-8z",
        "fan": "M12 12m-2 0a2 2 0 1 0 4 0a2 2 0 1 0-4 0 M12 10c0-4 1.5-7 4.5-7s3 4.5-2.5 7.5 M14 12c4 0 7 1.5 7 4.5s-4.5 3-7.5-2.5 M12 14c0 4-1.5 7-4.5 7s-3-4.5 2.5-7.5 M10 12c-4 0-7-1.5-7-4.5s4.5-3 7.5 2.5",
        "profiles": "M12 2 2 7l10 5 10-5-10-5z M2 17l10 5 10-5 M2 12l10 5 10-5",
        "settings": "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6",
        "chip": "M6 6h12v12H6z M9 2v4 M15 2v4 M9 18v4 M15 18v4 M2 9h4 M2 15h4 M18 9h4 M18 15h4",
        "keyboard": "M2 6h20v12H2z M6 10h.01 M10 10h.01 M14 10h.01 M18 10h.01 M7 14h10",
        "mouse": "M12 2a6 6 0 0 0-6 6v8a6 6 0 0 0 12 0V8a6 6 0 0 0-6-6z M12 6v4",
        "gpu": "M2 7h20v10H2z M6 17v3 M18 17v3 M7 12a2 2 0 1 0 4 0a2 2 0 1 0-4 0z M15 10h3 M15 14h3",
        "memory": "M3 7h18v10H3z M7 7v10 M11 7v10 M15 7v10 M5 17v3 M19 17v3",
        "strip": "M3 12h18 M5 12a1 1 0 1 0 2 0a1 1 0 1 0-2 0z M11 12a1 1 0 1 0 2 0a1 1 0 1 0-2 0z M17 12a1 1 0 1 0 2 0a1 1 0 1 0-2 0z",
        "headset": "M3 14v-2a9 9 0 0 1 18 0v2 M3 14h3v6H3z M18 14h3v6h-3z",
        "motherboard": "M3 3h18v18H3z M7 7h4v4H7z M15 7h2 M15 11h2 M7 15h10",
        "check": "M5 12l5 5L20 7",
        "alert": "M12 3 2 20h20L12 3z M12 10v4 M12 17h.01",
        "refresh": "M21 12a9 9 0 1 1-3-6.7 M21 4v5h-5",
        "plus": "M12 5v14 M5 12h14",
        "trash": "M3 6h18 M8 6V4h8v2 M6 6l1 14h10l1-14",
        "upload": "M12 15V3 M7 8l5-5 5 5 M4 15v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4",
        "download": "M12 3v12 M7 10l5 5 5-5 M4 15v4a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-4",
        "copy": "M9 9h12v12H9z M5 15H3V3h12v2",
        "edit": "M4 20h4L20 8l-4-4L4 16v4z",
        "power": "M12 2v10 M6.3 6.3a8 8 0 1 0 11.4 0",
        "droplet": "M12 2.7 6.3 9a8 8 0 1 0 11.4 0L12 2.7z",
        "thermo": "M14 14.8V4a2 2 0 0 0-4 0v10.8a4 4 0 1 0 4 0z",
        "image": "M3 3h18v18H3z M8.5 10a1.5 1.5 0 1 0 0-3a1.5 1.5 0 1 0 0 3z M21 15l-5-5L5 21",
        "film": "M3 3h18v18H3z M7 3v18 M17 3v18 M3 8h4 M3 16h4 M17 8h4 M17 16h4",
        "sync": "M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3",
        "close": "M18 6 6 18 M6 6l12 12",
        "gauge": "M12 14l4-4 M3.3 19a10 10 0 1 1 17.4 0",
        "eye": "M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z",
        "info": "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 11v5 M12 8h.01",
        "rotate": "M3 12a9 9 0 0 1 15.5-6.2L21 8 M21 3v5h-5",
        "sparkle": "M12 3l1.9 5.6L19.5 10l-5.6 1.9L12 17.5l-1.9-5.6L4.5 10l5.6-1.4z M19 17l.7 2.3L22 20l-2.3.7L19 23l-.7-2.3L16 20l2.3-.7z"
    })

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.stroke
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || root.paths["chip"] }
        }
    }
}
