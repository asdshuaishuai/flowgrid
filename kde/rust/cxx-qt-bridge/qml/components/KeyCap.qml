import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    height: 28
    radius: 4
    color: "#ffffff"
    border.color: "rgba(80,94,112,0.22)"
    border.width: 1

    property string text: ""

    implicitWidth: Math.max(34, keyText.width + 16)

    Text {
        id: keyText
        anchors.centerIn: parent
        text: root.text
        color: "#171a1f"
        font.pixelSize: 11
        font.bold: true
    }
}
