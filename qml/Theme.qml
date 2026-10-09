pragma Singleton
import QtQuick
import Orkc.Backend

// "Graphite" design language: warm neutral darks (no blue tint), hairline
// borders, tight radii, instrument-style readouts, one user-chosen accent.
QtObject {
    readonly property color bg: "#0b0b0a"
    readonly property color header: "#0e0e0d"
    readonly property color surface: "#141413"
    readonly property color surface2: "#1b1b19"
    readonly property color surface3: "#262623"
    readonly property color border: "#262624"
    readonly property color borderStrong: "#37362f"
    readonly property color text: "#f3f1ea"
    readonly property color textDim: "#aaa79c"
    readonly property color textFaint: "#6e6c63"

    // Accent comes from settings so the whole app re-themes live
    readonly property color accent: AppSettings.accentColor
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, 0.12)
    readonly property color accentLine: Qt.rgba(accent.r, accent.g, accent.b, 0.50)
    readonly property color onAccent: (accent.r * 0.299 + accent.g * 0.587 + accent.b * 0.114) > 0.55 ? "#0b0b0a" : "#ffffff"

    // Status
    readonly property color ok: "#8bd450"
    readonly property color warn: "#f2c230"
    readonly property color danger: "#ff5a4f"
    readonly property color info: "#5ab8e8"
    readonly property color sim: "#e8b7f0"

    // Data series (fixed, independent of accent)
    readonly property color seriesLiquid: "#4cc9e0"
    readonly property color seriesPump: "#3ed1b4"
    readonly property color seriesFan: "#b7e35a"
    readonly property color seriesCpu: "#ff9a4d"

    readonly property int radius: 10
    readonly property int radiusMd: 8
    readonly property int radiusSm: 6
    readonly property int pad: 20

    readonly property string font: "Noto Sans"
    readonly property string mono: "JetBrainsMono Nerd Font"
    readonly property int fsDisplay: 44
    readonly property int fsH1: 26
    readonly property int fsH2: 15
    readonly property int fsBody: 13
    readonly property int fsSmall: 12
    readonly property int fsTiny: 10

    readonly property int durFast: AppSettings.reduceMotion ? 0 : 120
    readonly property int dur: AppSettings.reduceMotion ? 0 : 220
    readonly property int durSlow: AppSettings.reduceMotion ? 0 : 420

    readonly property var accentPresets: ["#ff7a29", "#f5b82e", "#b7e35a", "#3ddc97", "#22d3c5", "#3fa9f5", "#ff5f6d", "#d6d3cc"]
    // LED colour shortcuts (hardware colours, not UI colours)
    readonly property var colorPresets: ["#ffffff", "#ff1f3d", "#ff6a00", "#ffd60a", "#3ddc97", "#00e5ff", "#0a84ff", "#7a3cff", "#ff2dd4", "#000000"]

    function isNum(v) { return v !== undefined && v !== null && !isNaN(v) }

    function temp(v, digits) {
        if (!isNum(v)) return "—"
        const d = digits === undefined ? 1 : digits
        return AppSettings.fahrenheit ? (v * 9 / 5 + 32).toFixed(d) : Number(v).toFixed(d)
    }
    readonly property string tempUnit: AppSettings.fahrenheit ? "°F" : "°C"

    function tempColor(v, warnAt, critAt) {
        if (!isNum(v)) return textFaint
        if (v >= critAt) return danger
        if (v >= warnAt) return warn
        return ok
    }

    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
