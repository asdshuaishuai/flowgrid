import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    width: 450
    height: 480
    minimumWidth: 420
    maximumWidth: 480
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    visible: false

    property bool hasUnsavedChanges: false

    function show() {
        visible = true
        for (var i = 0; i < settingsListModel.count; i++) {
            var item = settingsListModel.get(i)
            if (item.control === "toggle") {
                settingsListModel.setProperty(i, "value", backend.get_setting(item.key))
            }
        }
    }
    function hide() { visible = false; settingsLoader.active = false }

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
            text: "Settings"
            color: "#171a1f"
            font.pixelSize: 18
            font.bold: true
        }

        Item { Layout.preferredHeight: 16 }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 1

            model: ListModel {
                id: settingsListModel
                ListElement { title: "Auto-discovery"; description: "Automatically discover nearby FlowGrid devices on startup"; control: "toggle"; key: "autoConnect"; value: true }
                ListElement { title: "Preferred Protocol"; description: "Choose the transport protocol for connections"; control: "badge"; key: "preferredProtocol"; value: "Auto" }
                ListElement { title: "Mouse Smoothing"; description: "Interpolate high-DPI mouse movements for smoother cursor"; control: "toggle"; key: "mouseSmoothing"; value: false }
                ListElement { title: "DTLS Encryption"; description: "Encrypt all HID traffic with DTLS 1.3"; control: "toggle"; key: "dtls"; value: true }
                ListElement { title: "Clipboard Sync"; description: "Synchronize clipboard content between devices"; control: "toggle"; key: "clipboardSync"; value: true }
                ListElement { title: "Key Mapping"; description: "Enable key remapping between different platforms"; control: "toggle"; key: "keyMapping"; value: true }
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 64
                color: "rgba(255,255,255,0.9)"
                radius: 4
                border.color: "rgba(109,122,140,0.14)"
                border.width: 1

                property bool toggleValue: model.value
                property string badgeValue: model.value

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Layout.alignment: Qt.AlignVCenter

                        Text {
                            text: model.title
                            color: "#171a1f"
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }
                        Text {
                            text: model.description
                            color: "#68707d"
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                            Layout.preferredWidth: parent.width - 20
                        }
                    }

                    Rectangle {
                        visible: model.control === "toggle"
                        width: 34; height: 18; radius: 9
                        color: parent.parent.toggleValue ? "#18a058" : "#cccccc"
                        Layout.alignment: Qt.AlignVCenter

                        Rectangle {
                            width: 14; height: 14; radius: 7
                            color: "#ffffff"
                            anchors.verticalCenter: parent.verticalCenter
                            x: parent.parent.toggleValue ? parent.width - width - 2 : 2
                            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                parent.parent.toggleValue = !parent.parent.toggleValue
                                settingsListModel.setProperty(index, "value", parent.parent.toggleValue)
                                root.hasUnsavedChanges = true
                            }
                        }
                    }

                    Rectangle {
                        visible: model.control === "badge"
                        width: badgeLbl.width + 16
                        height: 22
                        radius: 999
                        color: "#e8f7ff"
                        Layout.alignment: Qt.AlignVCenter

                        Text {
                            id: badgeLbl
                            anchors.centerIn: parent
                            text: parent.parent.badgeValue
                            color: "#0675b9"
                            font.pixelSize: 11
                            font.bold: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var vals = ["NearLink", "DirectLink", "Auto"]
                                var idx = vals.indexOf(parent.parent.badgeValue)
                                parent.parent.badgeValue = vals[(idx + 1) % vals.length]
                                settingsListModel.setProperty(index, "value", parent.parent.badgeValue)
                                root.hasUnsavedChanges = true
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.preferredHeight: 12 }

        Rectangle {
            Layout.fillWidth: true
            height: 48
            color: "#fafafa"
            radius: 4
            border.color: "#d9dee7"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Rectangle {
                    width: unsavedText.width + 12
                    height: 20
                    radius: 999
                    color: "#fef3c7"
                    visible: root.hasUnsavedChanges

                    Text {
                        id: unsavedText
                        anchors.centerIn: parent
                        text: "Unsaved"
                        color: "#d97706"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Cancel"
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
                    onClicked: {
                        root.hasUnsavedChanges = false
                        root.hide()
                    }
                }

                Button {
                    text: "Save"
                    Layout.preferredHeight: 28
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
                    }
                    onClicked: {
                        for (var i = 0; i < settingsListModel.count; i++) {
                            var item = settingsListModel.get(i)
                            if (item.control === "toggle") {
                                backend.set_setting(item.key, item.value)
                            }
                        }
                        backend.save_settings()
                        root.hasUnsavedChanges = false
                        root.hide()
                    }
                }
            }
        }
    }
}
