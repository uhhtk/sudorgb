import QtQuick
import QtQuick.Controls.Basic
import Orkc

Button {
    id: control
    property string iconName: ""
    property bool busy: false
    property bool danger: false
    readonly property color base: danger ? Theme.danger : Theme.accent
    implicitHeight: 38
    leftPadding: 16; rightPadding: 16
    enabled: !busy
    hoverEnabled: true

    contentItem: Row {
        spacing: 8
        Icon {
            visible: control.iconName !== "" && !control.busy
            name: control.iconName
            size: 16
            color: control.danger ? "white" : Theme.onAccent
            anchors.verticalCenter: parent.verticalCenter
        }
        Rectangle {
            visible: control.busy
            width: 14; height: 14; radius: 7
            color: "transparent"
            border.width: 2
            border.color: Theme.onAccent
            anchors.verticalCenter: parent.verticalCenter
            RotationAnimation on rotation { running: control.busy; from: 0; to: 360; loops: Animation.Infinite; duration: 900 }
            Rectangle { width: 6; height: 6; color: control.base; anchors.top: parent.top; anchors.right: parent.right }
        }
        Text {
            text: control.text
            color: control.danger ? "white" : Theme.onAccent
            font.family: Theme.font
            font.pixelSize: Theme.fsBody
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    background: Rectangle {
        radius: Theme.radiusMd
        color: !control.enabled ? Theme.withAlpha(control.base, 0.4)
             : control.down ? Qt.darker(control.base, 1.15)
             : control.hovered ? Qt.lighter(control.base, 1.08) : control.base
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
}
