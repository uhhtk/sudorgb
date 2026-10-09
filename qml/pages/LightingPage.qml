import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc
import Orkc.Backend

Item {
    id: page
    property var window

    // "all" | "kraken:ring" | "kraken:fans" | <device index>
    property var selection: "all"
    property var dev: ({})              // snapshot of the selected device's model roles
    readonly property bool isDevice: typeof selection === "number"
    readonly property bool isKraken: typeof selection === "string" && selection.indexOf("kraken:") === 0
    readonly property string krakenChannel: isKraken ? selection.split(":")[1] : ""
    readonly property var krakenChannels: (Kraken.capabilities.lighting || [])

    // ---- "all devices" state
    property string allEffect: "static"
    property int allBrightness: 100
    property int allSpeed: 50
    property bool syncKraken: true
    property color allColor: "#ff7a29"

    // ---- Kraken channel editor state
    property string kMode: "fixed"
    property var kColors: ["#ff7a29"]
    property int kColorIndex: 0
    property int kBrightness: 100
    property string kSpeed: "normal"

    // ---- device editor state
    property int modeColorIndex: -1      // >=0 when editing a mode-specific colour

    function loadKraken() {
        const cfg = (Kraken.desired.lighting || {})[krakenChannel]
        kMode = cfg ? cfg.mode : "fixed"
        kColors = cfg && cfg.colors && cfg.colors.length ? cfg.colors.slice() : ["#ff7a29"]
        kBrightness = cfg && cfg.brightness !== undefined ? cfg.brightness : 100
        kSpeed = cfg ? (cfg.speed || "normal") : "normal"
        kColorIndex = 0
        wheel.setColor(kColors[0])
    }
    onSelectionChanged: {
        modeColorIndex = -1
        if (isKraken) loadKraken()
        else if (selection === "all") wheel.setColor(allColor)
        else if (dev && dev.primaryColor) wheel.setColor(dev.primaryColor)
    }

    // Kraken updates go out on every move: the service paces them to what the
    // cooler can display and always lands the newest colour (no queueing).
    function sendKraken() {
        const ch = isKraken ? krakenChannel : "all"
        Kraken.setLighting(ch, { mode: kMode, colors: kColors, brightness: kBrightness, speed: kSpeed })
    }
    function krakenModeFor(effect) {
        return effect === "rainbow" ? "spectrum" : effect === "breathing" ? "breathing" : effect === "off" ? "off" : "fixed"
    }
    function applyAll(final) {
        Rgb.applyEffectAll(allEffect, [allColor], allSpeed, allBrightness)
        // Not gated on Kraken.ready: if the cooler is (re)connecting, the service
        // stores the effect and applies it the moment it is ready.
        if (syncKraken && Kraken.state !== "disabled") {
            Kraken.setLighting("all", { mode: krakenModeFor(allEffect), colors: [allColor.toString()], brightness: allBrightness,
                                        speed: allSpeed < 34 ? "slow" : allSpeed > 66 ? "fast" : "normal" })
        }
    }
    Timer { id: allThrottle; interval: 60; onTriggered: page.applyAll(false) }

    function onWheel(c, final) {
        if (selection === "all") {
            allColor = c
            if (allEffect === "static" || allEffect === "breathing") { if (final) applyAll(true); else if (!allThrottle.running) allThrottle.start() }
        } else if (isKraken) {
            const cols = kColors.slice()
            cols[kColorIndex] = c.toString()
            kColors = cols
            sendKraken()
        } else if (isDevice) {
            if (modeColorIndex >= 0) {
                const cols = (dev.modes[dev.activeMode].colors || []).map(x => x.toString())
                cols[modeColorIndex] = c.toString()
                if (final || !devModeThrottle.running) { Rgb.setDeviceMode(selection, dev.activeMode, { colors: cols }); devModeThrottle.start() }
            } else {
                Rgb.setDeviceColor(selection, c)
            }
        }
    }
    Timer { id: devModeThrottle; interval: 120 }
    Component.onCompleted: wheel.setColor(allColor)

    RowLayout {
        anchors.fill: parent
        anchors.margins: 32
        anchors.topMargin: 28
        spacing: 20

        // ------------------------------------------------ device list
        ColumnLayout {
            Layout.preferredWidth: 300
            Layout.maximumWidth: 320
            Layout.fillHeight: true
            spacing: 14

            SectionHeader { title: "RGB Lighting"; subtitle: Rgb.connected ? Rgb.controllableCount + " OpenRGB devices" : "OpenRGB offline"; Layout.fillWidth: true }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border

                ListView {
                    id: list
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 4
                    boundsBehavior: Flickable.StopAtBounds
                    header: Column {
                        width: list.width
                        spacing: 4
                        DeviceEntry {
                            width: parent.width
                            title: "All devices"
                            subtitle: "Sync every device"
                            iconName: "sync"
                            selected: page.selection === "all"
                            swatch: page.allColor
                            onClicked: page.selection = "all"
                        }
                        Repeater {
                            model: Kraken.state !== "disabled" ? page.krakenChannels : []
                            delegate: DeviceEntry {
                                required property var modelData
                                width: parent.width
                                title: modelData === "ring" ? "Kraken pump ring" : "Kraken fans"
                                subtitle: Kraken.ready ? (Kraken.deviceName || "Kraken") : "Kraken " + Kraken.state
                                iconName: "fan"
                                selected: page.selection === "kraken:" + modelData
                                dim: !Kraken.ready
                                swatch: {
                                    const c = (Kraken.desired.lighting || {})[modelData]
                                    return c && c.colors && c.colors.length ? c.colors[0] : "#22252f"
                                }
                                onClicked: page.selection = "kraken:" + modelData
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: Theme.border; visible: Rgb.devices.count > 0 }
                    }
                    model: Rgb.devices
                    delegate: DeviceEntry {
                        id: entry
                        required property int index
                        required property string name
                        required property string typeName
                        required property string typeIcon
                        required property var modes
                        required property int activeMode
                        required property string activeModeName
                        required property var zones
                        required property var colors
                        required property color primaryColor
                        required property bool perLed
                        required property bool supportsBrightness
                        required property int brightness
                        required property bool krakenOwned
                        required property int ledCount
                        width: list.width
                        title: name
                        subtitle: krakenOwned ? "Managed by Kraken service" : typeName + " · " + activeModeName
                        iconName: typeIcon
                        swatch: primaryColor
                        dim: krakenOwned
                        selected: page.selection === index
                        onClicked: if (!krakenOwned) page.selection = index
                        Binding {
                            when: entry.selected
                            target: page
                            property: "dev"
                            value: ({ name: entry.name, typeName: entry.typeName, modes: entry.modes, activeMode: entry.activeMode,
                                      zones: entry.zones, colors: entry.colors, primaryColor: entry.primaryColor, perLed: entry.perLed,
                                      supportsBrightness: entry.supportsBrightness, brightness: entry.brightness, ledCount: entry.ledCount })
                        }
                    }
                    footer: Item {
                        width: list.width
                        height: Rgb.connected ? 0 : offline.implicitHeight + 24
                        Text {
                            id: offline
                            visible: !Rgb.connected
                            anchors.fill: parent
                            anchors.margins: 12
                            text: Rgb.statusText
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint
                            font.family: Theme.font
                            font.pixelSize: Theme.fsSmall
                        }
                    }
                }
            }
        }

        // ------------------------------------------------ editor
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border

            ScrollView {
                id: editorScroll
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: editorScroll.availableWidth
                    spacing: 20

                    // header
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: page.selection === "all" ? "All devices" : page.isKraken
                                      ? (Kraken.deviceName || "Kraken") + (page.krakenChannel === "ring" ? " · Pump ring" : " · Fans")
                                      : (page.dev.name || "")
                                color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsH1 - 2; font.weight: Font.Bold
                                elide: Text.ElideRight; Layout.fillWidth: true
                            }
                            Text {
                                text: page.selection === "all" ? "Applies to every RGB device" + (page.syncKraken && page.krakenChannels.length ? " and the Kraken" : "")
                                      : page.isKraken ? "Host-driven effects · the cooler accepts about one frame per second"
                                      : page.dev.typeName + " · " + page.dev.ledCount + " LEDs · " + (page.dev.perLed ? "per-LED colour" : "hardware effect")
                                color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                            }
                        }
                        StatusPill {
                            visible: page.isKraken && !Kraken.ready
                            text: "Kraken " + Kraken.state + " — changes apply when it connects"
                            kind: "warn"
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: editorScroll.availableWidth > 760 ? 2 : 1
                        columnSpacing: 32
                        rowSpacing: 24

                        // ---- colour picker column
                        ColumnLayout {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 300
                            spacing: 16
                            visible: !(page.isKraken && (page.kMode === "spectrum" || page.kMode === "off"))
                                     && !(page.selection === "all" && (page.allEffect === "rainbow" || page.allEffect === "off"))

                            ColorWheel {
                                id: wheel
                                Layout.preferredWidth: 260
                                Layout.preferredHeight: 260
                                Layout.alignment: Qt.AlignHCenter
                                onMoved: (c) => page.onWheel(c, false)
                                onCommitted: (c) => page.onWheel(c, true)
                            }
                            AccentSlider {
                                Layout.fillWidth: true
                                label: "Value"
                                from: 0; to: 100
                                value: Math.round(wheel.value * 100)
                                trackGradient: ["#000000", Qt.hsva(wheel.hue, wheel.saturation, 1, 1)]
                                onMoved: { wheel.value = value / 100; page.onWheel(wheel.color, false) }
                                onCommitted: page.onWheel(wheel.color, true)
                            }
                            RowLayout {
                                spacing: 10
                                Rectangle { width: 36; height: 36; radius: 10; color: wheel.color; border.color: Theme.borderStrong }
                                FieldInput {
                                    id: hex
                                    Layout.preferredWidth: 110
                                    text: wheel.color.toString().toUpperCase()
                                    font.family: Theme.mono
                                    validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{6}/ }
                                    onAccepted: { const t = text.startsWith("#") ? text : "#" + text; wheel.setColor(t); page.onWheel(wheel.color, true) }
                                }
                                Text {
                                    text: "R " + Math.round(wheel.color.r * 255) + "  G " + Math.round(wheel.color.g * 255) + "  B " + Math.round(wheel.color.b * 255)
                                    color: Theme.textDim; font.family: Theme.mono; font.pixelSize: Theme.fsSmall
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: Theme.colorPresets
                                    delegate: Swatch {
                                        required property var modelData
                                        size: 24
                                        swatch: modelData
                                        onClicked: { wheel.setColor(modelData); page.onWheel(wheel.color, true) }
                                    }
                                }
                            }
                        }

                        // ---- options column
                        ColumnLayout {
                            Layout.alignment: Qt.AlignTop
                            Layout.fillWidth: true
                            spacing: 18

                            // ===== ALL DEVICES
                            ColumnLayout {
                                visible: page.selection === "all"
                                spacing: 16
                                Layout.fillWidth: true
                                Label2 { text: "Effect" }
                                Segmented {
                                    model: [{ value: "static", label: "Static" }, { value: "rainbow", label: "Rainbow" },
                                            { value: "breathing", label: "Breathing" }, { value: "off", label: "Off" }]
                                    current: page.allEffect
                                    onActivated: (v) => { page.allEffect = v; page.applyAll(true) }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                                    text: page.allEffect === "rainbow" ? "Uses each device's built-in rainbow where it has one. Devices without one (e.g. Commander Core, Apex Pro) are animated by ORKC while it runs."
                                        : page.allEffect === "breathing" ? "Uses each device's built-in breathing where it has one; ORKC animates the rest while it runs."
                                        : page.allEffect === "off" ? "Uses each device's Off mode, or sets its LEDs to black." : "Every LED set to one colour."
                                }
                                AccentSlider {
                                    Layout.fillWidth: true
                                    label: "Brightness"
                                    value: page.allBrightness
                                    onCommitted: (v) => { page.allBrightness = v; page.applyAll(true) }
                                }
                                AccentSlider {
                                    Layout.fillWidth: true
                                    visible: page.allEffect === "rainbow" || page.allEffect === "breathing"
                                    label: "Speed"
                                    value: page.allSpeed
                                    onCommitted: (v) => { page.allSpeed = v; page.applyAll(true) }
                                }
                                Toggle {
                                    Layout.fillWidth: true
                                    visible: page.krakenChannels.length > 0
                                    label: "Include Kraken ring & fans"
                                    hint: "Mirrors the effect onto the cooler through the Kraken service"
                                    checked: page.syncKraken
                                    onToggled: page.syncKraken = checked
                                }
                                PrimaryButton { text: "Apply to all"; iconName: "check"; onClicked: page.applyAll(true) }
                            }

                            // ===== KRAKEN CHANNEL
                            ColumnLayout {
                                visible: page.isKraken
                                spacing: 16
                                Layout.fillWidth: true
                                Label2 { text: "Effect" }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Segmented {
                                        model: {
                                            const fx = Kraken.capabilities.effects || {}
                                            return ["fixed", "gradient", "breathing", "cycle", "spectrum", "off"].filter(k => fx[k]).map(k => ({ value: k, label: fx[k].label }))
                                        }
                                        current: page.kMode
                                        onActivated: (v) => {
                                            page.kMode = v
                                            const spec = (Kraken.capabilities.effects || {})[v] || { min_colors: 1, max_colors: 1 }
                                            let cols = page.kColors.slice(0, Math.max(spec.max_colors, 0))
                                            while (cols.length < spec.min_colors) cols.push(["#ff7a29", "#00e5ff", "#ff2dd4", "#ffd60a"][cols.length % 4])
                                            page.kColors = cols.length ? cols : page.kColors
                                            page.kColorIndex = 0
                                            if (page.kColors.length) wheel.setColor(page.kColors[0])
                                            page.sendKraken()
                                        }
                                    }
                                }
                                Label2 {
                                    text: "Colours"
                                    visible: ((Kraken.capabilities.effects || {})[page.kMode] || {}).max_colors > 0
                                }
                                Row {
                                    spacing: 10
                                    readonly property var spec: (Kraken.capabilities.effects || {})[page.kMode] || { min_colors: 0, max_colors: 0 }
                                    visible: spec.max_colors > 0
                                    Repeater {
                                        model: page.kColors
                                        delegate: Swatch {
                                            required property int index
                                            required property var modelData
                                            swatch: modelData
                                            size: 32
                                            selected: index === page.kColorIndex
                                            onClicked: { page.kColorIndex = index; wheel.setColor(modelData) }
                                        }
                                    }
                                    GhostButton {
                                        compact: true
                                        iconName: "plus"
                                        visible: page.kColors.length < parent.spec.max_colors
                                        onClicked: { page.kColors = page.kColors.concat([wheel.color.toString()]); page.kColorIndex = page.kColors.length - 1; page.sendKraken() }
                                    }
                                    GhostButton {
                                        compact: true
                                        iconName: "trash"
                                        visible: page.kColors.length > Math.max(1, parent.spec.min_colors)
                                        onClicked: { const c = page.kColors.slice(); c.splice(page.kColorIndex, 1); page.kColors = c; page.kColorIndex = 0; page.sendKraken() }
                                    }
                                }
                                AccentSlider {
                                    Layout.fillWidth: true
                                    visible: page.kMode !== "off"
                                    label: "Brightness"
                                    value: page.kBrightness
                                    onCommitted: (v) => { page.kBrightness = v; page.sendKraken() }
                                }
                                Label2 { text: "Speed"; visible: ((Kraken.capabilities.effects || {})[page.kMode] || {}).animated === true }
                                Segmented {
                                    visible: ((Kraken.capabilities.effects || {})[page.kMode] || {}).animated === true
                                    model: [{ value: "slow", label: "Slow" }, { value: "normal", label: "Normal" }, { value: "fast", label: "Fast" }]
                                    current: page.kSpeed
                                    onActivated: (v) => { page.kSpeed = v; page.sendKraken() }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                                    text: "The 2023/2024 Kraken firmware rejects hardware effects, so every effect is streamed from the host and tuned to look smooth at about 1 frame per second. The pump ring is repainted automatically after LCD uploads."
                                }
                            }

                            // ===== OPENRGB DEVICE
                            ColumnLayout {
                                visible: page.isDevice
                                spacing: 16
                                Layout.fillWidth: true
                                Label2 { text: "Mode" }
                                DropdownField {
                                    Layout.fillWidth: true
                                    model: page.dev.modes || []
                                    textRole: "name"
                                    currentIndex: page.dev.activeMode !== undefined ? page.dev.activeMode : -1
                                    onActivated: (i) => { page.modeColorIndex = -1; Rgb.setDeviceMode(page.selection, i, {}) }
                                }
                                readonly property var mode: page.dev.modes && page.dev.activeMode >= 0 ? page.dev.modes[page.dev.activeMode] : null

                                AccentSlider {
                                    Layout.fillWidth: true
                                    visible: page.dev.supportsBrightness === true
                                    label: parent.mode && parent.mode.hasBrightness ? "Brightness" : "Brightness (software)"
                                    value: page.dev.brightness || 0
                                    onCommitted: (v) => Rgb.setDeviceBrightness(page.selection, v)
                                }
                                AccentSlider {
                                    Layout.fillWidth: true
                                    visible: parent.mode !== null && parent.mode.hasSpeed
                                    label: "Speed"
                                    readonly property var m: parent.mode
                                    value: m && m.speedMax !== m.speedMin ? Math.round(100 * (m.speed - m.speedMin) / (m.speedMax - m.speedMin)) : 50
                                    onCommitted: (v) => Rgb.setDeviceMode(page.selection, page.dev.activeMode, { speed: v })
                                }
                                Label2 { text: "Direction"; visible: parent.mode !== null && parent.mode.hasDirection }
                                Segmented {
                                    visible: parent.mode !== null && parent.mode.hasDirection
                                    small: true
                                    model: [{ value: 0, label: "Left" }, { value: 1, label: "Right" }, { value: 2, label: "Up" },
                                            { value: 3, label: "Down" }, { value: 4, label: "Horizontal" }, { value: 5, label: "Vertical" }]
                                    current: parent.mode ? parent.mode.direction : 0
                                    onActivated: (v) => Rgb.setDeviceMode(page.selection, page.dev.activeMode, { direction: v })
                                }
                                Label2 { text: "Mode colours"; visible: parent.mode !== null && parent.mode.hasModeSpecificColor && parent.mode.colors.length > 0 }
                                Row {
                                    spacing: 10
                                    visible: parent.mode !== null && parent.mode.hasModeSpecificColor && parent.mode.colors.length > 0
                                    Repeater {
                                        model: parent.parent.mode ? parent.parent.mode.colors : []
                                        delegate: Swatch {
                                            required property int index
                                            required property var modelData
                                            swatch: modelData
                                            size: 32
                                            selected: page.modeColorIndex === index
                                            onClicked: { page.modeColorIndex = index; wheel.setColor(modelData) }
                                        }
                                    }
                                }
                                Toggle {
                                    Layout.fillWidth: true
                                    visible: parent.mode !== null && parent.mode.hasRandomColor
                                    label: "Random colours"
                                    checked: parent.mode !== null && parent.mode.colorMode === 3
                                    onToggled: Rgb.setDeviceMode(page.selection, page.dev.activeMode, { random: checked })
                                }

                                Label2 { text: "Zones"; visible: (page.dev.zones || []).length > 0 }
                                Repeater {
                                    model: page.dev.zones || []
                                    delegate: Rectangle {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        height: 46
                                        radius: Theme.radiusMd
                                        color: Theme.surface2
                                        border.color: Theme.border
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 8
                                            spacing: 10
                                            Rectangle { width: 18; height: 18; radius: 5; color: modelData.color; border.color: Theme.borderStrong }
                                            Text { text: modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Text { text: modelData.ledsCount + " LEDs"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny }
                                            GhostButton {
                                                compact: true
                                                text: "Apply colour"
                                                onClicked: Rgb.setZoneColor(page.selection, modelData.index, wheel.color)
                                            }
                                        }
                                    }
                                }
                                Label2 { text: "Live LEDs" }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Repeater {
                                        model: page.dev.colors || []
                                        delegate: Rectangle { required property var modelData; width: 12; height: 12; radius: 3; color: modelData }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: page.isDevice && !Rgb.connected
                text: "OpenRGB disconnected"
                color: Theme.textFaint
                font.family: Theme.font
            }
        }
    }

    component Label2: Text {
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fsTiny
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1.2
    }

    component DeviceEntry: Rectangle {
        id: e
        property string title: ""
        property string subtitle: ""
        property string iconName: "chip"
        property color swatch: "transparent"
        property bool selected: false
        property bool dim: false
        signal clicked()
        height: 58
        radius: Theme.radiusMd
        color: selected ? Theme.accentSoft : (m.containsMouse ? Theme.surface2 : "transparent")
        border.color: selected ? Theme.accentLine : "transparent"
        opacity: dim ? 0.55 : 1
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 12
            spacing: 12
            Rectangle {
                width: 36; height: 36; radius: 10
                color: Theme.surface2
                border.color: Theme.border
                Icon { anchors.centerIn: parent; name: e.iconName; size: 18; color: e.selected ? Theme.accent : Theme.textDim }
            }
            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true
                Text { text: e.title; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                Text { text: e.subtitle; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            Rectangle { width: 14; height: 14; radius: 7; color: e.swatch; border.color: Theme.borderStrong }
        }
        MouseArea { id: m; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: e.clicked() }
    }
}
