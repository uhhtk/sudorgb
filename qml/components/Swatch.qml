import QtQuick
import Orkc

Rectangle {
    id: root
    property color swatch: "white"
    property bool selected: false
    property real size: 26
    signal clicked()
    width: size; height: size
    radius: size / 3
    color: swatch
    border.width: selected ? 2 : 1
    border.color: selected ? "white" : Qt.rgba(1, 1, 1, 0.12)
    scale: mouse.containsMouse ? 1.08 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durFast } }
    Rectangle {
        visible: root.selected
        anchors.fill: parent
        anchors.margins: -4
        radius: parent.radius + 4
        color: "transparent"
        border.color: Theme.accentLine
        border.width: 2
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
