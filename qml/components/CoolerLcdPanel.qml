import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Orkc
import Orkc.Backend

// LCD of a cooler-service device (Corsair ELITE / NAUTILUS / LINK LCD, MSI Coreliquid).
RowLayout {
    id: panel
    required property var device
    readonly property var lcd: device.lcd || ({})
    readonly property var caps: lcd.caps || ({})
    readonly property var want: lcd.desired || ({})
    readonly property var shown: lcd.shown || ({})
    readonly property bool animated: (caps.media || []).indexOf("animation") >= 0
    readonly property bool round: caps.size && caps.size[0] === caps.size[1]

    property string mode: want.mode || "media"
    property string path: want.path || ""
    property string fit: want.fit || "cover"
    property int rotation: want.rotation || 0
    readonly property bool dirty: mode !== (want.mode || "media") || path !== (want.path || "") || fit !== (want.fit || "cover")
                                  || rotation !== (want.rotation || 0)
    readonly property string fileUrl: path ? "file://" + path.split("/").map(encodeURIComponent).join("/") : ""

    function apply() {
        const cfg = { mode: mode }
        if (mode === "media") { cfg.path = path; cfg.fit = fit; cfg.rotation = rotation }
        if (caps.brightness) cfg.brightness = Math.round(brightness.value)
        Coolers.setLcd(device.id, cfg)
    }

    spacing: 20
    FileDialog {
        id: picker
        title: panel.animated ? "Choose an image or animation" : "Choose an image"
        nameFilters: panel.animated ? ["Images and animations (*.gif *.webp *.png *.apng *.jpg *.jpeg *.bmp)"]
                                    : ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"]
        onAccepted: panel.path = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""))
    }

    Card {
        Layout.preferredWidth: 400
        Layout.alignment: Qt.AlignTop
        title: "On the cooler"
        subtitle: panel.shown.mode === "animation" ? "Animation · " + panel.shown.frames + " frames, streamed by SudoRGB"
                  : panel.shown.mode === "image" ? "Still image" : panel.shown.mode === "processing" ? "Preparing…"
                  : panel.shown.mode === "error" ? panel.shown.message : "Cooler's own screen"
        LcdPreview {
            visible: panel.round
            Layout.preferredWidth: 340
            Layout.preferredHeight: 340
            Layout.alignment: Qt.AlignHCenter
            mode: panel.want.mode === "media" && panel.want.path ? (panel.animated ? "gif" : "image") : "off"
            source: panel.want.path ? "file://" + panel.want.path.split("/").map(encodeURIComponent).join("/") : ""
            busy: panel.shown.mode === "processing"
            fillMode: panel.want.fit === "contain" ? Image.PreserveAspectFit : panel.want.fit === "stretch" ? Image.Stretch : Image.PreserveAspectCrop
            rotation: panel.want.rotation || 0
        }
        AnimatedImage {
            visible: !panel.round
            Layout.preferredWidth: 240
            Layout.preferredHeight: 320
            Layout.alignment: Qt.AlignHCenter
            source: panel.want.mode === "media" ? panel.fileUrl : ""
            fillMode: Image.PreserveAspectCrop
        }
        AccentSlider {
            id: brightness
            visible: panel.caps.brightness === true
            Layout.fillWidth: true
            label: "Brightness"
            value: panel.want.brightness !== undefined ? panel.want.brightness : 64
        }
        Text { visible: panel.round; text: "ORIENTATION"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
        Segmented {
            visible: panel.round
            model: [{ value: 0, label: "0°" }, { value: 90, label: "90°" }, { value: 180, label: "180°" }, { value: 270, label: "270°" }]
            current: panel.rotation
            onActivated: (v) => panel.rotation = v
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        spacing: 16
        Card {
            Layout.fillWidth: true
            title: "Display content"
            Segmented {
                model: [{ value: "media", label: panel.animated ? "Image or animation" : "Image" }, { value: "off", label: "Cooler's own screen" }]
                current: panel.mode
                onActivated: (v) => panel.mode = v
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                text: panel.animated
                      ? "GIF, animated WebP and APNG play at their own speed. This panel has no animation memory, so SudoRGB streams every frame while it runs; when SudoRGB closes the cooler goes back to its own screen."
                      : "Still images are stored in a slot on the cooler, so they stay after SudoRGB closes."
            }
        }
        Card {
            Layout.fillWidth: true
            visible: panel.mode === "media"
            title: "File"
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Text {
                        text: panel.path ? panel.path.split("/").pop() : "Choose a file"
                        color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold
                        elide: Text.ElideMiddle; Layout.fillWidth: true
                    }
                    GhostButton { text: panel.path ? "Change file…" : "Choose file…"; iconName: "upload"; onClicked: picker.open() }
                    Segmented {
                        visible: panel.round
                        model: [{ value: "cover", label: "Fill circle" }, { value: "contain", label: "Fit whole" }, { value: "stretch", label: "Stretch" }]
                        current: panel.fit
                        small: true
                        onActivated: (v) => panel.fit = v
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.device.state !== "ready" ? panel.device.message || ("Cooler " + panel.device.state + "; saved settings apply when it connects")
                      : panel.mode === "media" && !panel.path ? "Choose a file first." : panel.dirty ? "Unsaved changes" : "Applied"
                color: panel.dirty ? Theme.warn : Theme.textFaint
                font.family: Theme.font
                font.pixelSize: Theme.fsSmall
            }
            PrimaryButton {
                text: panel.device.state === "ready" ? "Apply to LCD" : "Save for later"
                iconName: "check"
                busy: panel.shown.mode === "processing"
                enabled: !(panel.mode === "media" && !panel.path)
                onClicked: panel.apply()
            }
        }
    }
}
