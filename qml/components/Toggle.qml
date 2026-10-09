import QtQuick
import QtQuick.Controls.Basic
import Orkc

Switch {
    id: control
    property string label: ""
    property string hint: ""
    hoverEnabled: true
    implicitHeight: hint !== "" ? 44 : 30
    spacing: 12

    indicator: Rectangle {
        x: control.width - width
        anchors.verticalCenter: parent.verticalCenter
        width: 40; height: 22; radius: 11
        color: control.checked ? Theme.accent : Theme.surface3
        border.color: control.checked ? Theme.accent : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.dur } }
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16; radius: 8
            color: "white"
            Behavior on x { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Column {
        anchors.verticalCenter: parent.verticalCenter
        rightPadding: 52
        spacing: 2
        Text {
            text: control.label
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: Theme.fsBody
        }
        Text {
            visible: control.hint !== ""
            text: control.hint
            color: Theme.textFaint
            font.family: Theme.font
            font.pixelSize: Theme.fsTiny
            width: control.width - 56
            wrapMode: Text.WordWrap
        }
    }
}
