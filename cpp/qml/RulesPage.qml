import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    background: Rectangle { color: "#1e1f2b" }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 18
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { Layout.fillWidth: true; spacing: 4
                Label { text: "分流规则"; color: "#f5f5f5"; font.pixelSize: 28; font.bold: true }
                Label { text: (appModel ? appModel.rules.length : 0) + " 条规则"; color: "#9399b2"; font.pixelSize: 13 }
            }
            Button { text: "刷新"; onClicked: appModel.refresh() }
        }
        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "搜索类型、匹配内容或代理名称"
            selectByMouse: true
        }
        ListView {
            id: ruleList
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
            model: appModel ? appModel.rules : []
            delegate: Rectangle {
                required property var modelData
                visible: search.text === "" || modelData.type.toLowerCase().indexOf(search.text.toLowerCase()) >= 0 || modelData.payload.toLowerCase().indexOf(search.text.toLowerCase()) >= 0 || modelData.proxy.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                width: ruleList.width; height: visible ? 64 : 0; radius: 10; color: "#292c3c"
                RowLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 12
                    Label { text: "#" + (modelData.index + 1); color: "#6c7086"; Layout.preferredWidth: 34 }
                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                        Label { text: modelData.type; color: "#89b4fa"; font.bold: true }
                        Label { text: modelData.payload || "匹配全部请求"; color: "#cdd6f4"; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Label { text: modelData.proxy || "DIRECT"; color: modelData.disabled ? "#6c7086" : "#a6e3a1" }
                    Switch {
                        checked: !modelData.disabled
                        text: checked ? "启用" : "禁用"
                        enabled: appModel !== null
                        onToggled: appModel.setRuleDisabled(modelData.index, !checked)
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: ruleList.count === 0
                text: search.text === "" ? "暂无规则" : "没有匹配的规则"
                color: "#6c7086"
            }
        }
    }
}
