import QtQuick
import QtQuick.Layouts
import Orkc

// Instrument readout: micro label, big monospace value, LED-bar meter, footnote.
ColumnLayout {
    id: root
    property string label: ""
    property var value
    property string display: Theme.isNum(value) ? String(value) : ""
    property string unit: ""
    property string sub: ""
    property string unavailableText: "Unavailable"
    property color tone: Theme.accent
    property real from: 0
    property real to: 100
    property var meterValue: value
    property int valueSize: 40
    readonly property bool available: Theme.isNum(value)
    spacing: 8

    RowLayout {
        spacing: 7
        Rectangle { width: 6; height: 6; radius: 1; color: root.available ? root.tone : Theme.textFaint }
        Text {
            text: root.label
            color: Theme.textDim
            font.family: Theme.font
            font.pixelSize: Theme.fsTiny
            font.weight: Font.DemiBold
            font.letterSpacing: 1.6
            font.capitalization: Font.AllUppercase
        }
    }
    Row {
        spacing: 6
        Text {
            id: big
            text: root.available ? root.display : "—"
            color: root.available ? Theme.text : Theme.textFaint
            font.family: Theme.mono
            font.pixelSize: root.valueSize
            font.weight: Font.Medium
        }
        Text {
            text: root.available ? root.unit : root.unavailableText
            color: root.available ? Theme.textDim : Theme.textFaint
            font.family: Theme.font
            font.pixelSize: root.available ? Theme.fsSmall + 1 : Theme.fsSmall
            anchors.baseline: big.baseline
        }
    }
    Meter {
        Layout.fillWidth: true
        value: root.meterValue
        from: root.from
        to: root.to
        tone: root.tone
    }
    Text {
        text: root.sub
        visible: root.sub !== ""
        color: Theme.textFaint
        font.family: Theme.font
        font.pixelSize: Theme.fsSmall
        elide: Text.ElideRight
        Layout.fillWidth: true
    }
}
