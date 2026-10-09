import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Orkc

Popup {
    id: root
    property string title: ""
    property string message: ""
    property string confirmText: "Confirm"
    property bool destructive: false
    default property alias extra: extraSlot.data
    signal accepted()
    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 40 : 460)
    padding: 24
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.55) }
    enter: Transition { ParallelAnimation {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.dur }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.dur; easing.type: Easing.OutCubic } } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.durFast } }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.borderStrong
    }
    contentItem: ColumnLayout {
        spacing: 14
        Text {
            text: root.title
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: Theme.fsH2 + 2
            font.weight: Font.Bold
        }
        Text {
            visible: root.message !== ""
            text: root.message
            color: Theme.textDim
            wrapMode: Text.WordWrap
            font.family: Theme.font
            font.pixelSize: Theme.fsBody
            Layout.fillWidth: true
        }
        ColumnLayout { id: extraSlot; Layout.fillWidth: true; spacing: 10 }
        RowLayout {
            Layout.topMargin: 6
            Item { Layout.fillWidth: true }
            GhostButton { text: "Cancel"; onClicked: root.close() }
            PrimaryButton {
                text: root.confirmText
                danger: root.destructive
                onClicked: { root.accepted(); root.close() }
            }
        }
    }
}
