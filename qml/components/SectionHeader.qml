import QtQuick
import QtQuick.Layouts
import Orkc

RowLayout {
    id: root
    property string title: ""
    property string subtitle: ""
    default property alias trailing: slot.data
    spacing: 16
    ColumnLayout {
        spacing: 4
        Layout.fillWidth: true
        Text {
            text: root.title
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: Theme.fsH1
            font.weight: Font.Bold
        }
        Text {
            visible: root.subtitle !== ""
            text: root.subtitle
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fsBody
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
    Row {
        id: slot
        spacing: 10
        Layout.alignment: Qt.AlignVCenter
    }
}
