import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 订阅页（对应原版 /profiles）：订阅列表 + 导入 + 更新 + 切换当前 + 删除。
// 数据链路：AppModel.importProfile/updateProfile → SubscriptionManager 拉取解析
// → setProfileRawText 落盘 → RuntimeConfigFactory 重新生成 → 内核重启。
Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    function formatTime(ms) {
        if (!ms || ms <= 0) return "未更新"
        const diff = Date.now() - ms
        if (diff < 60000) return "刚刚"
        if (diff < 3600000) return Math.floor(diff / 60000) + " 分钟前"
        if (diff < 86400000) return Math.floor(diff / 3600000) + " 小时前"
        return Math.floor(diff / 86400000) + " 天前"
    }

    function formatInterval(minutes) {
        const v = Number(minutes) || 0
        if (v >= 1440) return Math.round(v / 1440) + " 天"
        if (v >= 60) return Math.round(v / 60) + " 小时"
        return v + " 分钟"
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
                Label { text: "订阅管理"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label {
                    text: (appModel ? appModel.profiles.length : 0) + " 个订阅"
                    color: Theme.textMuted
                    font.pixelSize: 13
                }
            }
        }

        // 导入表单（原版支持剪贴板/手动输入 URL）
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 64
            radius: 12
            color: Theme.content1
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                TextField {
                    id: urlInput
                    Layout.fillWidth: true
                    placeholderText: "订阅链接（https://…）"
                    selectByMouse: true
                }
                TextField {
                    id: nameInput
                    Layout.preferredWidth: 140
                    placeholderText: "名称（可选）"
                    selectByMouse: true
                }
                Button {
                    id: importButton
                    text: "导入"
                    enabled: appModel && urlInput.text.trim() !== ""
                    onClicked: {
                        appModel.importProfile(urlInput.text.trim(), nameInput.text.trim())
                        urlInput.text = ""
                        nameInput.text = ""
                    }
                    background: Rectangle {
                        radius: 8
                        color: importButton.enabled
                            ? (importButton.hovered ? Theme.primaryHover : Theme.primary) : Theme.content3
                    }
                    contentItem: Label {
                        text: importButton.text
                        color: importButton.enabled ? Theme.textOnAccent : Theme.textDim
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        ListView {
            id: profileList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: appModel ? appModel.profiles : []

            delegate: Rectangle {
                required property var modelData
                required property int index
                width: profileList.width
                height: 92
                radius: 12
                readonly property bool isCurrent: appModel && appModel.currentProfileId === modelData.id
                color: isCurrent ? Theme.selected : Theme.content1
                border.width: isCurrent ? 1 : 0
                border.color: Theme.primary

                MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    spacing: 12

                    Rectangle {
                        width: 10; height: 10; radius: 5
                        color: isCurrent ? Theme.success : Theme.textDim
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            spacing: 8
                            Label {
                                text: modelData.name || "未命名订阅"
                                color: isCurrent ? Theme.primary : Theme.text
                                font.bold: true
                                font.pixelSize: 15
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // 类型 Chip（原版 ProfileCard 的 本地/远程 标记）
                            Rectangle {
                                radius: 8
                                implicitHeight: 18
                                implicitWidth: chipLabel.implicitWidth + 14
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.primary
                                Label {
                                    id: chipLabel
                                    anchors.centerIn: parent
                                    text: modelData.type === "remote" ? "远程" : "本地"
                                    color: Theme.primary
                                    font.pixelSize: 10
                                }
                            }
                            Label {
                                text: isCurrent ? "当前" : ""
                                color: Theme.success
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                        Label {
                            text: modelData.type === "remote" ? modelData.url : "本地配置文件"
                            color: Theme.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "更新于 " + root.formatTime(modelData.updated)
                                  + (modelData.type === "remote" && modelData.interval > 0
                                     ? " · 每 " + root.formatInterval(modelData.interval)
                                     : "")
                                  + (modelData.autoUpdate === false ? " · 自动更新已关" : "")
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                    }
                    // 自动更新开关（仅远程订阅且设置了 interval 才有意义）
                    Switch {
                        visible: modelData.type === "remote"
                        checked: modelData.autoUpdate !== false
                        onToggled: if (appModel) appModel.setProfileAutoUpdate(modelData.id, checked)
                    }

                    // 远程订阅：更新按钮
                    Button {
                        id: updateButton
                        visible: modelData.type === "remote"
                        text: "更新"
                        flat: true
                        onClicked: if (appModel) appModel.updateProfile(modelData.id)
                        background: Rectangle {
                            radius: 8
                            color: updateButton.hovered ? Theme.hover : "transparent"
                        }
                        contentItem: Label {
                            text: updateButton.text
                            color: Theme.textMuted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    // 切换当前
                    Button {
                        id: useButton
                        visible: !isCurrent
                        text: "使用"
                        flat: true
                        onClicked: if (appModel) appModel.setCurrentProfile(modelData.id)
                        background: Rectangle {
                            radius: 8
                            color: useButton.hovered ? Theme.primaryHover : Theme.primary
                        }
                        contentItem: Label {
                            text: useButton.text
                            color: Theme.textOnAccent
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    // 删除
                    Button {
                        id: deleteButton
                        text: "删除"
                        flat: true
                        onClicked: if (appModel) appModel.deleteProfile(modelData.id)
                        background: Rectangle {
                            radius: 8
                            color: deleteButton.hovered ? Theme.danger : "transparent"
                        }
                        contentItem: Label {
                            text: deleteButton.text
                            color: deleteButton.hovered ? Theme.textOnAccent : Theme.textDim
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                visible: profileList.count === 0
                spacing: 8
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "▦"; color: Theme.content3; font.pixelSize: 36 }
                Label { text: "暂无订阅，导入一个开始使用"; color: Theme.textMuted; font.pixelSize: 14 }
            }
        }
    }
}
