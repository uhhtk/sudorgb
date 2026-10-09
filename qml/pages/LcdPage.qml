import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Orkc
import Orkc.Backend

ScrollView {
    id: page
    property var window
    contentWidth: availableWidth

    readonly property var lcd: Kraken.desired.lcd || ({})
    readonly property var modes: Kraken.capabilities.lcd_modes || ["liquid", "image", "gif", "off"]
    readonly property var styles: Kraken.capabilities.sensor_styles || []

    // Draft (what the user is composing; applied with the button)
    property string mode: lcd.mode || "liquid"
    property string path: lcd.path || ""
    property string fit: lcd.fit || "cover"
    property string sensorStyle: lcd.sensor_style || "liquid_ring"
    property color ringColor: lcd.ring_color || Theme.accent
    property int orientation: lcd.orientation || 0
    readonly property bool needsFile: mode === "image" || mode === "gif"
    readonly property bool dirty: mode !== (lcd.mode || "liquid") || path !== (lcd.path || "") || fit !== (lcd.fit || "cover")
                                  || sensorStyle !== (lcd.sensor_style || "liquid_ring") || orientation !== (lcd.orientation || 0)
                                  || ringColor.toString() !== Qt.color(lcd.ring_color || Theme.accent).toString()

    function apply() {
        const cfg = { mode: mode, brightness: brightness.value, orientation: orientation, fit: fit,
                      sensor_style: sensorStyle, ring_color: ringColor.toString() }
        if (needsFile) cfg.path = path
        Kraken.setLcd(cfg)
    }

    FileDialog {
        id: picker
        title: page.mode === "gif" ? "Choose an animated GIF" : "Choose an image"
        nameFilters: page.mode === "gif" ? ["Animated GIF (*.gif)", "Animated WebP (*.webp)"] : ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"]
        onAccepted: page.path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""))
    }
    ConfirmDialog {
        id: clearDialog
        parent: Overlay.overlay
        title: "Clear LCD memory?"
        message: "Deletes all images and GIFs stored in the cooler (the firmware replays them at boot before any software runs). Your current setting is re-applied afterwards."
        confirmText: "Clear memory"
        destructive: true
        onAccepted: Kraken.clearLcdMedia()
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 20

        SectionHeader {
            Layout.fillWidth: true
            Layout.margins: 32
            Layout.bottomMargin: 0
            title: "Kraken LCD"
            subtitle: Kraken.ready ? (Kraken.deviceInfo.lcd_resolution ? Kraken.deviceInfo.lcd_resolution.join("×") + " round display" : "Round display")
                                   : "Kraken " + Kraken.state + " — settings are stored and applied when it connects"
            GhostButton { text: "Clear memory"; iconName: "trash"; enabled: Kraken.ready && !Kraken.lcdBusy; onClicked: clearDialog.open() }
        }

        ConflictBanner { Layout.fillWidth: true; Layout.leftMargin: 32; Layout.rightMargin: 32; onFixOpenRgb: if (page.window) page.window.go(5) }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            spacing: 20

            // ------------------------------------------------ preview
            Card {
                Layout.preferredWidth: 400
                Layout.alignment: Qt.AlignTop
                title: "On the cooler"
                subtitle: Kraken.lcdMessage !== "" ? Kraken.lcdMessage : "Live preview of the uploaded content"
                LcdPreview {
                    Layout.preferredWidth: 340
                    Layout.preferredHeight: 340
                    Layout.alignment: Qt.AlignHCenter
                    mode: Kraken.lcdMode
                    source: Kraken.lcdPreview
                    liquidTemp: Kraken.liquidTemp
                    busy: Kraken.lcdBusy
                    caption: Kraken.lcdMessage
                    ringColor: page.ringColor
                }
                AccentSlider {
                    id: brightness
                    Layout.fillWidth: true
                    label: "Brightness"
                    value: page.lcd.brightness !== undefined ? page.lcd.brightness : 70
                    enabled: page.mode !== "off"
                    onCommitted: (v) => Kraken.setLcdBrightness(v)
                }
                Text { text: "ORIENTATION"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                Segmented {
                    model: [{ value: 0, label: "0°" }, { value: 90, label: "90°" }, { value: 180, label: "180°" }, { value: 270, label: "270°" }]
                    current: page.orientation
                    onActivated: (v) => page.orientation = v
                }
            }

            // ------------------------------------------------ content
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 16

                Card {
                    Layout.fillWidth: true
                    title: "Display content"
                    GridLayout {
                        Layout.fillWidth: true
                        columns: width > 620 ? 3 : 2
                        columnSpacing: 12
                        rowSpacing: 12
                        Repeater {
                            model: [
                                { key: "liquid", label: "Liquid temperature", desc: "Firmware screen, zero CPU", icon: "droplet" },
                                { key: "sensors", label: "System sensors", desc: "Liquid, CPU & GPU, live", icon: "gauge" },
                                { key: "image", label: "Image", desc: "PNG, JPEG, WebP", icon: "image" },
                                { key: "gif", label: "Animated GIF", desc: "Optimised before upload", icon: "film" },
                                { key: "off", label: "Off", desc: "Backlight off", icon: "power" }
                            ].filter(m => page.modes.indexOf(m.key) >= 0)
                            delegate: Rectangle {
                                required property var modelData
                                readonly property bool sel: page.mode === modelData.key
                                Layout.fillWidth: true
                                height: 76
                                radius: Theme.radiusMd
                                color: sel ? Theme.accentSoft : (mm.containsMouse ? Theme.surface2 : Theme.bg)
                                border.color: sel ? Theme.accentLine : Theme.border
                                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    spacing: 12
                                    Icon { name: modelData.icon; size: 22; color: parent.parent.sel ? Theme.accent : Theme.textDim }
                                    ColumnLayout {
                                        spacing: 2
                                        Layout.fillWidth: true
                                        Text { text: modelData.label; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { text: modelData.desc; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; elide: Text.ElideRight; Layout.fillWidth: true }
                                    }
                                }
                                MouseArea { id: mm; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.mode = modelData.key }
                            }
                        }
                    }
                }

                // ---- file
                Card {
                    Layout.fillWidth: true
                    visible: page.needsFile
                    title: page.mode === "gif" ? "Animation" : "Image"
                    subtitle: "Fitted to the round panel, not stretched"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Item {
                            width: 132; height: 132
                            LcdPreview {
                                anchors.fill: parent
                                showBezel: false
                                mode: page.path ? (page.mode === "gif" ? "gif" : "image") : "off"
                                source: page.path ? "file://" + page.path.split("/").map(encodeURIComponent).join("/") : ""
                                fillMode: page.fit === "contain" ? Image.PreserveAspectFit : page.fit === "stretch" ? Image.Stretch : Image.PreserveAspectCrop
                            }
                            Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.color: Theme.borderStrong }
                            Text { anchors.centerIn: parent; visible: !page.path; text: "No file"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Text {
                                text: page.path ? page.path.split("/").pop() : "Choose a file to upload"
                                color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold
                                elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                            Text {
                                visible: page.path !== ""
                                text: page.path
                                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny
                                elide: Text.ElideMiddle; Layout.fillWidth: true
                            }
                            GhostButton { text: page.path ? "Change file…" : "Choose file…"; iconName: "upload"; onClicked: picker.open() }
                            Segmented {
                                model: [{ value: "cover", label: "Fill circle" }, { value: "contain", label: "Fit whole" }, { value: "stretch", label: "Stretch" }]
                                current: page.fit
                                small: true
                                onActivated: (v) => page.fit = v
                            }
                        }
                    }
                    Text {
                        visible: page.mode === "gif"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                        text: "GIFs are re-encoded in an isolated, memory-limited process: one shared palette, unchanged pixels stored as transparency, frames dropped (not slowed) if needed to stay under 8 MB. Processed files are cached, so re-applying is instant."
                    }
                }

                // ---- sensor style
                Card {
                    Layout.fillWidth: true
                    visible: page.mode === "sensors"
                    title: "Sensor screen"
                    subtitle: "Rendered by the service every 2 s"
                    Segmented {
                        model: page.styles.map(s => ({ value: s, label: s === "liquid_ring" ? "Liquid ring" : s === "cpu_gpu" ? "CPU + GPU" : s === "triple" ? "Triple" : s }))
                        current: page.sensorStyle
                        onActivated: (v) => page.sensorStyle = v
                    }
                    Text { text: "ACCENT"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: Theme.accentPresets
                            delegate: Swatch {
                                required property var modelData
                                swatch: modelData
                                selected: Qt.color(modelData).toString() === page.ringColor.toString()
                                onClicked: page.ringColor = modelData
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text {
                        Layout.fillWidth: true
                        text: page.needsFile && !page.path ? "Choose a file first." : page.dirty ? "Unsaved changes" : "Applied"
                        color: page.dirty ? Theme.warn : Theme.textFaint
                        font.family: Theme.font
                        font.pixelSize: Theme.fsSmall
                    }
                    GhostButton {
                        text: "Revert"
                        visible: page.dirty
                        onClicked: {
                            page.mode = page.lcd.mode || "liquid"; page.path = page.lcd.path || ""; page.fit = page.lcd.fit || "cover"
                            page.sensorStyle = page.lcd.sensor_style || "liquid_ring"; page.orientation = page.lcd.orientation || 0
                            page.ringColor = page.lcd.ring_color || Theme.accent
                        }
                    }
                    PrimaryButton {
                        text: Kraken.ready ? "Apply to LCD" : "Save for later"
                        iconName: "check"
                        busy: Kraken.lcdBusy
                        enabled: !(page.needsFile && !page.path)
                        onClicked: page.apply()
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 24 }
    }
}
