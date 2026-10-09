import QtQuick
import Orkc

// Segmented control. model: [{value, label, icon?}], current: value.
Rectangle {
    id: root
    property var model: []
    property var current
    property bool small: false
    signal activated(var value)

    implicitHeight: small ? 30 : 36
    implicitWidth: row.implicitWidth + 8
    radius: Theme.radiusMd
    color: Theme.surface2
    border.color: Theme.border

    Rectangle {
        id: indicator
        readonly property Item target: {
            for (let i = 0; i < rep.count; ++i) if (root.model[i] && root.model[i].value === root.current) return rep.itemAt(i)
            return null
        }
        visible: target !== null
        x: target ? target.x + 4 : 4
        width: target ? target.width : 0
        y: 4
        height: parent.height - 8
        radius: Theme.radiusSm
        color: Theme.accentSoft
        border.color: Theme.accentLine
        Behavior on x { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
    }

    Row {
        id: row
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            id: rep
            model: root.model
            delegate: Item {
                required property var modelData
                width: Math.max(lbl.implicitWidth + (ic.visible ? 22 : 0) + (root.small ? 18 : 26), 40)
                height: root.height - 8
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon {
                        id: ic
                        visible: !!modelData.icon
                        name: modelData.icon || "chip"
                        size: 14
                        color: modelData.value === root.current ? Theme.accent : Theme.textDim
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: lbl
                        text: modelData.label
                        color: modelData.value === root.current ? Theme.text : Theme.textDim
                        font.family: Theme.font
                        font.pixelSize: root.small ? Theme.fsSmall : Theme.fsBody
                        font.weight: modelData.value === root.current ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.current = modelData.value; root.activated(modelData.value) }
                }
            }
        }
    }
}
