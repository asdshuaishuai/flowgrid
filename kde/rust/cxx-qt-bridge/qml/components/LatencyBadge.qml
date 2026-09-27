import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    width: latencyText.width + 16
    height: 26
    radius: 999

    property string value: "--"

    color: {
        var v = parseFloat(value)
        if (isNaN(v)) return "#e8fbf1"
        if (v < 2) return "#e8fbf1"
        if (v < 4) return "#fef3c7"
        return "#fee2e2"
    }

    Text {
        id: latencyText
        anchors.centerIn: parent
        text: root.value
        color: {
            var v = parseFloat(root.value)
            if (isNaN(v)) return "#07966c"
            if (v < 2) return "#07966c"
            if (v < 4) return "#d97706"
            return "#dc2626"
        }
        font.pixelSize: 12
        font.bold: true
    }
}
