import QtQuick
import QtQuick.Controls.Basic
import Orkc

Button {
    id: control
    property string iconName: ""
    property bool danger: false
    property bool compact: false
    implicitHeight: compact ? 30 : 36
    leftPadding: text === "" ? 9 : 14
    rightPadding: text === "" ? 9 : 14
    hoverEnabled: true
    readonly property color fg: danger ? Theme.danger : (hovered ? Theme.text : Theme.textDim)

    contentItem: Row {
        spacing: 7
        Icon {
            visible: control.iconName !== ""
            name: control.iconName
            size: control.compact ? 14 : 16
            color: control.fg
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text !== ""
            text: control.text
            color: control.fg
            font.family: Theme.font
            font.pixelSize: control.compact ? Theme.fsSmall : Theme.fsBody
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    background: Rectangle {
        radius: Theme.radiusMd
        color: control.down ? Theme.surface3 : control.hovered ? Theme.surface2 : "transparent"
        border.color: control.hovered ? Theme.borderStrong : Theme.border
        opacity: control.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }
}
