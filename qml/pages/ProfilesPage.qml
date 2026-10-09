import QtQuick
import QtCore
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Orkc
import Orkc.Backend

ScrollView {
    id: page
    property var window
    contentWidth: availableWidth
    property string targetId: ""

    FileDialog {
        id: importDialog
        title: "Import profile"
        nameFilters: ["Profiles (*.json)"]
        onAccepted: Profiles.importFile(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: "Export profile"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Profiles (*.json)"]
        onAccepted: Profiles.exportFile(page.targetId, selectedFile)
    }

    ConfirmDialog {
        id: saveDialog
        parent: Overlay.overlay
        title: "Save current setup"
        message: "Captures every OpenRGB device's mode and colours plus the Kraken lighting, LCD and cooling as they are now."
        confirmText: "Save profile"
        onAccepted: Profiles.saveCurrent(nameField.text, descField.text, inclRgb.checked, inclKraken.checked)
        onOpened: { nameField.text = ""; descField.text = ""; nameField.forceActiveFocus() }
        FieldInput { id: nameField; Layout.fillWidth: true; placeholderText: "Name, e.g. Late night" }
        FieldInput { id: descField; Layout.fillWidth: true; placeholderText: "Description (optional)" }
        Toggle { id: inclRgb; Layout.fillWidth: true; label: "Include OpenRGB devices"; checked: true; enabled: Rgb.connected }
        Toggle { id: inclKraken; Layout.fillWidth: true; label: "Include Kraken lighting, LCD & cooling"; checked: true }
    }
    ConfirmDialog {
        id: renameDialog
        parent: Overlay.overlay
        title: "Edit profile"
        confirmText: "Save"
        onAccepted: Profiles.rename(page.targetId, renameField.text, renameDesc.text)
        FieldInput { id: renameField; Layout.fillWidth: true; placeholderText: "Name" }
        FieldInput { id: renameDesc; Layout.fillWidth: true; placeholderText: "Description" }
    }
    ConfirmDialog {
        id: deleteDialog
        parent: Overlay.overlay
        title: "Delete profile?"
        message: "The file is moved to the app's state folder, so it can be recovered."
        confirmText: "Delete"
        destructive: true
        onAccepted: Profiles.remove(page.targetId)
    }
    ConfirmDialog {
        id: overwriteDialog
        parent: Overlay.overlay
        title: "Update profile from current setup?"
        message: "Replaces this profile's lighting, LCD and cooling with what is active now."
        confirmText: "Update"
        onAccepted: Profiles.overwriteWithCurrent(page.targetId)
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: 20

        SectionHeader {
            Layout.fillWidth: true
            Layout.margins: 32
            Layout.bottomMargin: 0
            title: "Profiles"
            subtitle: "Lighting, LCD and cooling in one click. Stored in " + "~/.config/orkc/profiles"
            GhostButton { text: "Import"; iconName: "download"; onClicked: importDialog.open() }
            PrimaryButton { text: "Save current"; iconName: "plus"; onClicked: saveDialog.open() }
        }

        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            padding: 16
            Toggle {
                Layout.fillWidth: true
                label: "Restore automatically"
                hint: "Re-apply your last lighting whenever OpenRGB (re)connects, and the Kraken's lighting, LCD and cooling whenever it (re)connects — including after reboots."
                checked: AppSettings.autoRestoreProfile
                onToggled: AppSettings.autoRestoreProfile = checked
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 32
            Layout.rightMargin: 32
            columns: Math.max(1, Math.floor((page.availableWidth - 64 + 16) / 340))
            columnSpacing: 16
            rowSpacing: 16

            Repeater {
                model: Profiles.profiles
                delegate: Rectangle {
                    id: pcard
                    required property var modelData
                    readonly property bool active: modelData.id === Profiles.activeId
                    readonly property color tone: modelData.accent || Theme.accent
                    Layout.fillWidth: true
                    Layout.preferredHeight: 214
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: active ? Theme.withAlpha(tone, 0.6) : (hov.hovered ? Theme.borderStrong : Theme.border)
                    border.width: active ? 2 : 1
                    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
                    HoverHandler { id: hov }

                    // colour band
                    Rectangle {
                        id: band
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: pcard.border.width
                        height: 60
                        topLeftRadius: Theme.radius - 1
                        topRightRadius: Theme.radius - 1
                        readonly property var sw: pcard.modelData.swatches || []
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: band.sw[0] || "#1f2128" }
                            GradientStop { position: 0.5; color: band.sw[1] || band.sw[0] || "#1f2128" }
                            GradientStop { position: 1; color: band.sw[2] || band.sw[1] || band.sw[0] || "#1f2128" }
                        }
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        anchors.topMargin: 74
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text { text: pcard.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsH2; font.weight: Font.Bold; elide: Text.ElideRight; Layout.fillWidth: true }
                            Tag { visible: pcard.modelData.builtin === true; text: "Built-in" }
                            Tag { visible: pcard.active; text: "Active"; tone: Theme.ok }
                        }
                        Text {
                            text: pcard.modelData.description || ""
                            color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: {
                                const p = pcard.modelData
                                const bits = []
                                if (p.rgb) bits.push(p.rgb.all ? "All RGB: " + p.rgb.all.effect : p.deviceCount + " RGB devices")
                                if (p.kraken && p.kraken.lcd) bits.push("LCD: " + p.kraken.lcd.mode)
                                if (p.kraken && p.kraken.cooling) bits.push("Cooling")
                                return bits.join("  ·  ")
                            }
                            color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny
                            elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Item { Layout.fillHeight: true }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            PrimaryButton { text: pcard.active ? "Re-apply" : "Apply"; iconName: "check"; onClicked: Profiles.apply(pcard.modelData.id) }
                            Item { Layout.fillWidth: true }
                            GhostButton { compact: true; iconName: "copy"; ToolTip.visible: hovered; ToolTip.text: "Duplicate"; onClicked: Profiles.duplicate(pcard.modelData.id) }
                            GhostButton { compact: true; iconName: "upload"; ToolTip.visible: hovered; ToolTip.text: "Export"
                                onClicked: {
                                    page.targetId = pcard.modelData.id
                                    exportDialog.currentFolder = StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
                                    exportDialog.selectedFile = exportDialog.currentFolder + "/" + pcard.modelData.name.replace(/[^\w\- ]/g, "") + ".json"
                                    exportDialog.open()
                                } }
                            GhostButton { compact: true; iconName: "refresh"; visible: !pcard.modelData.builtin; ToolTip.visible: hovered; ToolTip.text: "Update from current setup"
                                onClicked: { page.targetId = pcard.modelData.id; overwriteDialog.open() } }
                            GhostButton { compact: true; iconName: "edit"; visible: !pcard.modelData.builtin; ToolTip.visible: hovered; ToolTip.text: "Rename"
                                onClicked: { page.targetId = pcard.modelData.id; renameField.text = pcard.modelData.name; renameDesc.text = pcard.modelData.description || ""; renameDialog.open() } }
                            GhostButton { compact: true; iconName: "trash"; danger: true; visible: !pcard.modelData.builtin; ToolTip.visible: hovered; ToolTip.text: "Delete"
                                onClicked: { page.targetId = pcard.modelData.id; deleteDialog.open() } }
                        }
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 24 }
    }

    component Tag: Rectangle {
        property alias text: t.text
        property color tone: Theme.textDim
        height: 20
        width: t.implicitWidth + 14
        radius: 10
        color: Theme.withAlpha(tone, 0.12)
        border.color: Theme.withAlpha(tone, 0.3)
        Text { id: t; anchors.centerIn: parent; color: parent.tone; font.family: Theme.font; font.pixelSize: Theme.fsTiny }
    }
}
