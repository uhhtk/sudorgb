import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc
import Orkc.Backend

ScrollView {
    id: page
    property var window
    contentWidth: availableWidth

    property var detectors: []
    function refreshDetectors() { detectors = Rgb.krakenDetectorCandidates(Kraken.deviceName || "NZXT Kraken 2024 Elite RGB") }
    Component.onCompleted: refreshDetectors()
    onVisibleChanged: if (visible) refreshDetectors()

    ConfirmDialog {
        id: detectorDialog
        parent: Overlay.overlay
        title: "Stop OpenRGB from driving the Kraken?"
        message: "This turns off the selected detectors in ~/.config/OpenRGB/OpenRGB.json (a timestamped backup is written next to it). OpenRGB must be closed. Your other RGB devices are unaffected; the Kraken's ring and fans are then controlled here through the Kraken service."
        confirmText: "Disable in OpenRGB"
        onAccepted: {
            const names = page.detectors.filter(d => d.recommended && d.enabled).map(d => d.name)
            const r = Rgb.disableOpenRgbDetectors(names)
            if (page.window) page.window.toast(r.message, !r.ok)
            page.refreshDetectors()
        }
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 20

        SectionHeader {
            Layout.fillWidth: true
            Layout.margins: 32
            Layout.bottomMargin: 0
            title: "Settings"
            subtitle: "Saved automatically to " + AppSettings.configPath
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            columns: page.availableWidth > 1000 ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16

            // ---------------------------------------------------- appearance
            Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: "Appearance"
                icon: "sparkle"
                Text { text: "ACCENT COLOUR"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: Theme.accentPresets
                        delegate: Swatch {
                            required property var modelData
                            swatch: modelData
                            size: 30
                            selected: Qt.color(modelData).toString() === AppSettings.accentColor.toString()
                            onClicked: AppSettings.accentColor = modelData
                        }
                    }
                }
                Toggle { Layout.fillWidth: true; label: "Reduce motion"; hint: "Turns off interface animations"; checked: AppSettings.reduceMotion; onToggled: AppSettings.reduceMotion = checked }
                Toggle { Layout.fillWidth: true; label: "Fahrenheit"; checked: AppSettings.fahrenheit; onToggled: AppSettings.fahrenheit = checked }
            }

            // ---------------------------------------------------- OpenRGB
            Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: "OpenRGB"
                subtitle: Rgb.connected ? "Connected · SDK protocol v" + Rgb.protocolVersion : Rgb.statusText
                icon: "lighting"
                trailing: StatusPill { text: Rgb.state; kind: Rgb.connected ? "ok" : Rgb.state === "disconnected" ? "error" : "busy" }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    FieldInput { id: host; Layout.fillWidth: true; text: AppSettings.openRgbHost; placeholderText: "127.0.0.1" }
                    FieldInput { id: port; Layout.preferredWidth: 90; text: AppSettings.openRgbPort; validator: IntValidator { bottom: 1; top: 65535 } }
                    GhostButton {
                        text: "Connect"
                        iconName: "refresh"
                        onClicked: { AppSettings.openRgbHost = host.text.trim(); AppSettings.openRgbPort = parseInt(port.text); Rgb.reconnect() }
                    }
                }
                Toggle {
                    Layout.fillWidth: true
                    label: "Start OpenRGB server automatically"
                    hint: "If nothing is listening and OpenRGB isn't running, launch “openrgb --server” bound to 127.0.0.1 only."
                    checked: AppSettings.autoStartOpenRgb
                    onToggled: AppSettings.autoStartOpenRgb = checked
                }
                RowLayout {
                    spacing: 10
                    GhostButton { text: "Start server now"; iconName: "power"; enabled: !Rgb.connected; onClicked: { if (page.window) page.window.toast(Rgb.startServer(), false) } }
                    GhostButton { text: "Rescan devices"; iconName: "refresh"; enabled: Rgb.connected; onClicked: Rgb.rescan() }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                Text { text: "KRAKEN OWNERSHIP"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                    text: Rgb.krakenInOpenRgb || page.detectors.some(d => d.recommended && d.enabled)
                          ? "OpenRGB is set to detect your Kraken. Two programs writing to the same USB HID device corrupt each other's replies (this is what breaks LCD uploads). Let the Kraken service own it:"
                          : !Rgb.openRgbConfigReadable()
                          ? "Couldn't read " + Rgb.openRgbConfigFile() + ", so OpenRGB's Kraken detectors can't be checked. If OpenRGB lists your Kraken, disable its “NZXT Kraken” detector in OpenRGB → Settings → Supported Devices."
                          : page.detectors.length === 0
                          ? "No NZXT Kraken detectors found in OpenRGB's configuration."
                          : "OpenRGB's Kraken detector is disabled. The Kraken service owns the cooler. ✓"
                }
                Repeater {
                    model: page.detectors.filter(d => d.recommended)
                    delegate: RowLayout {
                        required property var modelData
                        spacing: 8
                        Icon { name: modelData.enabled ? "alert" : "check"; size: 14; color: modelData.enabled ? Theme.warn : Theme.ok }
                        Text { text: modelData.name + (modelData.enabled ? " — enabled" : " — disabled"); color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsSmall }
                    }
                }
                PrimaryButton {
                    visible: page.detectors.some(d => d.recommended && d.enabled)
                    text: Rgb.openRgbRunning ? "Close OpenRGB first" : "Disable Kraken in OpenRGB…"
                    enabled: !Rgb.openRgbRunning
                    iconName: "settings"
                    onClicked: detectorDialog.open()
                }
            }

            // ---------------------------------------------------- Kraken service
            Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: "Kraken service"
                subtitle: Kraken.backendLabel || "Isolated Python process"
                icon: "fan"
                trailing: StatusPill {
                    text: Kraken.state
                    kind: Kraken.simulated ? "sim" : Kraken.ready ? "ok" : Kraken.state === "conflict" ? "warn"
                          : (Kraken.state === "failed" || Kraken.state === "error") ? "error" : "busy"
                }
                Toggle {
                    Layout.fillWidth: true
                    label: "Enable Kraken support"
                    hint: "Takes effect after restarting the app"
                    checked: AppSettings.krakenEnabled
                    onToggled: AppSettings.krakenEnabled = checked
                }
                Text { text: "PYTHON INTERPRETER"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                FieldInput {
                    Layout.fillWidth: true
                    text: AppSettings.pythonExecutable
                    font.family: Theme.mono
                    onEditingFinished: AppSettings.pythonExecutable = text.trim()
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny
                    text: "Run in isolated mode (-I) with PYTHONPATH and virtualenv variables removed, so a mise/pyenv Python on your PATH can't shadow the system liquidctl and Pillow."
                }
                AccentSlider {
                    Layout.fillWidth: true
                    label: "Telemetry interval"
                    suffix: " s"
                    from: 0.5; to: 5; stepSize: 0.5
                    value: AppSettings.telemetryInterval
                    onCommitted: (v) => { AppSettings.telemetryInterval = v; Kraken.setPollInterval(v) }
                }
                GridLayout {
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 4
                    Repeater {
                        model: [
                            ["Device", Kraken.deviceName || "—"],
                            ["Firmware", Kraken.firmware || "—"],
                            ["LEDs", Kraken.deviceInfo.led_counts ? "ring " + Kraken.deviceInfo.led_counts.ring + " · fans " + Kraken.deviceInfo.led_counts.fans : "—"],
                            ["Runtime", Kraken.pythonInfo || "—"],
                            ["Restarts", String(Kraken.restartCount)]
                        ]
                        delegate: Text {
                            required property var modelData
                            required property int index
                            text: modelData[0]
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fsSmall
                            Layout.row: index
                            Layout.column: 0
                        }
                    }
                    Repeater {
                        model: [Kraken.deviceName || "—", Kraken.firmware || "—",
                                Kraken.deviceInfo.led_counts ? "ring " + Kraken.deviceInfo.led_counts.ring + " · fans " + Kraken.deviceInfo.led_counts.fans : "—",
                                Kraken.pythonInfo || "—", String(Kraken.restartCount)]
                        delegate: Text {
                            required property var modelData
                            required property int index
                            text: modelData
                            color: Theme.text
                            font.family: Theme.font
                            font.pixelSize: Theme.fsSmall
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            Layout.row: index
                            Layout.column: 1
                        }
                    }
                }
                RowLayout {
                    spacing: 10
                    GhostButton { text: "Restart service"; iconName: "refresh"; onClicked: Kraken.retry() }
                }
            }

            // ---------------------------------------------------- diagnostics
            Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                title: "Diagnostics"
                subtitle: "Kraken service log (most recent last)"
                icon: "info"
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    radius: Theme.radiusMd
                    color: Theme.bg
                    border.color: Theme.border
                    ScrollView {
                        id: logScroll
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        TextArea {
                            readOnly: true
                            text: Kraken.logTail
                            color: Theme.textDim
                            font.family: Theme.mono
                            font.pixelSize: Theme.fsTiny
                            wrapMode: TextEdit.WrapAnywhere
                            selectByMouse: true
                            background: null
                            onTextChanged: logScroll.ScrollBar.vertical.position = 1.0 - logScroll.ScrollBar.vertical.size
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny
                    text: "Profiles: ~/.config/orkc/profiles · Session & Kraken state: ~/.local/state/orkc · LCD media cache: ~/.cache/orkc/lcd"
                }
            }
        }
        Item { Layout.preferredHeight: 24 }
    }
}
