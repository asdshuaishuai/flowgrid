import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    width: 420
    height: 340
    minimumWidth: 360
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    function show() { visible = true }
    function hide() { visible = false }

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

        RowLayout {
            Layout.fillWidth: true
            height: 48
            spacing: 12

            Column {
                spacing: 2
                Layout.alignment: Qt.AlignVCenter

                Text {
                    text: "Latency Monitor"
                    color: "#171a1f"
                    font.pixelSize: 16
                    font.bold: true
                }
                Text {
                    text: "Real-time connection quality"
                    color: "#68707d"
                    font.pixelSize: 11
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: latencyValue.width + 24
                height: 36
                radius: 999
                color: {
                    var v = parseFloat(backend.get_latency())
                    if (v < 2) return "#e8fbf1"
                    if (v < 4) return "#fef3c7"
                    return "#fee2e2"
                }
                Layout.alignment: Qt.AlignVCenter

                Text {
                    id: latencyValue
                    anchors.centerIn: parent
                    text: backend.get_latency()
                    color: {
                        var v = parseFloat(backend.get_latency())
                        if (v < 2) return "#07966c"
                        if (v < 4) return "#d97706"
                        return "#dc2626"
                    }
                    font.pixelSize: 18
                    font.bold: true
                }
            }
        }

        Item { Layout.preferredHeight: 12 }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140
            color: "#ffffff"
            radius: 6
            border.color: "rgba(109,122,140,0.18)"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 3

                Repeater {
                    model: latencyModel.samples

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"

                        Rectangle {
                            width: parent.width
                            height: parent.height * (modelData.value / latencyModel.maxLatency)
                            anchors.bottom: parent.bottom
                            radius: 2
                            color: {
                                var v = modelData.value
                                if (v < 2) return "#10a36f"
                                if (v < 4) return "#d97706"
                                return "#dc2626"
                            }
                            opacity: 0.85
                            Behavior on height { NumberAnimation { duration: 400; easing.type: Easing.InOutQuad } }
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 12 }

        RowLayout {
            Layout.fillWidth: true
            height: 52
            spacing: 8

            Repeater {
                model: [
                    { label: "Min", value: latencyModel.minLatency + "ms" },
                    { label: "Max", value: latencyModel.maxLatency + "ms" },
                    { label: "Avg", value: latencyModel.avgLatency + "ms" }
                ]

                Rectangle {
                    Layout.fillWidth: true
                    height: 52
                    radius: 6
                    color: "#f8fafc"
                    border.color: "rgba(109,122,140,0.12)"
                    border.width: 1

                    Column {
                        anchors.centerIn: parent
                        spacing: 2

                        Text {
                            text: modelData.value
                            color: "#171a1f"
                            font.pixelSize: 14
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            text: modelData.label
                            color: "#68707d"
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 12 }

        Button {
            text: "Close"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 28
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
            onClicked: root.hide()
        }
    }

    QtObject {
        id: latencyModel
        property string currentLatency: backend.get_latency()
        property real minLatency: 1.2
        property real maxLatency: 5.8
        property string avgLatency: "2.8"

        property var samples: [
            { value: 2.1 }, { value: 2.3 }, { value: 2.8 }, { value: 3.2 },
            { value: 2.0 }, { value: 1.8 }, { value: 2.4 }, { value: 2.6 },
            { value: 3.0 }, { value: 2.2 }, { value: 1.9 }, { value: 2.5 },
            { value: 2.7 }, { value: 3.1 }, { value: 2.3 }, { value: 2.0 },
            { value: 1.7 }, { value: 2.4 }, { value: 2.9 }, { value: 2.1 }
        ]
    }

    Timer {
        interval: 500
        running: root.visible
        repeat: true
        onTriggered: {
            var newVal = parseFloat(backend.get_latency())
            if (isNaN(newVal)) newVal = 2.4
            var arr = latencyModel.samples
            arr.shift()
            arr.push({ value: newVal })
            latencyModel.samples = arr
            latencyModel.currentLatency = newVal.toFixed(1) + "ms"

            var sum = 0, min = 999, max = 0
            for (var i = 0; i < arr.length; i++) {
                var v = arr[i].value
                sum += v
                if (v < min) min = v
                if (v > max) max = v
            }
            latencyModel.minLatency = min.toFixed(1)
            latencyModel.maxLatency = max.toFixed(1)
            latencyModel.avgLatency = (sum / arr.length).toFixed(1)
        }
    }
}
