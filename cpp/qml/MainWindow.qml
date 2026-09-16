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
    color: Theme.background

    // 注意：appModel / scriptBridge 由 main.cpp 作为上下文属性注入，此处不要
    // 重新声明同名 property，否则会遮蔽上下文属性导致整体数据为 undefined。
    property int currentPage: 0

    // 侧栏宽度（原版 App.tsx：siderWidth 默认 250，拖拽边界吸附 150→折叠/250/400）。
    // 折叠宽度 70 对齐原版 macOS iconOnly 态。
    readonly property int collapsedWidth: 70
    property int siderWidth: appModel ? Math.max(appModel.siderWidth, collapsedWidth) : 250
    readonly property bool siderCollapsed: siderWidth === collapsedWidth

    // 拖拽过程中只更新本地显示，松手时才持久化（对齐原版 onResizeEnd → patchAppConfig）。
    onSiderWidthChanged: {
        if (appModel && !sidebarDrag.active && appModel.siderWidth !== siderWidth)
            appModel.siderWidth = siderWidth
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Sidebar {
                id: sidebar
                Layout.fillHeight: true
                Layout.preferredWidth: root.siderWidth
                width: root.siderWidth
                currentIndex: root.currentPage
                iconOnly: root.siderCollapsed
                onPageSelected: root.currentPage = index
            }

            // 拖拽调宽手柄（原版 App.tsx 的 resize 感应条 + 边界吸附）。
            // 把手柄位于侧栏右缘，鼠标全局 x 即目标宽度，与原版 clientX 语义一致。
            Item {
                id: sidebarDrag
                Layout.fillHeight: true
                Layout.preferredWidth: 8
                property bool active: false

                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    anchors.margins: -3
                    cursorShape: Qt.SizeHorCursor
                    onPressed: sidebarDrag.active = true
                    onReleased: {
                        sidebarDrag.active = false
                        if (appModel && appModel.siderWidth !== root.siderWidth)
                            appModel.siderWidth = root.siderWidth
                    }
                    onPositionChanged: (mouse) => {
                        // 手柄紧跟侧栏右缘：手柄全局 x ≈ 期望侧栏宽度。
                        const desiredWidth = dragArea.mapToItem(null, mouse.x, mouse.y).x
                        let next
                        if (desiredWidth <= 150) next = root.collapsedWidth
                        else if (desiredWidth <= 250) next = 250
                        else if (desiredWidth >= 400) next = 400
                        else next = Math.round(desiredWidth)
                        root.siderWidth = next
                    }
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: parent.width - 2
                    width: 3
                    height: parent.height
                    radius: 1.5
                    color: sidebarDrag.active ? Theme.primary : "transparent"
                }
            }

            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentPage
                // 页面顺序 = Sidebar 卡片的 page 索引（对齐原版 defaultSiderOrder 的路由顺序）。
                OverviewPage { appModel: appModel }
                ProxyPage { appModel: appModel; scriptBridge: scriptBridge }
                ConnectionsPage { appModel: appModel }
                ProfilesPage { appModel: appModel }
                TunPage { appModel: appModel }
                DnsPage { appModel: appModel }
                SnifferPage { appModel: appModel }
                RulesPage { appModel: appModel }
                LogsPage { appModel: appModel }
                SettingsPage { appModel: appModel }
                CorePage { appModel: appModel; scriptBridge: scriptBridge }
            }
        }

        // ---- 底部状态栏：运行状态 + 当前页 + 启停 + 消息 ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.sider

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Theme.divider
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: appModel && appModel.running ? Theme.success : Theme.textDim
                }
                Label {
                    text: appModel ? (appModel.running ? "运行中" : "已停止") : "-"
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Label {
                    text: appModel && appModel.controllerVersion !== "" ? "· v" + appModel.controllerVersion : ""
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: appModel ? appModel.statusMessage : ""
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignRight
                }
                Button {
                    id: startButton
                    text: "启动"
                    flat: true
                    enabled: appModel && !appModel.running && scriptBridge && scriptBridge.available
                    onClicked: {
                        if (!scriptBridge.startProxy()) appModel.setStatusMessage("启动脚本执行失败")
                    }
                    background: Rectangle {
                        radius: 8
                        color: startButton.enabled
                            ? (startButton.hovered ? Theme.primaryHover : Theme.primary) : Theme.content3
                    }
                    contentItem: Label {
                        text: startButton.text
                        color: startButton.enabled ? Theme.textOnAccent : Theme.textDim
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Button {
                    id: stopButton
                    text: "停止"
                    flat: true
                    enabled: appModel && appModel.running && scriptBridge && scriptBridge.available
                    onClicked: {
                        if (!scriptBridge.stopProxy()) appModel.setStatusMessage("停止脚本执行失败")
                    }
                    background: Rectangle {
                        radius: 8
                        color: stopButton.enabled
                            ? (stopButton.hovered ? Theme.content4 : Theme.content3) : Theme.content3
                    }
                    contentItem: Label {
                        text: stopButton.text
                        color: stopButton.enabled ? Theme.text : Theme.textDim
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
