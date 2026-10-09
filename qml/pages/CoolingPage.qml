import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc
import Orkc.Backend

ScrollView {
    id: page
    property var window
    contentWidth: availableWidth
    readonly property var cooling: Kraken.desired.cooling || ({})
    readonly property int pumpFloor: Kraken.capabilities.pump_duty_min || 20

    readonly property var presets: ({
        quiet: { pump: [[20, 40], [35, 50], [42, 70], [48, 90], [52, 100]], fan: [[20, 25], [35, 30], [42, 50], [48, 75], [52, 100]] },
        balanced: { pump: [[20, 50], [33, 60], [40, 75], [46, 90], [50, 100]], fan: [[20, 30], [33, 40], [40, 60], [45, 80], [50, 100]] },
        performance: { pump: [[20, 70], [30, 80], [38, 90], [44, 100]], fan: [[20, 45], [30, 55], [38, 75], [44, 100]] }
    })

    ColumnLayout {
        width: page.availableWidth
        spacing: 20

        SectionHeader {
            Layout.fillWidth: true
            Layout.margins: 32
            Layout.bottomMargin: 0
            title: "Cooling"
            subtitle: "Curves run in the cooler's firmware against liquid temperature, so they keep working even if this app closes."
        }
        ConflictBanner { Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32; onFixOpenRgb: if (page.window) page.window.go(6) }

        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            title: "Live"
            RowLayout {
                Layout.fillWidth: true
                spacing: 36
                Readout { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; label: "Liquid"; value: Kraken.liquidTemp; display: Theme.temp(Kraken.liquidTemp); unit: Theme.tempUnit
                    from: 20; to: 60; tone: Theme.seriesLiquid; valueSize: 32; unavailableText: Kraken.ready ? "not reported" : "Kraken unavailable" }
                Readout { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; label: "Pump"; value: Kraken.pumpRpm; unit: "rpm"; from: 0; to: 3000; tone: Theme.seriesPump; valueSize: 32
                    sub: Theme.isNum(Kraken.pumpDuty) ? Kraken.pumpDuty + "% duty" : ""; unavailableText: Kraken.ready ? "not reported" : "Kraken unavailable" }
                Readout { Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; label: "Fans"; value: Kraken.fanRpm; unit: "rpm"; from: 0; to: 2000; tone: Theme.seriesFan; valueSize: 32
                    sub: Theme.isNum(Kraken.fanDuty) ? Kraken.fanDuty + "% duty" : ""; unavailableText: Kraken.ready ? "no fans reported" : "Kraken unavailable" }
            }
        }

        Repeater {
            model: [
                { ch: "pump", title: "Pump", sub: "Minimum " + page.pumpFloor + "% (firmware limit)", floor: page.pumpFloor, tone: Theme.seriesPump,
                  rpm: Kraken.pumpRpm, duty: Kraken.pumpDuty },
                { ch: "fan", title: "Radiator fans", sub: "0% allowed — fans may stop at low temperature", floor: 0, tone: Theme.seriesFan,
                  rpm: Kraken.fanRpm, duty: Kraken.fanDuty }
            ]
            delegate: Card {
                id: card
                required property var modelData
                readonly property var cfg: page.cooling[modelData.ch] || ({ mode: "curve", points: page.presets.balanced[modelData.ch] })
                property string mode: cfg.mode
                property var points: cfg.points || page.presets.balanced[modelData.ch]
                property int duty: cfg.duty !== undefined ? cfg.duty : 60
                readonly property bool dirty: mode !== cfg.mode || (mode === "curve" && JSON.stringify(points) !== JSON.stringify(cfg.points))
                                              || (mode === "fixed" && duty !== cfg.duty)
                function apply() {
                    Kraken.setCooling(modelData.ch, mode === "fixed" ? { mode: "fixed", duty: duty } : { mode: "curve", points: points })
                }
                Layout.fillWidth: true
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                title: modelData.title
                subtitle: modelData.sub
                icon: modelData.ch === "pump" ? "droplet" : "fan"
                trailing: Segmented {
                    small: true
                    model: [{ value: "curve", label: "Curve" }, { value: "fixed", label: "Fixed" }]
                    current: card.mode
                    onActivated: (v) => card.mode = v
                }

                RowLayout {
                    visible: card.mode === "curve"
                    spacing: 8
                    Text { text: "PRESET"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                    Repeater {
                        model: ["quiet", "balanced", "performance"]
                        delegate: GhostButton {
                            required property var modelData
                            compact: true
                            text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                            onClicked: card.points = page.presets[modelData][card.modelData.ch]
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Theme.isNum(card.modelData.duty) ? "Now: " + card.modelData.duty + "% · " + (card.modelData.rpm || 0) + " RPM" : ""
                        color: Theme.textDim; font.family: Theme.mono; font.pixelSize: Theme.fsSmall
                    }
                }
                CurveEditor {
                    visible: card.mode === "curve"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 240
                    points: card.points
                    dutyFloor: card.modelData.floor
                    currentTemp: Kraken.liquidTemp
                    tone: card.modelData.tone
                    onEdited: (pts) => card.points = pts
                }
                AccentSlider {
                    visible: card.mode === "fixed"
                    Layout.fillWidth: true
                    label: "Duty"
                    from: card.modelData.floor
                    to: 100
                    value: card.duty
                    onMoved: card.duty = Math.round(value)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: card.mode === "curve" ? "Above 60 °C liquid the cooler always runs at 100%." : ""
                        color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                    }
                    GhostButton { visible: card.dirty; text: "Revert"; onClicked: { card.mode = card.cfg.mode; card.points = card.cfg.points || card.points; card.duty = card.cfg.duty || 60 } }
                    PrimaryButton { text: card.dirty ? "Apply" : "Applied"; iconName: "check"; enabled: card.dirty || !page.cooling[card.modelData.ch]; onClicked: card.apply() }
                }
            }
        }
        Item { Layout.preferredHeight: 24 }
    }
}
