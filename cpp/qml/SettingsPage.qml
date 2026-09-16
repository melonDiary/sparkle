import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 22
        Label { text: "设置"; color: Theme.text; font.pixelSize: 28; font.bold: true }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; radius: 14; color: Theme.content1
            RowLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Label { text: "系统代理"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                    Label { text: appModel && appModel.systemProxyEnabled ? "系统流量将通过 Sparkle" : "系统代理当前未启用"; color: Theme.textMuted; font.pixelSize: 12 }
                }
                Switch {
                    checked: appModel ? appModel.systemProxyEnabled : false
                    onToggled: if (appModel) appModel.setSystemProxyEnabled(checked)
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; radius: 14; color: Theme.content1
            RowLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Label { text: "MITM 中间人代理"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                    Label { text: appModel && appModel.mitmEnabled ? "已启用，将在内核启动时监听并执行脚本规则" : "拦截并改写 HTTP 请求（QuickJS 脚本驱动）"; color: Theme.textMuted; font.pixelSize: 12 }
                }
                Switch {
                    checked: appModel ? appModel.mitmEnabled : false
                    onToggled: if (appModel) appModel.setMitmEnabled(checked)
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; radius: 14; color: Theme.content1
            RowLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Label { text: "开机自启"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                    Label { text: appModel && appModel.autostartEnabled ? "已加入系统登录项" : "登录后自动启动 Sparkle"; color: Theme.textMuted; font.pixelSize: 12 }
                }
                Switch {
                    checked: appModel ? appModel.autostartEnabled : false
                    onToggled: if (appModel) appModel.setAutostartEnabled(checked)
                }
            }
        }
        // 出站模式（原版 sider 顶部的 OutboundModeSwitcher，也在设置里可切）
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 88; radius: 14; color: Theme.content1
            RowLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 14
                ColumnLayout { Layout.fillWidth: true; spacing: 4
                    Label { text: "出站模式"; color: Theme.text; font.pixelSize: 15; font.bold: true }
                    Label { text: "规则 / 全局 / 直连，切换后内核自动重载"; color: Theme.textMuted; font.pixelSize: 12 }
                }
                ComboBox {
                    implicitWidth: 130
                    model: ["rule", "global", "direct"]
                    currentIndex: appModel ? ["rule", "global", "direct"].indexOf(appModel.outboundMode) : 0
                    onActivated: if (appModel) appModel.requestOutboundMode(currentText)
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
