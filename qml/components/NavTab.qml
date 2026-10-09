import QtQuick
import Orkc

// Header navigation tab. The active underline is drawn by the parent bar.
Item {
    id: root
    property string icon: "chip"
    property string label: ""
    property bool active: false
    property bool compact: false
    property bool attention: false
    signal clicked()

    implicitHeight: 40
    implicitWidth: row.implicitWidth + 28

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: Theme.radiusMd
        color: mouse.containsMouse && !root.active ? Theme.surface2 : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Icon {
            name: root.icon
            size: 17
            color: root.active ? Theme.accent : mouse.containsMouse ? Theme.text : Theme.textFaint
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }
        Text {
            visible: !root.compact
            text: root.label
            color: root.active ? Theme.text : mouse.containsMouse ? Theme.text : Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fsBody
            font.weight: root.active ? Font.DemiBold : Font.Medium
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Rectangle {
        visible: root.attention
        width: 6; height: 6; radius: 3
        color: Theme.warn
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
