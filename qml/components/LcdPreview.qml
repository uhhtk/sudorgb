import QtQuick
import QtQuick.Shapes
import Orkc

// Round LCD preview. Shows exactly what was uploaded (processed file from the
// service), the firmware liquid screen, or a placeholder.
Item {
    id: root
    property string mode: "unknown"       // liquid | sensors | image | gif | off | processing | error | unknown
    property string source: ""            // file URL of the processed media
    property var liquidTemp
    property bool busy: false
    property string caption: ""
    property color ringColor: Theme.accent
    property int fillMode: Image.PreserveAspectCrop
    property bool showBezel: true
    property color maskColor: Theme.surface   // colour behind the preview (corners are painted with it)
    readonly property color innerMask: maskColor
    implicitWidth: 240
    implicitHeight: 240

    readonly property real d: Math.min(width, height)

    Rectangle {  // bezel
        visible: root.showBezel
        anchors.centerIn: parent
        width: root.d; height: root.d; radius: width / 2
        color: "#050507"
        border.color: Theme.borderStrong
        border.width: 2
    }

    Item {
        id: screen
        anchors.centerIn: parent
        width: root.showBezel ? root.d - 16 : root.d; height: width

        Rectangle { anchors.fill: parent; color: "#07080b" }

        AnimatedImage {
            id: anim
            anchors.fill: parent
            source: (root.mode === "gif" || root.mode === "image" || root.mode === "sensors") ? root.source : ""
            fillMode: root.fillMode
            asynchronous: true
            cache: false
            playing: root.visible && root.mode === "gif"
            smooth: true
        }

        // Firmware liquid-temperature screen (rendered by the cooler; we mimic it)
        Column {
            visible: root.mode === "liquid" || (root.mode !== "off" && anim.status !== AnimatedImage.Ready)
            anchors.centerIn: parent
            spacing: 4
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Theme.isNum(root.liquidTemp) ? Theme.temp(root.liquidTemp, 0) : "—"
                color: "white"
                font.family: Theme.mono
                font.pixelSize: screen.width * 0.26
                font.weight: Font.Bold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.mode === "liquid" ? "LIQUID " + Theme.tempUnit : root.mode === "unknown" ? "NO DATA" : root.mode.toUpperCase()
                color: Theme.textDim
                font.family: Theme.font
                font.pixelSize: screen.width * 0.065
                font.letterSpacing: 2
            }
        }
        Rectangle {
            visible: root.mode === "off"
            anchors.fill: parent
            color: "black"
        }
    }
    // Round clip without layers or shaders: paint the four corners (square minus
    // circle, even-odd fill) in the background colour, then a crisp edge ring.
    Shape {
        anchors.fill: screen
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: root.innerMask
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            PathSvg {
                readonly property real w: screen.width
                readonly property real r: screen.width / 2 - 0.5
                path: "M-1,-1 H" + (w + 1) + " V" + (w + 1) + " H-1 Z M" + (w / 2) + "," + (w / 2 - r) +
                      " A" + r + "," + r + " 0 1,0 " + (w / 2) + "," + (w / 2 + r) +
                      " A" + r + "," + r + " 0 1,0 " + (w / 2) + "," + (w / 2 - r) + " Z"
            }
        }
    }
    Rectangle {  // bezel ring, drawn over the corner mask
        anchors.centerIn: parent
        width: root.showBezel ? root.d : screen.width
        height: width
        radius: width / 2
        color: "transparent"
        border.color: root.showBezel ? "#050507" : Theme.borderStrong
        border.width: root.showBezel ? 9 : 1
        Rectangle {
            visible: root.showBezel
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: Theme.borderStrong
            border.width: 1.5
        }
    }
    // Ring glow when liquid mode (the firmware draws its own ring)
    Rectangle {
        visible: root.mode === "liquid"
        anchors.centerIn: parent
        width: screen.width - 18; height: width; radius: width / 2
        color: "transparent"
        border.width: 6
        border.color: Theme.withAlpha(root.ringColor, 0.85)
    }

    // Busy overlay
    Rectangle {
        anchors.centerIn: parent
        width: screen.width; height: width; radius: width / 2
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: root.busy ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.dur } }
        Rectangle {
            id: spinner
            anchors.centerIn: parent
            width: 46; height: 46; radius: 23
            color: "transparent"
            border.width: 3
            border.color: Theme.withAlpha(Theme.accent, 0.25)
            Rectangle { width: 10; height: 10; radius: 5; color: Theme.accent; anchors.horizontalCenter: parent.horizontalCenter; y: -3 }
            RotationAnimation on rotation { running: root.busy; from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
        }
        Text {
            anchors.top: spinner.bottom
            anchors.topMargin: 12
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.caption
            color: Theme.text
            font.family: Theme.font
            font.pixelSize: Theme.fsSmall
        }
    }
}
