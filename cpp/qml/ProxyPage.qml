import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    title: "代理"

    property var appModel
    property var scriptBridge
    background: Rectangle { color: Theme.background }

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
                    color: Theme.text
                    font.pixelSize: 28
                    font.bold: true
                }
                Label {
                    text: appModel && appModel.running ? "内核正在运行 · 节点状态实时同步" : "启动内核后可查看实时节点状态"
                    color: Theme.textMuted
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
            Repeater {
                model: [
                    { label: "节点数量", value: appModel ? String(appModel.proxies.length) : "0" },
                    { label: "代理组", value: appModel ? String(Math.max(0, appModel.groupNames.length - 1)) : "0" },
                    { label: "控制器", value: appModel && appModel.controllerVersion !== "" ? appModel.controllerVersion : "未连接" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 76
                    radius: 12
                    color: Theme.content1
                    Column {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 5
                        Label { text: modelData.label; color: Theme.textMuted; font.pixelSize: 12 }
                        Label { text: modelData.value; color: Theme.text; font.pixelSize: 22; font.bold: true }
                    }
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
                color: mouseArea.containsMouse ? Theme.hover : Theme.content1
                border.width: modelData.current ? 2 : (modelData.alive ? 1 : 0)
                border.color: modelData.current ? Theme.primary : Theme.success

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: { if (appModel) appModel.activateNode(modelData.name) }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14
                    Rectangle {
                        width: 9
                        height: 9
                        radius: 5
                        color: modelData.current ? Theme.primary : (modelData.alive ? Theme.success : Theme.textDim)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label {
                            text: (modelData.name || "未命名节点") + (modelData.current ? "  ·  当前" : "")
                            color: modelData.current ? Theme.primary : Theme.text
                            font.bold: true
                            font.pixelSize: 15
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: (modelData.type || "Unknown") + (modelData.provider ? " · " + modelData.provider : "")
                            color: Theme.textMuted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Column {
                        spacing: 3
                        Label { text: modelData.delay >= 0 ? modelData.delay + " ms" : "未测试"; color: modelData.alive ? Theme.success : Theme.textMuted; font.bold: true; horizontalAlignment: Text.AlignRight; width: 70 }
                        Label { text: modelData.server || "本地节点"; color: Theme.textDim; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; width: 110; elide: Text.ElideRight }
                    }
                    Button {
                        text: "测速"
                        flat: true
                        onClicked: { if (appModel) appModel.testNodeDelay(modelData.name) }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                visible: proxyList.count === 0
                spacing: 8
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "◈"; color: Theme.content3; font.pixelSize: 36 }
                Label { text: "暂无代理节点"; color: Theme.textMuted; font.pixelSize: 14 }
            }
        }
    }
}
