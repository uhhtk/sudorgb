import QtQuick
import QtQuick.Layouts
import Orkc
import Orkc.Backend

// Explains who else is driving the Kraken and how to hand it over.
Rectangle {
    id: root
    signal fixOpenRgb()
    visible: Kraken.state === "conflict"
    radius: Theme.radius
    color: Theme.withAlpha(Theme.warn, 0.07)
    border.color: Theme.withAlpha(Theme.warn, 0.35)
    implicitHeight: visible ? col.implicitHeight + 32 : 0

    readonly property bool openRgbHolds: {
        const h = Kraken.conflictHolders || []
        for (let i = 0; i < h.length; ++i) if (h[i].tool === "OpenRGB") return true
        return false
    }

    RowLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14
        Rectangle {
            width: 36; height: 36; radius: 10
            color: Theme.withAlpha(Theme.warn, 0.15)
            Layout.alignment: Qt.AlignTop
            Icon { anchors.centerIn: parent; name: "alert"; color: Theme.warn; size: 18 }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: "Kraken is in use by another program"
                color: Theme.text
                font.family: Theme.font
                font.pixelSize: Theme.fsBody + 1
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: Theme.fsSmall
                text: {
                    const h = Kraken.conflictHolders || []
                    const lines = h.map(x => "• " + x.tool + " (pid " + x.pid + ")" + (x.nodes && x.nodes.length ? " holds " + x.nodes.join(", ") : ""))
                    return "To protect the cooler only one program may control it. ORKC is waiting and will take over automatically once it is free.\n" + lines.join("\n")
                }
            }
        }
        ColumnLayout {
            spacing: 8
            Layout.alignment: Qt.AlignTop
            GhostButton {
                visible: root.openRgbHolds
                text: "Fix OpenRGB…"
                iconName: "settings"
                onClicked: root.fixOpenRgb()
            }
        }
    }
}
