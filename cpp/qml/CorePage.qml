import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    property var scriptBridge
    background: Rectangle { color: "#1e1f2b" }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 22
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { Layout.fillWidth: true; spacing: 4
                Label { text: "内核管理"; color: "#f5f5f5"; font.pixelSize: 28; font.bold: true }
                Label { text: "通过脚本桥接控制 Mihomo 生命周期"; color: "#9399b2"; font.pixelSize: 13 }
            }
            Button { text: "重启代理"; enabled: scriptBridge && scriptBridge.available; onClicked: scriptBridge.restartProxy() }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 150; radius: 16; color: "#292c3c"
            RowLayout {
                anchors.fill: parent; anchors.margins: 22
                Rectangle { width: 62; height: 62; radius: 31; color: appModel && appModel.running ? "#a6e3a1" : "#45475a"; Text { anchors.centerIn: parent; text: appModel && appModel.running ? "✓" : "–"; color: "#11111b"; font.pixelSize: 28; font.bold: true } }
                ColumnLayout { Layout.fillWidth: true; spacing: 7
                    Label { text: appModel && appModel.running ? "运行中" : "已停止"; color: "#eef0ff"; font.pixelSize: 21; font.bold: true }
                    Label { text: "状态：" + (appModel ? appModel.coreState : "unknown"); color: "#9399b2" }
                }
                Label { text: appModel && appModel.controllerVersion !== "" ? "控制器 " + appModel.controllerVersion : "控制器未连接"; color: "#89b4fa" }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 16; color: "#242635"
            ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
                Label { text: "运行说明"; color: "#eef0ff"; font.pixelSize: 16; font.bold: true }
                Label { text: "启动与停止操作通过 ScriptBridge 转发到可热修改的 JavaScript。\n内核日志和状态变化会实时返回 QML 数据模型。"; color: "#9399b2"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
