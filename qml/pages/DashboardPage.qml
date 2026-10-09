import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc
import Orkc.Backend

ScrollView {
    id: page
    property var window
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    readonly property bool narrow: width < 1000
    readonly property var ringCfg: (Kraken.desired.lighting || {}).ring || ({})

    function lcdLabel(m) {
        return m === "gif" ? "Animated GIF" : m === "image" ? "Image" : m === "sensors" ? "Sensor screen"
             : m === "liquid" ? "Liquid temperature" : m === "off" ? "Display off" : m === "processing" ? "Optimising media…"
             : m === "error" ? "Upload failed" : "Not set this session"
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 18

        Item { Layout.preferredHeight: 6 }

        // ------------------------------------------------ title row
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            spacing: 14
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text { text: "Overview"; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsH1; font.weight: Font.Bold }
                Text {
                    text: (Profiles.activeName !== "" ? "Profile: " + Profiles.activeName : "No profile applied")
                          + (Kraken.ready ? "  ·  " + Kraken.deviceName : "")
                    color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsBody
                }
            }
        }

        ConflictBanner {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            onFixOpenRgb: if (page.window) page.window.go(6)
        }

        // ------------------------------------------------ hero: cooler + instruments
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            columns: page.narrow ? 1 : 2
            columnSpacing: 16
            rowSpacing: 16

            Card {
                Layout.preferredWidth: page.narrow ? -1 : 400
                Layout.fillWidth: page.narrow
                Layout.fillHeight: true
                title: "Kraken head"
                subtitle: page.lcdLabel(Kraken.lcdMode) + (page.ringCfg.mode ? "  ·  ring: " + page.ringCfg.mode : "")
                trailing: GhostButton { compact: true; iconName: "edit"; text: "LCD"; onClicked: if (page.window) page.window.go(2) }
                CoolerView {
                    Layout.preferredWidth: 320
                    Layout.preferredHeight: 320
                    Layout.alignment: Qt.AlignHCenter
                    ring: page.ringCfg
                    ledCount: (Kraken.deviceInfo.led_counts || {}).ring || 24
                    lcdMode: Kraken.lcdMode
                    lcdSource: Kraken.lcdPreview
                    liquidTemp: Kraken.liquidTemp
                    busy: Kraken.lcdBusy
                    caption: Kraken.lcdMessage
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Kraken.ready ? (Kraken.lcdMessage || " ") : Kraken.stateMessage
                    color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                    elide: Text.ElideRight
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: "Instruments"
                subtitle: "Live from the Kraken service and the kernel's hwmon sensors"
                GridLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columns: 2
                    columnSpacing: 36
                    rowSpacing: 28
                    Readout {
                        Layout.fillWidth: true
                        label: "Liquid"
                        value: Kraken.liquidTemp
                        display: Theme.temp(Kraken.liquidTemp)
                        unit: Theme.tempUnit
                        from: 20; to: 60
                        tone: Theme.isNum(Kraken.liquidTemp) && Kraken.liquidTemp >= 45 ? Theme.tempColor(Kraken.liquidTemp, 45, 52) : Theme.seriesLiquid
                        sub: Theme.isNum(Kraken.liquidTemp) ? (Kraken.liquidTemp < 40 ? "Normal range" : Kraken.liquidTemp < 50 ? "Warm" : "Hot: check cooling") : ""
                        unavailableText: Kraken.ready ? "not reported" : "Kraken unavailable"
                    }
                    Readout {
                        Layout.fillWidth: true
                        label: "CPU · " + (SystemInfo.cpuSensor || "no sensor")
                        value: SystemInfo.cpuTemp
                        display: Theme.temp(SystemInfo.cpuTemp, 0)
                        unit: Theme.tempUnit
                        from: 25; to: 95
                        tone: Theme.isNum(SystemInfo.cpuTemp) && SystemInfo.cpuTemp >= 80 ? Theme.tempColor(SystemInfo.cpuTemp, 80, 90) : Theme.seriesCpu
                        sub: Theme.isNum(SystemInfo.cpuLoad) ? Math.round(SystemInfo.cpuLoad) + "% load" : ""
                        unavailableText: "no sensor found"
                    }
                    Readout {
                        Layout.fillWidth: true
                        label: "Pump"
                        value: Kraken.pumpRpm
                        unit: "rpm"
                        from: 0; to: 3000
                        tone: Theme.seriesPump
                        sub: Theme.isNum(Kraken.pumpDuty) ? Kraken.pumpDuty + "% duty" : ""
                        unavailableText: Kraken.ready ? "not reported" : "Kraken unavailable"
                    }
                    Readout {
                        Layout.fillWidth: true
                        label: "Radiator fans"
                        value: Kraken.fanRpm
                        unit: "rpm"
                        from: 0; to: 2000
                        tone: Theme.seriesFan
                        sub: Theme.isNum(Kraken.fanDuty) ? Kraken.fanDuty + "% duty" : ""
                        unavailableText: Kraken.ready ? "no fans reported" : "Kraken unavailable"
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    spacing: 24
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 4
                        Text { text: "LIQUID HISTORY"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.6 }
                        Sparkline { Layout.fillWidth: true; Layout.fillHeight: true; values: Kraken.liquidHistory; stroke: Theme.seriesLiquid; minSpan: 3 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 4
                        Text { text: "CPU HISTORY"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.6 }
                        Sparkline { Layout.fillWidth: true; Layout.fillHeight: true; values: SystemInfo.cpuHistory; stroke: Theme.seriesCpu; minSpan: 8 }
                    }
                }
            }
        }

        // ------------------------------------------------ profiles strip
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            title: "Profiles"
            subtitle: "Lighting, LCD and cooling in one click"
            trailing: GhostButton { compact: true; text: "Manage"; onClicked: if (page.window) page.window.go(4) }
            Flow {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: Profiles.profiles
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool active: modelData.id === Profiles.activeId
                        height: 44
                        width: chipRow.implicitWidth + 28
                        radius: Theme.radiusMd
                        color: active ? Theme.accentSoft : (chipMouse.containsMouse ? Theme.surface3 : Theme.surface2)
                        border.color: active ? Theme.accentLine : Theme.border
                        Behavior on color { ColorAnimation { duration: Theme.durFast } }
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 10
                            Row {
                                spacing: 2
                                anchors.verticalCenter: parent.verticalCenter
                                Repeater {
                                    model: (modelData.swatches || []).slice(0, 4)
                                    delegate: Rectangle { required property var modelData; width: 4; height: 18; radius: 2; color: modelData }
                                }
                            }
                            Text { text: modelData.name; color: Theme.text; font.pixelSize: Theme.fsBody; font.family: Theme.font; font.weight: Font.Medium; anchors.verticalCenter: parent.verticalCenter }
                        }
                        MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Profiles.apply(modelData.id) }
                    }
                }
            }
        }

        // ------------------------------------------------ devices
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            title: "Devices"
            subtitle: Rgb.connected ? "Live state reported by OpenRGB and the Kraken service" : Rgb.statusText
            trailing: GhostButton { compact: true; iconName: "refresh"; text: "Rescan"; onClicked: Rgb.rescan() }

            GridLayout {
                Layout.fillWidth: true
                columns: Math.max(1, Math.floor((page.availableWidth - 56 - 40 + 12) / 300))
                columnSpacing: 12
                rowSpacing: 12

                DeviceTile {
                    Layout.fillWidth: true
                    visible: Kraken.state !== "disabled"
                    iconName: "fan"
                    deviceName: Kraken.deviceName || "NZXT Kraken"
                    detail: Kraken.ready ? "Kraken service" + (Kraken.firmware ? " · fw " + Kraken.firmware : "") : Kraken.stateMessage
                    modeText: page.ringCfg.mode ? "Ring: " + page.ringCfg.mode : "Lighting not set"
                    swatches: {
                        const lit = Kraken.desired.lighting || {}
                        let s = []
                        for (const ch of ["ring", "fans"]) if (lit[ch] && lit[ch].colors) s = s.concat(lit[ch].colors)
                        return s.slice(0, 24)
                    }
                    tone: Kraken.simulated ? Theme.sim : Kraken.ready ? Theme.ok : Kraken.state === "conflict" ? Theme.warn : Theme.textFaint
                }

                Repeater {
                    model: Rgb.devices
                    delegate: DeviceTile {
                        required property string name
                        required property string typeName
                        required property string typeIcon
                        required property string activeModeName
                        required property var colors
                        required property int ledCount
                        required property bool krakenOwned
                        Layout.fillWidth: true
                        iconName: typeIcon
                        deviceName: name
                        detail: typeName + " · " + ledCount + " LEDs"
                        modeText: krakenOwned ? "Skipped: owned by the Kraken service" : activeModeName
                        swatches: colors.slice(0, 32)
                        tone: krakenOwned ? Theme.warn : Theme.ok
                    }
                }
            }
            Text {
                visible: !Rgb.connected
                text: Rgb.statusText
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fsSmall
                Layout.fillWidth: true
            }
        }

        Item { Layout.preferredHeight: 20 }
    }

    component DeviceTile: Rectangle {
        id: tile
        property string iconName: "chip"
        property alias deviceName: nameText.text
        property string detail: ""
        property string modeText: ""
        property var swatches: []
        property color tone: Theme.ok
        implicitHeight: 104
        radius: Theme.radiusMd
        color: tileHover.hovered ? Theme.surface2 : Theme.bg
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        HoverHandler { id: tileHover }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                Icon { name: tile.iconName; size: 18; color: Theme.textDim }
                Text { id: nameText; color: Theme.text; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold; font.family: Theme.font; elide: Text.ElideRight; Layout.fillWidth: true }
                Rectangle { width: 7; height: 7; radius: 3.5; color: tile.tone }
            }
            Text { text: tile.detail; color: Theme.textFaint; font.pixelSize: Theme.fsTiny + 1; font.family: Theme.font; elide: Text.ElideRight; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text { text: tile.modeText; color: Theme.textDim; font.pixelSize: Theme.fsSmall; font.family: Theme.font; elide: Text.ElideRight; Layout.fillWidth: true }
                Row {
                    spacing: 2
                    Repeater {
                        model: tile.swatches
                        delegate: Rectangle { required property var modelData; width: 4; height: 14; radius: 1.5; color: modelData }
                    }
                }
            }
        }
    }
}
