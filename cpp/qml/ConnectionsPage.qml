import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 连接页（对应原版 /connections）：WS 实时连接列表 + 搜索过滤 + 单条/全部关闭。
Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    function formatSpeed(bytes) {
        const v = Number(bytes) || 0
        if (v >= 1024 * 1024) return (v / 1024 / 1024).toFixed(2) + " MB"
        if (v >= 1024) return (v / 1024).toFixed(1) + " KB"
        return v.toFixed(0) + " B"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "连接"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label {
                    text: (appModel ? appModel.connections.length : 0) + " 条活跃连接"
                    color: Theme.textMuted
                    font.pixelSize: 13
                }
            }
            Button {
                id: closeAllButton
                text: "关闭全部"
                flat: true
                enabled: appModel && appModel.connections.length > 0
                onClicked: appModel.closeAllConnections()
                background: Rectangle {
                    radius: 8
                    color: closeAllButton.enabled
                        ? (closeAllButton.hovered ? Theme.danger : Theme.content1) : Theme.content1
                }
                contentItem: Label {
                    text: closeAllButton.text
                    color: closeAllButton.enabled ? Theme.danger : Theme.textDim
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "搜索主机、进程、规则或链路"
            selectByMouse: true
        }

        // 表头
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 34
            radius: 8
            color: Theme.content2
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8
                Label { text: "主机 / 目标"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 260; elide: Text.ElideRight }
                Label { text: "规则"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 150; elide: Text.ElideRight }
                Label { text: "链路"; color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: "上行"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight }
                Label { text: "下行"; color: Theme.textDim; font.pixelSize: 12; Layout.preferredWidth: 70; horizontalAlignment: Text.AlignRight }
                Item { Layout.preferredWidth: 60 }
            }
        }

        ListView {
            id: connList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: appModel ? appModel.connections : []

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: connList.width
                height: 56
                radius: 10
                color: rowMouse.containsMouse ? Theme.hover : Theme.content1

                // 搜索过滤：不匹配的行折叠为 0 高。
                readonly property bool match: search.text === ""
                    || modelData.host.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                    || modelData.process.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                    || modelData.rule.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                    || modelData.chains.toLowerCase().indexOf(search.text.toLowerCase()) >= 0
                opacity: match ? 1 : 0

                MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 8

                    ColumnLayout {
                        Layout.preferredWidth: 260
                        spacing: 2
                        Label {
                            text: modelData.host
                            color: Theme.text
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: modelData.network.toUpperCase() + " · " + modelData.destination
                                  + (modelData.process !== "" ? " · " + modelData.process : "")
                            color: Theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        text: modelData.rule
                        color: Theme.textMuted
                        font.pixelSize: 12
                        Layout.preferredWidth: 150
                        elide: Text.ElideRight
                    }
                    Label {
                        text: modelData.chains
                        color: Theme.primary
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    Label {
                        text: root.formatSpeed(modelData.upload)
                        color: Theme.textMuted
                        font.pixelSize: 12
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        text: root.formatSpeed(modelData.download)
                        color: Theme.textMuted
                        font.pixelSize: 12
                        Layout.preferredWidth: 70
                        horizontalAlignment: Text.AlignRight
                    }
                    Button {
                        id: closeButton
                        text: "✕"
                        flat: true
                        implicitWidth: 32
                        implicitHeight: 32
                        onClicked: if (appModel) appModel.closeConnection(modelData.id)
                        background: Rectangle {
                            radius: 8
                            color: closeButton.hovered ? Theme.danger : "transparent"
                        }
                        contentItem: Text {
                            text: closeButton.text
                            color: closeButton.hovered ? Theme.textOnAccent : Theme.textDim
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                visible: connList.count === 0
                spacing: 8
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "🔗"; color: Theme.content3; font.pixelSize: 36 }
                Label { text: "暂无活跃连接"; color: Theme.textMuted; font.pixelSize: 14 }
            }
        }
    }
}
