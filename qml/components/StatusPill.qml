import QtQuick
import Orkc

// Small status indicator: coloured dot (pulsing while busy) + label.
Rectangle {
    id: root
    property string text: ""
    property string kind: "idle"   // ok | warn | error | busy | idle | sim
    readonly property color tone: kind === "ok" ? Theme.ok : kind === "warn" ? Theme.warn : kind === "error" ? Theme.danger
                                 : kind === "busy" ? Theme.info : kind === "sim" ? Theme.sim : Theme.textFaint
    implicitHeight: 26
    implicitWidth: row.implicitWidth + 20
    radius: Theme.radiusSm
    color: Theme.withAlpha(tone, 0.10)
    border.color: Theme.withAlpha(tone, 0.28)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Rectangle {
            id: dot
            width: 7; height: 7; radius: 3.5
            color: root.tone
            anchors.verticalCenter: parent.verticalCenter
            SequentialAnimation on opacity {
                running: root.kind === "busy" && Theme.durSlow > 0
                loops: Animation.Infinite
                NumberAnimation { to: 0.25; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 600; easing.type: Easing.InOutSine }
                onRunningChanged: if (!running) dot.opacity = 1
            }
        }
        Text {
            text: root.text
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: Theme.fsSmall
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
