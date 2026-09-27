import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

Window {
    id: mainWindow
    width: 520
    height: 680
    minimumWidth: 480
    maximumWidth: 580
    visible: true
    title: "FlowGrid"
    color: "#fbfcfd"

    // Rust backend instance (event-driven via signals)
    FlowGridBackend {
        id: backend
    }

    // Fast event queue consumer (16ms ~ 60fps) — drains pushed events, does NOT poll business data
    Timer {
        interval: 16
        running: true
        repeat: true
        onTriggered: backend.poll_events()
    }

    // Signal connections for reactive UI updates
    Connections {
        target: backend

        function onDevicesChanged() {
            deviceRepeater.model = backend.get_devices()
        }
        function onDeviceStateChanged(id, state) {
            console.log("Device state changed:", id, state)
            deviceRepeater.model = backend.get_devices()
        }
        function onDeviceRemoved(id) {
            console.log("Device removed:", id)
            deviceRepeater.model = backend.get_devices()
        }
        function onLatencyUpdated(id, latencyMs) {
            console.log("Latency updated:", id, latencyMs)
            deviceRepeater.model = backend.get_devices()
        }
        function onKeymapChanged() {
            keymapRepeater.model = backend.get_keymap_rules()
        }
        function onErrorOccured(message) {
            console.error("FlowGrid error:", message)
        }
        function onScanStateChanged(scanning) {
            console.log("Scan state:", scanning)
        }
    }

    // Titlebar (KDE Breeze style)
    Rectangle {
        id: titlebar
        anchors.top: parent.top
        width: parent.width
        height: 30
        color: "#31363b"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10

            Row {
                spacing: 7
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                Rectangle {
                    width: 16; height: 16; radius: 4; color: "#3daee9"
                }
                Text {
                    text: "FlowGrid"
                    color: "#eef1f4"
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            Text {
                text: "Connections"
                color: "#eef1f4"
                font.pixelSize: 12
                font.bold: true
                Layout.alignment: Qt.AlignCenter
            }

            Item { Layout.fillWidth: true }
        }
    }

    // Content area
    ScrollView {
        anchors.top: titlebar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: navBar.top
        anchors.margins: 0
        contentWidth: parent.width
        clip: true

        Column {
            width: parent.width
            spacing: 0

            // Toolbar
            Rectangle {
                width: parent.width; height: 48; color: "#fbfcfd"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14; anchors.rightMargin: 14

                    Row {
                        spacing: 8; Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter

                        Rectangle {
                            width: 7; height: 7; radius: 999
                            color: "#10a36f"

                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                NumberAnimation { from: 1.0; to: 0.4; duration: 1000 }
                                NumberAnimation { from: 0.4; to: 1.0; duration: 1000 }
                            }
                        }
                        Text {
                            id: connText
                            text: "MacBook Pro"
                            color: "#2f3a4d"
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: "Settings"
                        Layout.preferredHeight: 28
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

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
                            if (settingsLoader.item) settingsLoader.item.show();
                            else settingsLoader.active = true;
                        }
                    }
                }
            }

            // Device list
            Column {
                width: parent.width
                spacing: 8
                leftPadding: 14; rightPadding: 14

                Repeater {
                    id: deviceRepeater
                    model: backend.get_devices()

                    Rectangle {
                        width: parent.width - 28
                        height: 64
                        radius: 7
                        color: connected ? "rgba(255,255,255,0.88)" : "rgba(255,255,255,0.45)"
                        border.color: "rgba(109,122,140,0.18)"
                        border.width: 1

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 10

                            Rectangle {
                                width: 42; height: 42; radius: 8
                                color: "#e6f2ff"
                                Layout.alignment: Qt.AlignVCenter

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.name.toString().substring(0, 2).toUpperCase()
                                    color: "#3daee9"
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Layout.alignment: Qt.AlignVCenter

                                Text {
                                    text: modelData.name
                                    color: "#171a1f"
                                    font.pixelSize: 13
                                    font.weight: Font.Bold
                                }
                                Text {
                                    text: modelData.meta
                                    color: "#68707d"
                                    font.pixelSize: 11
                                }
                            }

                            Row {
                                spacing: 8; Layout.alignment: Qt.AlignVCenter

                                Rectangle {
                                    width: latencyText.width + 16; height: 26; radius: 999
                                    color: {
                                        var v = parseFloat(modelData.latency);
                                        if (v < 2) return "#e8fbf1";
                                        if (v < 4) return "#fef3c7";
                                        return "#fee2e2";
                                    }

                                    Text {
                                        id: latencyText
                                        anchors.centerIn: parent
                                        text: modelData.latency
                                        color: {
                                            var v = parseFloat(modelData.latency);
                                            if (v < 2) return "#07966c";
                                            if (v < 4) return "#d97706";
                                            return "#dc2626";
                                        }
                                        font.pixelSize: 12
                                        font.bold: true
                                    }
                                }

                                Row {
                                    spacing: 6

                                    Button {
                                        width: 26; height: 26
                                        flat: true
                                        text: connected ? "⏻" : "↻"
                                        font.pixelSize: 13
                                        onClicked: {
                                            if (connected) {
                                                backend.disconnect_device(modelData.id);
                                            } else {
                                                backend.connect_device(modelData.id);
                                            }
                                        }
                                    }
                                    Button {
                                        width: 26; height: 26
                                        flat: true
                                        text: "🗑"
                                        font.pixelSize: 13
                                        onClicked: backend.remove_device(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Collapsible sections
            // Key Mapping section
            Rectangle {
                width: parent.width - 28
                height: section1Col.height + 36
                radius: 7
                color: "rgba(255,255,255,0.74)"
                border.color: "rgba(109,122,140,0.18)"
                border.width: 1
                x: 14

                Column {
                    id: section1Col
                    width: parent.width
                    spacing: 0

                    MouseArea {
                        width: parent.width; height: 36
                        onClicked: keymapBody.visible = !keymapBody.visible

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10; anchors.rightMargin: 10

                            Text {
                                text: "Active Key Mapping"
                                color: "#171a1f"
                                font.pixelSize: 12
                                font.bold: true
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            }
                            Rectangle {
                                width: badge1Text.width + 14; height: 20; radius: 999
                                color: "#e8f7ff"
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Text {
                                    id: badge1Text
                                    anchors.centerIn: parent
                                    text: "3 rules"
                                    color: "#0675b9"
                                    font.pixelSize: 10
                                    font.bold: true
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: keymapBody.visible ? "▼" : "▶"
                                color: "#68707d"
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: "rgba(109,122,140,0.16)" }

                    Column {
                        id: keymapBody
                        width: parent.width
                        padding: 10
                        spacing: 8

                        GridLayout {
                            width: parent.width - 20
                            columns: 3
                            columnSpacing: 8; rowSpacing: 8

                            Repeater {
                                id: keymapRepeater
                                model: backend.get_keymap_rules()

                                Column {
                                    spacing: 5
                                    Layout.fillWidth: true

                                    Row {
                                        spacing: 5
                                        Layout.alignment: Qt.AlignHCenter

                                        Rectangle {
                                            width: Math.max(34, keyFromText.width + 14); height: 28; radius: 4
                                            color: "#ffffff"
                                            border.color: "rgba(80,94,112,0.22)"
                                            border.width: 1

                                            Text {
                                                id: keyFromText
                                                anchors.centerIn: parent
                                                text: modelData.from_key
                                                color: "#171a1f"
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }
                                        Text {
                                            text: "→"
                                            color: "#68707d"
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                        Rectangle {
                                            width: Math.max(34, keyToText.width + 14); height: 28; radius: 4
                                            color: "#ffffff"
                                            border.color: "rgba(80,94,112,0.22)"
                                            border.width: 1

                                            Text {
                                                id: keyToText
                                                anchors.centerIn: parent
                                                text: modelData.to_key
                                                color: "#171a1f"
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                        }
                                    }
                                    Text {
                                        text: modelData.context
                                        color: "#68707d"
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Protocol Health section
            Rectangle {
                width: parent.width - 28
                height: section2Col.height + 36
                radius: 7
                color: "rgba(255,255,255,0.74)"
                border.color: "rgba(109,122,140,0.18)"
                border.width: 1
                x: 14

                Column {
                    id: section2Col
                    width: parent.width
                    spacing: 0

                    MouseArea {
                        width: parent.width; height: 36
                        onClicked: healthBody.visible = !healthBody.visible

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10; anchors.rightMargin: 10

                            Text {
                                text: "Protocol Health"
                                color: "#171a1f"
                                font.pixelSize: 12
                                font.bold: true
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            }
                            Rectangle {
                                width: badge2Text.width + 14; height: 20; radius: 999
                                color: "#e8fbf1"
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Text {
                                    id: badge2Text
                                    anchors.centerIn: parent
                                    text: "Stable"
                                    color: "#07966c"
                                    font.pixelSize: 10
                                    font.bold: true
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: healthBody.visible ? "▼" : "▶"
                                color: "#68707d"
                                font.pixelSize: 12
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            }
                        }
                    }

                    Rectangle { width: parent.width; height: 1; color: "rgba(109,122,140,0.16)" }

                    GridLayout {
                        id: healthBody
                        width: parent.width - 20
                        columns: 2
                        columnSpacing: 8; rowSpacing: 8
                        x: 10; padding: 10

                        Repeater {
                            model: [
                                { label: "Latency", value: "2.8ms" },
                                { label: "Packet Loss", value: "0.01%" },
                                { label: "Wi-Fi Channel", value: "149" },
                                { label: "Uptime", value: "99.9%" }
                            ]

                            Rectangle {
                                width: (parent.width - 8) / 2; height: 64
                                radius: 6
                                color: "#f8fafc"

                                Column {
                                    anchors.centerIn: parent
                                    spacing: 3

                                    Text {
                                        text: modelData.value
                                        color: "#171a1f"
                                        font.pixelSize: 16
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
                }
            }
        }
    }

    // Navigation bar
    Rectangle {
        id: navBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: 48
        color: "#fafafa"
        border.color: "#d9dee7"
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14; anchors.rightMargin: 14
            spacing: 6

            Button {
                text: "Keymap Editor"
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
                    if (keymapLoader.item) keymapLoader.item.show();
                    else keymapLoader.active = true;
                }
            }
            Button {
                text: "System Tray"
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
                    if (trayPopupLoader.item) trayPopupLoader.item.show();
                    else trayPopupLoader.active = true;
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Add Device"
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
                    color: "#3daee9"
                }
                onClicked: {
                    if (wizardLoader.item) wizardLoader.item.show();
                    else wizardLoader.active = true;
                }
            }
        }
    }

    // Loaders for sub-windows
    Loader {
        id: settingsLoader
        active: false
        source: "SettingsView.qml"
        onLoaded: item.show()
    }
    Loader {
        id: keymapLoader
        active: false
        source: "KeyMapperView.qml"
        onLoaded: item.show()
    }
    Loader {
        id: trayPopupLoader
        active: false
        source: "TrayPopup.qml"
        onLoaded: item.show()
    }
    Loader {
        id: wizardLoader
        active: false
        source: "AddDeviceWizard.qml"
        onLoaded: item.show()
    }
}
