import QtQuick
import QtQuick.Layouts
import Orkc

// Flat instrument panel: hairline border, micro-caps title, optional subtitle and trailing slot.
Rectangle {
    id: root
    property string title: ""
    property string subtitle: ""
    property string icon: ""            // kept for API compatibility; titles are text-only
    property alias trailing: trailingSlot.data
    default property alias content: body.data
    property int padding: Theme.pad
    property bool hoverable: false

    color: Theme.surface
    radius: Theme.radius
    border.color: hover.hovered && hoverable ? Theme.borderStrong : Theme.border
    border.width: 1
    implicitWidth: col.implicitWidth + padding * 2
    implicitHeight: col.implicitHeight + padding * 2
    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

    HoverHandler { id: hover }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: root.padding
        spacing: 16

        RowLayout {
            visible: root.title !== "" || trailingSlot.children.length > 0
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                spacing: 3
                Layout.fillWidth: true
                Text {
                    text: root.title
                    color: Theme.textDim
                    font.family: Theme.font
                    font.pixelSize: Theme.fsTiny
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.6
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    visible: root.subtitle !== ""
                    text: root.subtitle
                    color: Theme.textFaint
                    font.family: Theme.font
                    font.pixelSize: Theme.fsSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Row {
                id: trailingSlot
                spacing: 8
                Layout.alignment: Qt.AlignVCenter
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
        }
    }
}
