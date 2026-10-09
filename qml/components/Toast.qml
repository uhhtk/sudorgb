import QtQuick
import Orkc

// Stack of transient notifications (bottom-right).
Column {
    id: root
    spacing: 8
    width: 360
    function show(message, error) {
        if (!message) return
        model.append({ msg: message, err: !!error })
        if (model.count > 4) model.remove(0)
    }
    ListModel { id: model }
    Repeater {
        model: model
        delegate: Rectangle {
            id: toast
            required property int index
            required property string msg
            required property bool err
            width: root.width
            height: txt.implicitHeight + 24
            radius: Theme.radiusMd
            color: Theme.surface2
            border.color: err ? Theme.withAlpha(Theme.danger, 0.5) : Theme.borderStrong
            opacity: 0
            x: 20
            Component.onCompleted: { opacity = 1; x = 0 }
            Behavior on opacity { NumberAnimation { duration: Theme.dur } }
            Behavior on x { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
            Rectangle { width: 3; radius: 2; height: parent.height - 16; anchors.verticalCenter: parent.verticalCenter; x: 8; color: toast.err ? Theme.danger : Theme.accent }
            Text {
                id: txt
                anchors.left: parent.left; anchors.leftMargin: 20
                anchors.right: close.left; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: toast.msg
                wrapMode: Text.WordWrap
                color: Theme.text
                font.family: Theme.font
                font.pixelSize: Theme.fsSmall
            }
            Icon {
                id: close
                name: "close"; size: 14; color: Theme.textFaint
                anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: model.remove(toast.index) }
            }
            Timer { running: true; interval: toast.err ? 9000 : 4500; onTriggered: if (toast.index >= 0 && toast.index < model.count) model.remove(toast.index) }
        }
    }
}
