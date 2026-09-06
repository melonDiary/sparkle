import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1180
    height: 760
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: "Sparkle"
    color: "#1e1f2b"

    property var appModel
    property var scriptBridge
    property int currentPage: 0

    header: ToolBar {
        height: 64
        background: Rectangle { color: "#1e1f2b"; border.color: "#292c3c"; border.width: 1 }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 10
            Label {
                text: ["概况", "代理", "规则", "日志", "设置", "内核"][root.currentPage]
                color: "#eef0ff"
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
            }
            Rectangle {
                implicitWidth: 10; implicitHeight: 10; radius: 5
                color: appModel && appModel.running ? "#a6e3a1" : "#6c7086"
            }
            Label { text: appModel && appModel.running ? "运行中" : "已停止"; color: "#9399b2"; font.pixelSize: 12 }
            Button {
                text: "启动"
                enabled: appModel && !appModel.running && scriptBridge && scriptBridge.available
                onClicked: {
                    if (!scriptBridge.startProxy()) appModel.setStatusMessage("启动脚本执行失败")
                }
            }
            Button {
                text: "停止"
                enabled: appModel && appModel.running && scriptBridge && scriptBridge.available
                onClicked: {
                    if (!scriptBridge.stopProxy()) appModel.setStatusMessage("停止脚本执行失败")
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Sidebar {
            id: sidebar
            Layout.fillHeight: true
            currentIndex: root.currentPage
            onPageSelected: root.currentPage = index
        }
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentPage
            OverviewPage { appModel: root.appModel }
            ProxyPage { appModel: root.appModel; scriptBridge: root.scriptBridge }
            RulesPage { appModel: root.appModel }
            LogsPage { appModel: root.appModel }
            SettingsPage { appModel: root.appModel }
            CorePage { appModel: root.appModel; scriptBridge: root.scriptBridge }
        }
    }
}
