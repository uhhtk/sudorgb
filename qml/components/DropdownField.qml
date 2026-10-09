import QtQuick
import QtQuick.Controls.Basic
import Orkc

ComboBox {
    id: control
    implicitHeight: 36
    font.family: Theme.font
    font.pixelSize: Theme.fsBody
    hoverEnabled: true

    contentItem: Text {
        leftPadding: 12
        rightPadding: 30
        text: control.displayText
        color: Theme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        name: "download"
        size: 12
        color: Theme.textDim
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
    }
    background: Rectangle {
        radius: Theme.radiusMd
        color: Theme.surface2
        border.color: control.pressed || control.popup.visible ? Theme.accent : (control.hovered ? Theme.borderStrong : Theme.border)
    }
    delegate: ItemDelegate {
        id: d
        required property int index
        required property var modelData
        width: control.width - 8
        x: 4
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textRole ? d.modelData[control.textRole] : d.modelData
            color: d.index === control.currentIndex ? Theme.accent : Theme.text
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: d.highlighted ? Theme.surface3 : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.surface2
            border.color: Theme.borderStrong
        }
    }
}
