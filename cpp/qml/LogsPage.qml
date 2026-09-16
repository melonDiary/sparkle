import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 30; spacing: 18
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout { Layout.fillWidth: true; spacing: 4
                Label { text: "运行日志"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label { text: (appModel ? appModel.logs.length : 0) + " 条记录"; color: Theme.textMuted; font.pixelSize: 13 }
            }
            Button { text: "刷新"; onClicked: appModel.refresh() }
        }
        TextField { id: search; Layout.fillWidth: true; placeholderText: "筛选日志内容"; selectByMouse: true }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; radius: 14; color: Theme.content2
            ListView {
                id: logs
                anchors.fill: parent; anchors.margins: 16; clip: true; spacing: 5
                model: appModel ? appModel.logs : []
                delegate: Label {
                    required property var modelData
                    visible: search.text === "" || modelData.payload.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                    width: logs.width; height: visible ? implicitHeight : 0
                    text: "[" + modelData.level + "] " + modelData.payload
                    color: modelData.level === "error" ? Theme.danger : modelData.level === "warning" ? Theme.warning : Theme.textMuted
                    font.family: "Menlo"
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
