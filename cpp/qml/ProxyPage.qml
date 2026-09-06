import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    title: "代理"

    property var appModel
    property var scriptBridge
    background: Rectangle { color: "#1e1f2b" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label {
                    text: "代理节点"
                    color: "#f5f5f5"
                    font.pixelSize: 28
                    font.bold: true
                }
                Label {
                    text: appModel && appModel.running ? "内核正在运行 · 节点状态实时同步" : "启动内核后可查看实时节点状态"
                    color: "#9399b2"
                    font.pixelSize: 13
                }
            }
            ComboBox {
                id: groupSelector
                Layout.preferredWidth: 170
                model: appModel ? appModel.groupNames : []
                currentIndex: Math.max(0, model.indexOf(appModel ? appModel.selectedGroup : "全部"))
                onActivated: {
                    if (appModel) appModel.setSelectedGroup(currentText)
                }
            }
            Button {
                text: "刷新"
                enabled: !!appModel
                onClicked: appModel.refresh()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 76
                radius: 12
                color: "#292c3c"
                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 5
                    Label { text: "节点数量"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel ? appModel.proxies.length : 0; color: "#cdd6f4"; font.pixelSize: 22; font.bold: true }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 76
                radius: 12
                color: "#292c3c"
                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 5
                    Label { text: "代理组"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel ? Math.max(0, appModel.groupNames.length - 1) : 0; color: "#cdd6f4"; font.pixelSize: 22; font.bold: true }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 76
                radius: 12
                color: "#292c3c"
                Column {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 5
                    Label { text: "控制器"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel && appModel.controllerVersion !== "" ? appModel.controllerVersion : "未连接"; color: appModel && appModel.controllerVersion !== "" ? "#a6e3a1" : "#f9e2af"; font.pixelSize: 18; font.bold: true }
                }
            }
        }

        ListView {
            id: proxyList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: appModel ? appModel.proxies : []

            delegate: Rectangle {
                required property var modelData
                width: proxyList.width
                height: 72
                radius: 12
                color: mouseArea.containsMouse ? "#34384d" : "#292c3c"
                border.width: modelData.alive ? 1 : 0
                border.color: "#a6e3a1"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14
                    Rectangle {
                        width: 9
                        height: 9
                        radius: 5
                        color: modelData.alive ? "#a6e3a1" : "#6c7086"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label { text: modelData.name || "未命名节点"; color: "#eef0ff"; font.bold: true; font.pixelSize: 15 }
                        Label { text: (modelData.type || "Unknown") + (modelData.provider ? " · " + modelData.provider : ""); color: "#9399b2"; font.pixelSize: 12 }
                    }
                    Column {
                        spacing: 3
                        Label { text: modelData.delay >= 0 ? modelData.delay + " ms" : "未测试"; color: modelData.alive ? "#a6e3a1" : "#9399b2"; font.bold: true; horizontalAlignment: Text.AlignRight; width: 70 }
                        Label { text: modelData.server || "本地节点"; color: "#6c7086"; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; width: 110; elide: Text.ElideRight }
                    }
                }
                MouseArea { id: mouseArea; anchors.fill: parent; hoverEnabled: true }
            }

            Column {
                anchors.centerIn: parent
                visible: proxyList.count === 0
                spacing: 8
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "◈"; color: "#45475a"; font.pixelSize: 36 }
                Label { text: "暂无代理节点"; color: "#7f849c"; font.pixelSize: 14 }
            }
        }
    }
}
