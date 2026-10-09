import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Orkc
import Orkc.Backend

ApplicationWindow {
    id: win
    width: 1320
    height: 860
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: "SudoRGB"
    color: Theme.bg
    font.family: Theme.font

    property int currentPage: 0
    readonly property bool compact: width < 1180
    readonly property bool minimized: visibility === Window.Minimized || visibility === Window.Hidden

    // Don't spend CPU on sensors nobody is looking at.
    Binding { target: SystemInfo; property: "active"; value: !win.minimized }
    onMinimizedChanged: Kraken.setPollInterval(minimized ? 5 : AppSettings.telemetryInterval)

    readonly property var pages: [
        { label: "Overview", icon: "dashboard", src: "pages/DashboardPage.qml" },
        { label: "Lighting", icon: "lighting", src: "pages/LightingPage.qml" },
        { label: "LCD", icon: "lcd", src: "pages/LcdPage.qml" },
        { label: "Cooling", icon: "fan", src: "pages/CoolingPage.qml" },
        { label: "Profiles", icon: "profiles", src: "pages/ProfilesPage.qml" },
        { label: "Devices", icon: "chip", src: "pages/DevicesPage.qml" },
        { label: "Settings", icon: "settings", src: "pages/SettingsPage.qml" }
    ]

    function go(i) { currentPage = i }
    function toast(msg, err) { toasts.show(msg, err) }

    Connections {
        target: Rgb
        function onNotify(message, error) { win.toast(message, error) }
    }
    Connections {
        target: Profiles
        function onNotify(message, error) { win.toast(message, error) }
        function onApplied(id, warnings) {
            const p = Profiles.get(id)
            win.toast("Applied “" + p.name + "”" + (warnings.length ? "\n" + warnings.join("\n") : ""), warnings.length > 0)
        }
    }
    Connections {
        target: Kraken
        function onCommandFinished(id, ok, message, result) {
            if (!ok && message && message.indexOf("Replaced by a newer") < 0) win.toast("Kraken: " + message, true)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------------------------------------------------- header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: Theme.header
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 18
                spacing: 18

                Row {
                    spacing: 11
                    Layout.alignment: Qt.AlignVCenter
                    Image { source: "qrc:/qt/qml/Orkc/resources/icons/sudorgb.png"; width: 48; height: 48; sourceSize: Qt.size(96, 96); mipmap: true; anchors.verticalCenter: parent.verticalCenter }
                    Column {
                        visible: !win.compact
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        Text { text: "SudoRGB"; color: Theme.text; font.family: Theme.mono; font.pixelSize: Theme.fsH2; font.weight: Font.Bold }
                        Text { text: "control center"; color: Theme.textFaint; font.family: Theme.font; font.pixelSize: Theme.fsTiny; font.letterSpacing: 1.2 }
                    }
                }

                Item { Layout.preferredWidth: win.compact ? 0 : 12 }

                // Tabs with a sliding underline
                Item {
                    Layout.fillHeight: true
                    Layout.preferredWidth: tabs.implicitWidth
                    Row {
                        id: tabs
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Repeater {
                            id: tabRep
                            model: win.pages
                            delegate: NavTab {
                                required property int index
                                required property var modelData
                                compact: win.compact
                                icon: modelData.icon
                                label: modelData.label
                                active: win.currentPage === index
                                attention: (index === 0 || index === 3) && Kraken.state === "conflict"
                                onClicked: win.go(index)
                            }
                        }
                    }
                    Rectangle {
                        readonly property Item target: tabRep.itemAt(win.currentPage)
                        height: 2
                        radius: 1
                        color: Theme.accent
                        y: parent.height - height
                        x: target ? tabs.x + target.x + 12 : 0
                        width: target ? target.width - 24 : 0
                        Behavior on x { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
                        Behavior on width { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } }
                    }
                }

                Item { Layout.fillWidth: true }

                // Live link status
                Row {
                    spacing: 16
                    Layout.alignment: Qt.AlignVCenter
                    Repeater {
                        model: [
                            { name: "OpenRGB", tone: Rgb.connected ? Theme.ok : (Rgb.state === "connecting" || Rgb.state === "syncing") ? Theme.info : Theme.danger,
                              detail: Rgb.connected ? Rgb.controllableCount + " dev" : "offline" },
                            { name: "Kraken", tone: Kraken.simulated ? Theme.sim : Kraken.ready ? Theme.ok : Kraken.state === "conflict" ? Theme.warn
                                                  : (Kraken.state === "starting" || Kraken.state === "searching" || Kraken.state === "restarting") ? Theme.info : Theme.danger,
                              detail: Kraken.ready ? (Kraken.simulated ? "sim" : (Theme.isNum(Kraken.liquidTemp) ? Theme.temp(Kraken.liquidTemp) + Theme.tempUnit : "ok")) : Kraken.state }
                        ]
                        delegate: Row {
                            required property var modelData
                            spacing: 7
                            Rectangle { width: 7; height: 7; radius: 3.5; color: modelData.tone; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: modelData.name; color: Theme.textDim; font.family: Theme.font; font.pixelSize: Theme.fsSmall; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: modelData.detail; color: Theme.textFaint; font.family: Theme.mono; font.pixelSize: Theme.fsTiny + 1; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------- pages
        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Repeater {
                model: win.pages
                delegate: Loader {
                    id: pageLoader
                    required property int index
                    required property var modelData
                    readonly property bool current: win.currentPage === index
                    property bool visited: current
                    onCurrentChanged: if (current) visited = true
                    anchors.fill: parent
                    active: visited
                    asynchronous: !current
                    source: modelData.src
                    opacity: current ? 1 : 0
                    visible: opacity > 0.01
                    transform: Translate { x: pageLoader.current ? 0 : 18; Behavior on x { NumberAnimation { duration: Theme.dur; easing.type: Easing.OutCubic } } }
                    Behavior on opacity { NumberAnimation { duration: Theme.dur } }
                    onLoaded: {
                        if (item.hasOwnProperty("window")) item.window = win
                    }
                }
            }
        }
    }

    Toast {
        id: toasts
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        z: 100
    }
}
