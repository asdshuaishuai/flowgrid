import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    width: 270
    height: trayColumn.height + 24
    minimumWidth: 260
    maximumWidth: 280
    flags: Qt.Popup | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    x: Screen.width - width - 20
    y: 40

    function show() {
        visible = true
        var devices = backend.get_devices()
        if (devices.length > 0) {
            activeDeviceName.text = devices[0].name
            activeDeviceMeta.text = devices[0].platform + " · " + (devices[0].connected ? "Connected" : "Idle")
            activeDeviceLatency.text = devices[0].latency
        }
        latencyText.text = backend.get_latency()
    }
    function hide() { visible = false; trayPopupLoader.active = false }

    onVisibleChanged: {
        if (visible) {
            opacity = 0
            y = root.y + 6
            fadeInAnim.start()
        }
    }

    ParallelAnimation {
        id: fadeInAnim
        NumberAnimation { target: root; property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.InOutQuad }
        NumberAnimation { target: root; property: "y"; from: root.y + 6; to: root.y - 6; duration: 200; easing.type: Easing.InOutQuad }
    }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: 6
        color: "#f7f9fb"
        opacity: 0.95

        Rectangle {
            anchors.fill: parent
            radius: 6
            color: "#f7f9fb"
            border.color: "rgba(110,123,141,0.12)"
            border.width: 1
        }
    }

    Column {
        id: trayColumn
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 0

        RowLayout {
            width: parent.width
            height: 28

            Text {
                text: "FlowGrid"
                color: "#171a1f"
                font.pixelSize: 14
                font.bold: true
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: connectedText.width + 16
                height: 20
                radius: 999
                color: "#e8fbf1"
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                Text {
                    id: connectedText
                    anchors.centerIn: parent
                    text: "Connected"
                    color: "#07966c"
                    font.pixelSize: 10
                    font.bold: true
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: "rgba(110,123,141,0.12)" }

        Item { width: parent.width; height: 8 }

        Text {
            text: "CONNECTION"
            color: "#68707d"
            font.pixelSize: 11
            font.bold: true
        }

        Item { width: parent.width; height: 6 }

        Rectangle {
            width: parent.width
            height: 56
            radius: 7
            color: "rgba(255,255,255,0.88)"
            border.color: "rgba(109,122,140,0.18)"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Rectangle {
                    width: 36; height: 36; radius: 8
                    color: "#e6f2ff"
                    Layout.alignment: Qt.AlignVCenter
                    Text {
                        anchors.centerIn: parent
                        text: activeDeviceName.text.substring(0, 2).toUpperCase()
                        color: "#3daee9"
                        font.pixelSize: 11
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Layout.alignment: Qt.AlignVCenter

                    Text {
                        id: activeDeviceName
                        text: "No Device"
                        color: "#171a1f"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                    Text {
                        id: activeDeviceMeta
                        text: "--"
                        color: "#68707d"
                        font.pixelSize: 10
                    }
                }

                Text {
                    id: activeDeviceLatency
                    text: "--"
                    color: "#07966c"
                    font.pixelSize: 11
                    font.bold: true
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        Item { width: parent.width; height: 8 }

        Rectangle { width: parent.width; height: 1; color: "rgba(110,123,141,0.12)" }

        Item { width: parent.width; height: 8 }

        Text {
            text: "STATUS"
            color: "#68707d"
            font.pixelSize: 11
            font.bold: true
        }

        Item { width: parent.width; height: 6 }

        RowLayout {
            width: parent.width
            height: 22

            Text {
                text: "Latency"
                color: "#171a1f"
                font.pixelSize: 12
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            Text {
                id: latencyText
                text: "--"
                color: "#07966c"
                font.pixelSize: 12
                font.bold: true
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            }
        }

        Item { width: parent.width; height: 8 }

        Rectangle { width: parent.width; height: 1; color: "rgba(110,123,141,0.12)" }

        Item { width: parent.width; height: 8 }

        Text {
            text: "QUICK TOGGLES"
            color: "#68707d"
            font.pixelSize: 11
            font.bold: true
        }

        Item { width: parent.width; height: 6 }

        Column {
            width: parent.width
            spacing: 6

            Repeater {
                model: [
                    { label: "Key Mapping", key: "keyMapping" },
                    { label: "Clipboard Sync", key: "clipboardSync" },
                    { label: "Mouse Smoothing", key: "mouseSmoothing" },
                    { label: "DTLS Encryption", key: "dtls" }
                ]

                RowLayout {
                    width: parent.width
                    height: 26

                    Text {
                        text: modelData.label
                        color: "#171a1f"
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    }
                    Item { Layout.fillWidth: true }

                    Rectangle {
                        property bool on: backend.get_setting(modelData.key)
                        width: 34; height: 18; radius: 9
                        color: on ? "#18a058" : "#cccccc"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                        Rectangle {
                            width: 14; height: 14; radius: 7
                            color: "#ffffff"
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.on ? parent.width - width - 2 : 2
                            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                parent.on = !parent.on
                                backend.set_setting(modelData.key, parent.on)
                            }
                        }
                    }
                }
            }
        }

        Item { width: parent.width; height: 8 }

        Rectangle { width: parent.width; height: 1; color: "rgba(110,123,141,0.12)" }

        Item { width: parent.width; height: 8 }

        Column {
            width: parent.width
            spacing: 0

            Repeater {
                model: [
                    { label: "Open Main Window", shortcut: "Ctrl+Shift+F", danger: false },
                    { label: "Settings", shortcut: "", danger: false },
                    { label: "Keymap Editor", shortcut: "", danger: false },
                    { label: "Quit", shortcut: "", danger: true }
                ]

                Rectangle {
                    width: parent.width
                    height: 32
                    radius: 4
                    color: actionArea.containsMouse ? "#f1f5f9" : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4

                        Text {
                            text: modelData.label
                            color: modelData.danger ? "#dc2626" : "#171a1f"
                            font.pixelSize: 12
                            font.weight: modelData.danger ? Font.Bold : Font.Normal
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: modelData.shortcut
                            color: "#68707d"
                            font.pixelSize: 10
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        }
                    }

                    MouseArea {
                        id: actionArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (modelData.danger) Qt.quit()
                            else root.hide()
                        }
                    }
                }
            }
        }
    }
}
