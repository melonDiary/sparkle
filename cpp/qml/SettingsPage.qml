import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    background: Rectangle { color: "#1e1f2b" }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 22
        Label { text: "设置"; color: "#f5f5f5"; font.pixelSize: 28; font.bold: true }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; radius: 14; color: "#292c3c"
            RowLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Label { text: "系统代理"; color: "#eef0ff"; font.pixelSize: 15; font.bold: true }
                    Label { text: appModel && appModel.systemProxyEnabled ? "系统流量将通过 Sparkle" : "系统代理当前未启用"; color: "#9399b2"; font.pixelSize: 12 }
                }
                Switch {
                    checked: appModel ? appModel.systemProxyEnabled : false
                    onToggled: if (appModel) appModel.setSystemProxyEnabled(checked)
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; radius: 14; color: "#292c3c"
            RowLayout {
                anchors.fill: parent; anchors.margins: 18
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Label { text: "脚本驱动操作"; color: "#eef0ff"; font.pixelSize: 15; font.bold: true }
                    Label { text: "按钮行为由 QuickJS 脚本控制，无需重新编译"; color: "#9399b2"; font.pixelSize: 12 }
                }
                Label { text: "已启用"; color: "#a6e3a1"; font.bold: true }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
