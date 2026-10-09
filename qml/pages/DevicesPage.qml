import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc
import Orkc.Backend

// Hardware compatibility: every connected USB device, which backend can drive it,
// and what to do when something is missing. Rescans while visible (hot-plug).
ScrollView {
    id: page
    property var window
    contentWidth: availableWidth

    property string filter: "attention"
    property var all: []
    property var summary: ({})
    readonly property var shown: all.filter(d => filter === "all" ? true
                                         : filter === "supported" ? d.status === "supported"
                                         : filter === "attention" ? d.status !== "other"
                                         : d.status === filter)

    function rescan() {
        all = Hardware.devices(true)
        summary = Hardware.summary()
    }
    function count(st) { return all.filter(d => st === "attention" ? d.status !== "other" : d.status === st).length }

    Component.onCompleted: rescan()
    Timer { interval: 4000; repeat: true; running: page.visible; onTriggered: page.rescan() }

    readonly property var statusInfo: ({
        supported: { label: "Supported", tone: Theme.ok, hint: "" },
        permissions: { label: "Needs permission", tone: Theme.warn,
                       hint: "A driver exists but your user can't open the device. Reload udev rules: sudo udevadm control --reload && sudo udevadm trigger, then replug it." },
        known: { label: "Known, not yet in SudoRGB", tone: Theme.info,
                 hint: "Another Linux project supports this device; a SudoRGB integration is on the roadmap." },
        unsupported: { label: "Unrecognized", tone: Theme.textFaint,
                       hint: "No installed driver knows this device. If it has RGB lighting, export diagnostics and share the file to request support." },
        other: { label: "Other USB", tone: Theme.textFaint, hint: "" }
    })

    ColumnLayout {
        width: page.availableWidth
        spacing: 18

        SectionHeader {
            Layout.fillWidth: true
            Layout.margins: 28
            Layout.bottomMargin: 0
            title: "Devices"
            subtitle: "What's connected, which driver handles it, and how to fix what's missing"
            GhostButton { text: "Rescan"; iconName: "refresh"; onClicked: { Hardware.reload(); page.rescan() } }
            PrimaryButton {
                text: "Export diagnostics"
                iconName: "download"
                onClicked: {
                    const p = Hardware.exportDiagnostics({ "SudoRGB": Qt.application.version, "OpenRGB": Rgb.statusText,
                                                           "OpenRGB protocol": Rgb.protocolVersion, "Kraken service": Kraken.state + " " + Kraken.backendLabel })
                    if (page.window) page.window.toast(p ? "Saved " + p + "\nAttach it to your support request." : "Could not write the diagnostics file", !p)
                }
            }
        }

        // Backends
        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            title: "Drivers"
            subtitle: "SudoRGB uses the device knowledge of the drivers installed on this system"
            Flow {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: [
                        { name: "OpenRGB", n: (page.summary.backends || {})["OpenRGB"], live: Rgb.connected ? Rgb.controllableCount + " active" : "server offline", tone: Rgb.connected ? Theme.ok : Theme.warn },
                        { name: "SudoRGB native", n: (page.summary.backends || {})["SudoRGB"], live: "built in", tone: Theme.ok },
                        { name: "liquidctl", n: (page.summary.backends || {})["liquidctl"], live: "coolers", tone: Theme.ok },
                        { name: "OpenRazer", n: (page.summary.backends || {})["OpenRazer"], live: "planned", tone: Theme.info }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool installed: modelData.n !== undefined
                        height: 56
                        width: col.implicitWidth + 32
                        radius: Theme.radiusMd
                        color: Theme.bg
                        border.color: Theme.border
                        opacity: installed ? 1 : 0.55
                        Column {
                            id: col
                            anchors.centerIn: parent
                            spacing: 3
                            Row {
                                spacing: 7
                                Rectangle { width: 7; height: 7; radius: 3.5; anchors.verticalCenter: parent.verticalCenter; color: installed ? modelData.tone : Theme.textFaint }
                                Text { text: modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody; font.weight: Font.DemiBold }
                            }
                            Text {
                                text: installed ? modelData.n + " known devices · " + modelData.live : "not installed"
                                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                            }
                        }
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                text: "RAM, motherboard and GPU lighting is detected by OpenRGB over SMBus and appears under Lighting, not in the USB list below."
            }
        }

        // Filter
        Segmented {
            Layout.leftMargin: 28
            current: page.filter
            onActivated: (v) => page.filter = v
            model: [
                { value: "attention", label: "Relevant (" + page.count("attention") + ")" },
                { value: "supported", label: "Supported (" + page.count("supported") + ")" },
                { value: "permissions", label: "Needs permission (" + page.count("permissions") + ")" },
                { value: "unsupported", label: "Unrecognized (" + page.count("unsupported") + ")" },
                { value: "all", label: "All USB (" + page.all.length + ")" }
            ]
        }

        Card {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            padding: 10
            Repeater {
                model: page.shown
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property var info: page.statusInfo[modelData.status] || page.statusInfo.other
                    Layout.fillWidth: true
                    implicitHeight: rowCol.implicitHeight + 24
                    radius: Theme.radiusMd
                    color: hov.hovered ? Theme.surface2 : "transparent"
                    HoverHandler { id: hov }
                    ColumnLayout {
                        id: rowCol
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 14; anchors.rightMargin: 14
                        spacing: 4
                        RowLayout {
                            spacing: 12
                            Layout.fillWidth: true
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Text { text: row.modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: Theme.fsBody + 1; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    text: (row.modelData.vendor ? row.modelData.vendor + " · " : "") + row.modelData.id
                                          + (row.modelData.hidraw ? " · " + row.modelData.hidraw : "")
                                    color: Theme.textFaint; font.family: Theme.mono; font.pixelSize: Theme.fsTiny + 1; elide: Text.ElideRight; Layout.fillWidth: true
                                }
                            }
                            Repeater {
                                model: row.modelData.backends
                                delegate: Rectangle {
                                    required property var modelData
                                    height: 22; width: bt.implicitWidth + 16; radius: Theme.radiusSm
                                    color: Theme.surface3
                                    Text { id: bt; anchors.centerIn: parent; text: modelData; color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsTiny + 1 }
                                }
                            }
                            StatusPill {
                                text: row.info.label
                                kind: row.modelData.status === "supported" ? "ok" : row.modelData.status === "permissions" ? "warn"
                                      : row.modelData.status === "known" ? "busy" : "idle"
                            }
                        }
                        Text {
                            visible: row.info.hint !== ""
                            Layout.fillWidth: true
                            text: row.info.hint
                            wrapMode: Text.WordWrap
                            color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                        }
                    }
                }
            }
            Text {
                visible: page.shown.length === 0
                text: "Nothing in this category."
                color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsSmall
                Layout.margins: 14
            }
        }
        Item { Layout.preferredHeight: 20 }
    }
}
