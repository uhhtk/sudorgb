import QtQuick
import QtQuick.Controls.Basic
import Orkc

Slider {
    id: control
    property string label: ""
    property string suffix: "%"
    property var trackGradient: null     // optional list of colours for the track
    property bool showValue: true
    signal committed(real value)
    from: 0; to: 100; stepSize: 1
    implicitHeight: label !== "" ? 46 : 24
    onPressedChanged: if (!pressed) committed(value)

    Text {
        visible: control.label !== ""
        text: control.label
        color: Theme.textDim
        font.family: Theme.font
        font.pixelSize: Theme.fsSmall
        anchors.left: parent.left
        anchors.top: parent.top
    }
    Text {
        visible: control.showValue && control.label !== ""
        text: (control.stepSize > 0 && control.stepSize < 1 ? control.value.toFixed(1) : Math.round(control.value)) + control.suffix
        color: Theme.text
        font.family: Theme.mono
        font.pixelSize: Theme.fsSmall
        anchors.right: parent.right
        anchors.top: parent.top
    }

    background: Rectangle {
        x: control.leftPadding
        y: control.label !== "" ? control.height - 12 : (control.height - height) / 2
        width: control.availableWidth
        height: 6
        radius: 3
        color: Theme.surface3
        gradient: control.trackGradient ? grad : null
        Gradient {
            id: grad
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: control.trackGradient ? control.trackGradient[0] : "black" }
            GradientStop { position: 1; color: control.trackGradient ? control.trackGradient[control.trackGradient.length - 1] : "white" }
        }
        Rectangle {
            visible: !control.trackGradient
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.background.y + 3 - height / 2
        width: control.pressed ? 20 : 18
        height: width
        radius: width / 2
        color: "white"
        border.color: Theme.accent
        border.width: control.pressed ? 4 : 3
        Behavior on width { NumberAnimation { duration: Theme.durFast } }
    }
}
