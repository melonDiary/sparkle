import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "概况"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label { text: "实时查看 Sparkle 的运行状态"; color: Theme.textMuted; font.pixelSize: 13 }
            }
            Button { text: "刷新数据"; onClicked: appModel.refresh() }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 128
            radius: 16
            color: Theme.content1
            border.width: 1
            border.color: appModel && appModel.running ? Theme.success : Theme.content3

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16
                Rectangle {
                    width: 54
                    height: 54
                    radius: 27
                    color: appModel && appModel.running ? Theme.success : Theme.content3
                    Text { anchors.centerIn: parent; text: appModel && appModel.running ? "✓" : "–"; color: Theme.background; font.pixelSize: 26; font.bold: true }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label { text: appModel && appModel.running ? "代理运行中" : "代理已停止"; color: Theme.text; font.pixelSize: 20; font.bold: true }
                    Label { text: appModel && appModel.controllerVersion !== "" ? "控制器版本 " + appModel.controllerVersion : "等待控制器连接"; color: Theme.textMuted; font.pixelSize: 13 }
                }
                Label { text: appModel ? appModel.coreState : "unknown"; color: Theme.primary; font.pixelSize: 14 }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 720 ? 4 : 2
            rowSpacing: 12
            columnSpacing: 12

            Repeater {
                model: [
                    { label: "代理节点", value: appModel ? String(appModel.proxies.length) : "0" },
                    { label: "分流规则", value: appModel ? String(appModel.rules.length) : "0" },
                    { label: "上行速度", value: appModel && appModel.traffic.upload !== undefined ? (appModel.traffic.upload / 1024 / 1024).toFixed(2) + " MB/s" : "0 MB/s" },
                    { label: "下行速度", value: appModel && appModel.traffic.download !== undefined ? (appModel.traffic.download / 1024 / 1024).toFixed(2) + " MB/s" : "0 MB/s" },
                    { label: "活动连接", value: appModel ? String(appModel.connectionCount) : "0" },
                    { label: "内核内存", value: appModel && appModel.memory.inUse !== undefined ? (appModel.memory.inUse / 1024 / 1024).toFixed(0) + " MB" : "—" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true; implicitHeight: 100; radius: 12; color: Theme.content1
                    Column { anchors.fill: parent; anchors.margins: 16; spacing: 8
                        Label { text: modelData.label; color: Theme.textMuted; font.pixelSize: 12 }
                        Label { text: modelData.value; color: Theme.text; font.pixelSize: 22; font.bold: true }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: Theme.content2
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Label { text: "最近活动"; color: Theme.text; font.pixelSize: 16; font.bold: true }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: appModel ? appModel.logs : []
                    delegate: Label {
                        required property var modelData
                        width: parent.width
                        text: "[" + modelData.level + "]  " + modelData.payload
                        color: modelData.level === "error" ? Theme.danger : Theme.textMuted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label { anchors.centerIn: parent; visible: parent.count === 0; text: "暂无活动记录"; color: Theme.textDim }
                }
            }
        }
    }
}
