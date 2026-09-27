import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: parent ? parent.width : 360
    height: 64
    radius: 7
    color: connected ? "rgba(255,255,255,0.88)" : "rgba(255,255,255,0.45)"
    border.color: "rgba(109,122,140,0.18)"
    border.width: 1

    // Props
    property string iconText: icon ? icon.substring(0, 2).toUpperCase() : "??"
    property string icon: ""
    property string name: "Device"
    property string meta: ""
    property string latency: "--"
    property bool connected: false
    property string transportType: ""

    RowLayout {
        anchors.fill: parent
        anchors.margins: 11
        spacing: 10

        // Icon
        Rectangle {
            width: 42; height: 42; radius: 8
            color: "#e6f2ff"
            Layout.alignment: Qt.AlignVCenter

            Text {
                anchors.centerIn: parent
                text: root.iconText
                color: "#3daee9"
                font.pixelSize: 12
                font.bold: true
            }
        }

        // Name + Meta
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Layout.alignment: Qt.AlignVCenter

            Text {
                text: root.name
                color: "#171a1f"
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            Text {
                text: root.meta + (root.transportType ? " · " + root.transportType : "")
                color: "#68707d"
                font.pixelSize: 11
            }
        }

        // Latency badge + actions
        Row {
            spacing: 8
            Layout.alignment: Qt.AlignVCenter

            LatencyBadge {
                value: root.latency
            }

            Row {
                spacing: 6

                Button {
                    width: 26; height: 26
                    flat: true
                    text: root.connected ? "⏻" : "↻"
                    font.pixelSize: 13
                    onClicked: {
                        // toggle connection
                    }
                }
                Button {
                    width: 26; height: 26
                    flat: true
                    text: "🗑"
                    font.pixelSize: 13
                    onClicked: {
                        // remove device
                    }
                }
            }
        }
    }
}
