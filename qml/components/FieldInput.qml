import QtQuick
import QtQuick.Controls.Basic
import Orkc

TextField {
    id: control
    implicitHeight: 36
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accentLine
    selectedTextColor: Theme.text
    font.family: Theme.font
    font.pixelSize: Theme.fsBody
    leftPadding: 12; rightPadding: 12
    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.surface2
        border.color: control.activeFocus ? Theme.accent : (control.hovered ? Theme.borderStrong : Theme.border)
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
    }
}
