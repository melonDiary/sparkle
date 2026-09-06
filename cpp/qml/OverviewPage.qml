import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    background: Rectangle { color: "#1e1f2b" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "概况"; color: "#f5f5f5"; font.pixelSize: 28; font.bold: true }
                Label { text: "实时查看 Sparkle 的运行状态"; color: "#9399b2"; font.pixelSize: 13 }
            }
            Button { text: "刷新数据"; onClicked: appModel.refresh() }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 128
            radius: 16
            color: "#292c3c"
            border.width: 1
            border.color: appModel && appModel.running ? "#a6e3a1" : "#45475a"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16
                Rectangle {
                    width: 54
                    height: 54
                    radius: 27
                    color: appModel && appModel.running ? "#a6e3a1" : "#45475a"
                    Text { anchors.centerIn: parent; text: appModel && appModel.running ? "✓" : "–"; color: "#11111b"; font.pixelSize: 26; font.bold: true }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label { text: appModel && appModel.running ? "代理运行中" : "代理已停止"; color: "#eef0ff"; font.pixelSize: 20; font.bold: true }
                    Label { text: appModel && appModel.controllerVersion !== "" ? "控制器版本 " + appModel.controllerVersion : "等待控制器连接"; color: "#9399b2"; font.pixelSize: 13 }
                }
                Label { text: appModel ? appModel.coreState : "unknown"; color: "#89b4fa"; font.pixelSize: 14 }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 720 ? 4 : 2
            rowSpacing: 12
            columnSpacing: 12

            Rectangle {
                Layout.fillWidth: true; implicitHeight: 100; radius: 12; color: "#292c3c"
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 8
                    Label { text: "代理节点"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel ? appModel.proxies.length : 0; color: "#cdd6f4"; font.pixelSize: 24; font.bold: true }
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 100; radius: 12; color: "#292c3c"
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 8
                    Label { text: "分流规则"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel ? appModel.rules.length : 0; color: "#cdd6f4"; font.pixelSize: 24; font.bold: true }
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 100; radius: 12; color: "#292c3c"
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 8
                    Label { text: "上行速度"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel && appModel.traffic.upload !== undefined ? (appModel.traffic.upload / 1024 / 1024).toFixed(2) + " MB/s" : "0 MB/s"; color: "#cdd6f4"; font.pixelSize: 19; font.bold: true }
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 100; radius: 12; color: "#292c3c"
                Column { anchors.fill: parent; anchors.margins: 16; spacing: 8
                    Label { text: "下行速度"; color: "#9399b2"; font.pixelSize: 12 }
                    Label { text: appModel && appModel.traffic.download !== undefined ? (appModel.traffic.download / 1024 / 1024).toFixed(2) + " MB/s" : "0 MB/s"; color: "#cdd6f4"; font.pixelSize: 19; font.bold: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: "#242635"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12
                Label { text: "最近活动"; color: "#eef0ff"; font.pixelSize: 16; font.bold: true }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: appModel ? appModel.logs : []
                    delegate: Label {
                        required property var modelData
                        width: parent.width
                        text: "[" + modelData.level + "]  " + modelData.payload
                        color: modelData.level === "error" ? "#f38ba8" : "#a6adc8"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Label { anchors.centerIn: parent; visible: parent.count === 0; text: "暂无活动记录"; color: "#6c7086" }
                }
            }
        }
    }
}
