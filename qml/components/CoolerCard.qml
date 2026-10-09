import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc
import Orkc.Backend

// One cooler from the cooler service (any liquidctl AIO or fan hub except the Kraken).
Card {
    id: card
    required property var device
    readonly property bool ready: device.state === "ready"
    readonly property var chans: device.channels || []
    property string channel: chans.length ? chans[0].id : ""
    readonly property var chan: chans.find(c => c.id === channel) || ({})
    readonly property var cfg: (device.speed || {})[channel] || ({ mode: "curve", points: defaultCurve })
    readonly property var defaultCurve: channel === "pump" ? [[20, 50], [33, 60], [40, 75], [46, 90], [50, 100]]
                                                           : [[20, 30], [33, 40], [40, 60], [45, 80], [50, 100]]
    property string mode: cfg.mode
    property var points: cfg.points || defaultCurve
    property int duty: cfg.duty !== undefined ? cfg.duty : 60
    onChannelChanged: { mode = cfg.mode; points = cfg.points || defaultCurve; duty = cfg.duty !== undefined ? cfg.duty : 60 }
    readonly property bool dirty: mode !== cfg.mode || (mode === "curve" && JSON.stringify(points) !== JSON.stringify(cfg.points))
                                  || (mode === "fixed" && duty !== cfg.duty)

    title: device.name
    subtitle: ready ? (Theme.isNum(device.liquid_temp) ? "Liquid " + Theme.temp(device.liquid_temp) + Theme.tempUnit : "No temperature reported")
                    : device.message
    trailing: StatusPill {
        text: card.ready ? "Connected" : device.state === "busy" ? "In use by " + (device.holders || []).join(", ")
              : device.state === "permission" ? "Needs permission" : device.state
        kind: card.ready ? "ok" : device.state === "busy" || device.state === "permission" ? "warn" : "idle"
    }

    // live per-channel readouts
    Flow {
        visible: card.chans.length > 0
        Layout.fillWidth: true
        spacing: 24
        Repeater {
            model: card.chans
            delegate: ColumnLayout {
                required property var modelData
                spacing: 2
                Text { text: modelData.label.toUpperCase(); color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                Text {
                    text: (Theme.isNum(modelData.rpm) ? modelData.rpm + " rpm" : "—") + (Theme.isNum(modelData.duty) ? " · " + modelData.duty + "%" : "")
                    color: Theme.text; font.family: Theme.mono; font.pixelSize: Theme.fsBody
                }
            }
        }
    }

    ColumnLayout {
        visible: card.ready && card.chans.length > 0
        Layout.fillWidth: true
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Segmented {
                small: true
                model: card.chans.map(c => ({ value: c.id, label: c.label }))
                current: card.channel
                onActivated: (v) => card.channel = v
            }
            Item { Layout.fillWidth: true }
            Segmented {
                small: true
                model: [{ value: "curve", label: "Curve" }, { value: "fixed", label: "Fixed" }]
                current: card.mode
                onActivated: (v) => card.mode = v
            }
        }
        CurveEditor {
            visible: card.mode === "curve"
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            points: card.points
            dutyFloor: card.chan.floor || 0
            currentTemp: card.device.liquid_temp
            tone: card.channel === "pump" ? Theme.seriesPump : Theme.seriesFan
            onEdited: (pts) => card.points = pts
        }
        AccentSlider {
            visible: card.mode === "fixed"
            Layout.fillWidth: true
            label: card.chan.label || "Duty"
            from: card.chan.floor || 0
            to: 100
            value: card.duty
            onMoved: card.duty = Math.round(value)
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                text: card.mode !== "curve" ? (card.channel === "pump" ? "Pumps never go below " + (card.chan.floor || 20) + "%." : "")
                      : card.chan.curve === "software" ? "This cooler has no hardware curves: SudoRGB sets the speed from liquid temperature every 2 s while it runs."
                      : card.chan.curve === "hardware" ? "Runs in the cooler's firmware, also when SudoRGB is closed." : ""
            }
            PrimaryButton {
                text: card.dirty ? "Apply" : "Applied"
                iconName: "check"
                enabled: card.dirty || !(card.device.speed || {})[card.channel]
                onClicked: Coolers.setSpeed(card.device.id, card.channel,
                                            card.mode === "fixed" ? { mode: "fixed", duty: card.duty } : { mode: "curve", points: card.points })
            }
        }
    }
    Text {
        visible: !card.ready && card.device.state === "busy"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsSmall
        text: "To control it here, stop the other program from using it (in OpenRGB: Settings → Supported Devices, untick this controller, then restart OpenRGB). SudoRGB picks it up within 10 s."
    }
}
