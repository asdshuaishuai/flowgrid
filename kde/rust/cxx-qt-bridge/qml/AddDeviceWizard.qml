import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    width: 400
    height: 420
    minimumWidth: 400
    maximumWidth: 400
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    property int currentStep: 0
    property var selectedDevice: null

    function show() {
        visible = true
        currentStep = 0
        selectedDevice = null
        backend.scan_devices()
    }
    function hide() {
        visible = false
        wizardLoader.active = false
    }

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: "#f7f9fb"
        border.color: "rgba(110,123,141,0.12)"
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 0

        Text {
            text: "Add Device"
            color: "#171a1f"
            font.pixelSize: 18
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Item { Layout.preferredHeight: 16 }

        StepIndicator {
            id: stepIndicator
            currentStep: root.currentStep
            totalSteps: 3
            Layout.alignment: Qt.AlignHCenter
        }

        Item { Layout.preferredHeight: 20 }

        StackLayout {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentStep

            Rectangle {
                color: "transparent"
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.centerIn: parent
                    spacing: 20
                    width: parent.width

                    Rectangle {
                        width: 64; height: 64; radius: 32
                        color: "transparent"
                        border.color: "#3daee9"
                        border.width: 3
                        anchors.horizontalCenter: parent.horizontalCenter

                        RotationAnimation on rotation {
                            loops: Animation.Infinite
                            from: 0
                            to: 360
                            duration: 1000
                        }

                        Rectangle {
                            width: 64; height: 64; radius: 32
                            color: "transparent"
                            border.color: "#d9dee7"
                            border.width: 3
                            opacity: 0.35
                        }
                    }

                    Text {
                        text: "Searching for devices..."
                        color: "#68707d"
                        font.pixelSize: 14
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: "Make sure the target device is on the same network and has FlowGrid running."
                        color: "#68707d"
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }

            Rectangle {
                color: "transparent"
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.fill: parent
                    spacing: 8
                    width: parent.width

                    Text {
                        text: "Select a device to connect:"
                        color: "#171a1f"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    ListView {
                        width: parent.width
                        height: parent.height - 40
                        clip: true
                        spacing: 6

                        model: backend.get_devices()

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: 56
                            radius: 6
                            color: selectedDevice && selectedDevice.id === modelData.id ? "#e8f7ff" : "#ffffff"
                            border.color: selectedDevice && selectedDevice.id === modelData.id ? "#3daee9" : "rgba(109,122,140,0.18)"
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
                                        text: modelData.name.substring(0, 2).toUpperCase()
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
                                        text: modelData.name
                                        color: "#171a1f"
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                    Text {
                                        text: modelData.platform + " · " + modelData.transport_type
                                        color: "#68707d"
                                        font.pixelSize: 10
                                    }
                                }

                                Rectangle {
                                    width: latText.width + 14
                                    height: 22
                                    radius: 999
                                    color: {
                                        var v = parseFloat(modelData.latency)
                                        if (v < 2) return "#e8fbf1"
                                        if (v < 4) return "#fef3c7"
                                        return "#fee2e2"
                                    }
                                    Layout.alignment: Qt.AlignVCenter

                                    Text {
                                        id: latText
                                        anchors.centerIn: parent
                                        text: modelData.latency
                                        color: {
                                            var v = parseFloat(modelData.latency)
                                            if (v < 2) return "#07966c"
                                            if (v < 4) return "#d97706"
                                            return "#dc2626"
                                        }
                                        font.pixelSize: 10
                                        font.bold: true
                                    }
                                }

                                RadioButton {
                                    checked: selectedDevice && selectedDevice.id === modelData.id
                                    onClicked: root.selectedDevice = modelData
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectedDevice = modelData
                            }
                        }
                    }
                }
            }

            Rectangle {
                color: "transparent"
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.centerIn: parent
                    spacing: 16
                    width: parent.width

                    Rectangle {
                        width: 72; height: 72; radius: 36
                        color: "#e8fbf1"
                        anchors.horizontalCenter: parent.horizontalCenter

                        Text {
                            anchors.centerIn: parent
                            text: "✓"
                            color: "#10a36f"
                            font.pixelSize: 32
                            font.bold: true
                        }

                        scale: 0
                        SequentialAnimation on scale {
                            running: root.visible && root.currentStep === 2
                            PauseAnimation { duration: 100 }
                            NumberAnimation { to: 1.1; duration: 300; easing.type: Easing.OutBack }
                            NumberAnimation { to: 1.0; duration: 150; easing.type: Easing.InOutQuad }
                        }
                    }

                    Text {
                        text: selectedDevice ? "Connected to " + selectedDevice.name : "Connected"
                        color: "#171a1f"
                        font.pixelSize: 16
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Text {
                        text: "You can now control this device with your keyboard and mouse."
                        color: "#68707d"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Column {
                        spacing: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 60

                        Repeater {
                            model: selectedDevice ? [
                                { label: "Device", value: selectedDevice.name },
                                { label: "Protocol", value: selectedDevice.transport_type },
                                { label: "Latency", value: selectedDevice.latency }
                            ] : []

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: modelData.label
                                    color: "#68707d"
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 80
                                }
                                Text {
                                    text: modelData.value
                                    color: "#171a1f"
                                    font.pixelSize: 11
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 16 }

        RowLayout {
            Layout.fillWidth: true
            height: 36
            spacing: 8

            Button {
                text: "Back"
                Layout.preferredHeight: 28
                visible: root.currentStep > 0
                contentItem: Text {
                    text: parent.text
                    color: "#475569"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 3
                    color: "#ffffff"
                    border.color: "#d9dee7"
                    border.width: 1
                }
                onClicked: root.currentStep--
            }

            Item { Layout.fillWidth: true }

            Button {
                text: root.currentStep === 2 ? "Finish" : "Next"
                Layout.preferredHeight: 28
                enabled: root.currentStep !== 1 || root.selectedDevice !== null
                contentItem: Text {
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 12
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 3
                    color: parent.enabled ? (parent.down ? "#2a9ad4" : "#3daee9") : "#a0c4d9"
                }
                onClicked: {
                    if (root.currentStep === 2) {
                        root.hide()
                    } else if (root.currentStep === 1 && selectedDevice) {
                        backend.connect_device(selectedDevice.id)
                        root.currentStep++
                    } else {
                        root.currentStep++
                    }
                }
            }
        }
    }
}
