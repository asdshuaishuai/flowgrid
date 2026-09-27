import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    width: 550
    height: 560
    minimumWidth: 520
    maximumWidth: 580
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    function show() {
        visible = true
        keymapListView.model = backend.get_keymap_rules()
    }
    function hide() { visible = false; keymapLoader.active = false }

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
            text: "Key Mapping"
            color: "#171a1f"
            font.pixelSize: 18
            font.bold: true
        }

        Item { Layout.preferredHeight: 14 }

        RowLayout {
            Layout.fillWidth: true
            height: 32
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                radius: 6
                color: "#ffffff"
                border.color: "#d9dee7"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 6

                    Text {
                        text: "🔍"
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignVCenter
                    }
                    TextInput {
                        id: searchInput
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        font.pixelSize: 12
                        color: "#171a1f"
                        clip: true
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Search rules..."
                            color: "#68707d"
                            font.pixelSize: 12
                            visible: !searchInput.text && !searchInput.activeFocus
                        }
                    }
                }
            }

            Row {
                spacing: 6
                Layout.alignment: Qt.AlignVCenter

                Repeater {
                    model: ["All", "Global", "Apps", "Game"]

                    Rectangle {
                        width: pillText.width + 18
                        height: 26
                        radius: 999
                        color: filterPillMouseArea.containsMouse || (index === 0) ? "#3daee9" : "#ffffff"
                        border.color: index === 0 ? "#3daee9" : "#d9dee7"
                        border.width: 1

                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: modelData
                            color: (filterPillMouseArea.containsMouse || index === 0) ? "#ffffff" : "#68707d"
                            font.pixelSize: 11
                            font.bold: true
                        }

                        MouseArea {
                            id: filterPillMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 12 }

        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "#f1f5f9"
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 0

                Text {
                    text: "SOURCE"
                    color: "#5d6878"
                    font.pixelSize: 10
                    font.bold: true
                    Layout.preferredWidth: 120
                }
                Text {
                    text: "TARGET"
                    color: "#5d6878"
                    font.pixelSize: 10
                    font.bold: true
                    Layout.preferredWidth: 120
                }
                Text {
                    text: "CONTEXT"
                    color: "#5d6878"
                    font.pixelSize: 10
                    font.bold: true
                    Layout.preferredWidth: 100
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "ACTIONS"
                    color: "#5d6878"
                    font.pixelSize: 10
                    font.bold: true
                    Layout.alignment: Qt.AlignRight
                }
            }
        }

        ListView {
            id: keymapListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1

            model: backend.get_keymap_rules()

            delegate: Rectangle {
                width: ListView.view.width
                height: 44
                color: rowArea.containsMouse ? "#f8fafc" : "transparent"
                radius: 4

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 0

                    Row {
                        spacing: 4
                        Layout.preferredWidth: 120
                        Layout.alignment: Qt.AlignVCenter

                        KeyCap { text: modelData.from_key }
                    }

                    Row {
                        spacing: 4
                        Layout.preferredWidth: 120
                        Layout.alignment: Qt.AlignVCenter

                        KeyCap { text: modelData.to_key }
                    }

                    ContextBadge {
                        context: modelData.context
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item { Layout.fillWidth: true }

                    Row {
                        spacing: 4
                        Layout.alignment: Qt.AlignVCenter | Qt.AlignRight

                        Button {
                            width: 26; height: 26
                            flat: true
                            text: "✎"
                            font.pixelSize: 12
                            onClicked: { /* edit */ }
                        }
                        Button {
                            width: 26; height: 26
                            flat: true
                            text: "🗑"
                            font.pixelSize: 12
                            onClicked: backend.remove_keymap_rule(index)
                        }
                    }
                }

                MouseArea {
                    id: rowArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }
        }

        Item { Layout.preferredHeight: 10 }

        Button {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 32
            text: "+ Add Rule"
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
                color: parent.down ? "#2a9ad4" : "#3daee9"
                implicitWidth: 120
            }
            onClicked: { /* open add rule dialog */ }
        }
    }
}
