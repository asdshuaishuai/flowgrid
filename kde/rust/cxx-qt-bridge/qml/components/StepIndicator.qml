import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Row {
    id: root
    spacing: 0
    height: 22

    property int currentStep: 0
    property int totalSteps: 3

    Repeater {
        model: root.totalSteps

        Row {
            spacing: 0

            // Connector line before (except first)
            Rectangle {
                width: index === 0 ? 0 : 20
                height: 2
                color: index <= root.currentStep ? "#3daee9" : "#d9dee7"
                anchors.verticalCenter: parent.verticalCenter
            }

            // Circle
            Rectangle {
                width: 22; height: 22; radius: 11
                color: index <= root.currentStep ? "#3daee9" : "transparent"
                border.color: index <= root.currentStep ? "#3daee9" : "#d9dee7"
                border.width: 2
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: index + 1
                    color: index <= root.currentStep ? "#ffffff" : "#68707d"
                    font.pixelSize: 10
                    font.bold: true
                }
            }

            // Connector line after (except last)
            Rectangle {
                width: index === root.totalSteps - 1 ? 0 : 20
                height: 2
                color: index < root.currentStep ? "#3daee9" : "#d9dee7"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
