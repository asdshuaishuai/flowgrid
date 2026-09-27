import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    width: 7
    height: 7
    radius: 999
    color: "#10a36f"

    // Sequential pulse animation on opacity, 2s infinite
    SequentialAnimation on opacity {
        loops: Animation.Infinite
        NumberAnimation { from: 1.0; to: 0.4; duration: 1000 }
        NumberAnimation { from: 0.4; to: 1.0; duration: 1000 }
    }
}
