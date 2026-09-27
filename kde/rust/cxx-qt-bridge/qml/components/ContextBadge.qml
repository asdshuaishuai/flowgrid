import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    height: 22
    radius: 999

    property string context: "Global"

    color: {
        switch (context) {
        case "Apps": return "#fff7e6"
        case "Game": return "#f3e8ff"
        default: return "#e8f7ff"
        }
    }

    implicitWidth: ctxText.width + 16

    Text {
        id: ctxText
        anchors.centerIn: parent
        text: root.context
        color: {
            switch (root.context) {
            case "Apps": return "#d48806"
            case "Game": return "#7c3aed"
            default: return "#0675b9"
            }
        }
        font.pixelSize: 10
        font.bold: true
    }
}
