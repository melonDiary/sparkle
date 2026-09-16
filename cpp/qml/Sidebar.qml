import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 原版 sider（App.tsx）：标题 + 出站模式切换器 + 2 列实时状态卡网格。
// 卡片 = 活的组件（开关/实时数值/计数），点击跳转对应页面；
// 支持拖拽重排（拖起显示 ghost，落点附近卡片高亮交换），顺序持久化到
// appConfig.siderOrder（对齐原版 siderOrder + @dnd-kit 近似实现）。
Rectangle {
    id: root
    color: Theme.sider
    implicitWidth: 250
    // 宽度由 MainWindow 的拖拽手柄控制（含折叠态 70px），此处不做本地赋值。

    property int currentIndex: 0
    property bool iconOnly: false
    signal pageSelected(int index)

    // 全部卡片定义（key = 持久化顺序标识；span=2 横跨两列；page = StackLayout 索引）。
    readonly property var allCards: [
        { key: "sysproxy", title: "系统代理", icon: "🌐", span: 1, page: -1 },
        { key: "proxy", title: "代理组", icon: "◈", span: 2, page: 1 },
        { key: "connection", title: "连接", icon: "🔗", span: 2, page: 2 },
        { key: "profile", title: "订阅管理", icon: "▦", span: 2, page: 3 },
        { key: "tun", title: "TUN", icon: " ⇌", span: 1, page: 4 },
        { key: "dns", title: "DNS", icon: "Ⓝ", span: 1, page: 5 },
        { key: "sniff", title: "嗅探", icon: "👁", span: 1, page: 6 },
        { key: "mihomo", title: "内核", icon: "◇", span: 2, page: 10 },
        { key: "rule", title: "规则", icon: "≡", span: 1, page: 7 },
        { key: "log", title: "日志", icon: "▤", span: 1, page: 8 },
        { key: "overview", title: "概况", icon: "⌂", span: 2, page: 0 },
        { key: "settings", title: "设置", icon: "⚙", span: 2, page: 9 }
    ]

    // 显示顺序：appModel.siderOrder 里存在的 key 按其顺序在前，缺省 key 追加默认序。
    readonly property var displayCards: {
        const saved = appModel ? appModel.siderOrder : []
        const known = []
        for (let i = 0; i < allCards.length; i++) known.push(allCards[i].key)
        const keys = []
        for (let i = 0; i < saved.length; i++) {
            if (known.indexOf(saved[i]) >= 0 && keys.indexOf(saved[i]) < 0) keys.push(saved[i])
        }
        for (let i = 0; i < known.length; i++) {
            if (keys.indexOf(known[i]) < 0) keys.push(known[i])
        }
        return allCards.slice().sort((a, b) => keys.indexOf(a.key) - keys.indexOf(b.key))
    }

    // ---- 拖拽重排状态 ----
    property string draggingKey: ""
    property int dragFromIndex: -1
    property int dragHoverIndex: -1

    function formatSpeed(bytesPerSecond) {
        const v = Number(bytesPerSecond) || 0
        if (v >= 1024 * 1024) return (v / 1024 / 1024).toFixed(2) + " MB/s"
        if (v >= 1024) return (v / 1024).toFixed(1) + " KB/s"
        return v.toFixed(0) + " B/s"
    }

    function formatBytes(bytes) {
        const v = Number(bytes) || 0
        if (v >= 1024 * 1024 * 1024) return (v / 1024 / 1024 / 1024).toFixed(1) + " GB"
        if (v >= 1024 * 1024) return (v / 1024 / 1024).toFixed(0) + " MB"
        if (v >= 1024) return (v / 1024).toFixed(0) + " KB"
        return v.toFixed(0) + " B"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ---- 标题行（原版：Sparkle 标题 + 设置按钮）----
        RowLayout {
            Layout.fillWidth: true
            visible: !root.iconOnly
            spacing: 8

            Label {
                text: "Sparkle"
                color: Theme.text
                font.bold: true
                font.pixelSize: 17
                Layout.fillWidth: true
            }
            Button {
                id: settingsButton
                implicitWidth: 30
                implicitHeight: 30
                flat: true
                onClicked: root.pageSelected(9)
                background: Rectangle {
                    radius: 8
                    color: settingsButton.hovered ? Theme.hover : "transparent"
                }
                contentItem: Text {
                    text: "⚙"
                    color: root.currentIndex === 9 ? Theme.primary : Theme.textMuted
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // ---- 出站模式切换器（原版 OutboundModeSwitcher：规则/全局/直连）----
        RowLayout {
            Layout.fillWidth: true
            visible: !root.iconOnly
            spacing: 4
            Repeater {
                model: [
                    { key: "rule", label: "规则" },
                    { key: "global", label: "全局" },
                    { key: "direct", label: "直连" }
                ]
                delegate: Button {
                    id: modeButton
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: 32
                    flat: true
                    readonly property bool active: appModel ? appModel.outboundMode === modelData.key : false
                    background: Rectangle {
                        color: modeButton.active ? Theme.primary : Theme.content2
                        radius: 8
                    }
                    contentItem: Text {
                        text: modeButton.modelData.label
                        color: modeButton.active ? Theme.textOnAccent : Theme.textMuted
                        font.bold: modeButton.active
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: if (appModel) appModel.requestOutboundMode(modeButton.modelData.key)
                }
            }
        }

        // ---- 2 列实时状态卡网格（GridLayout 支持 col-span，GridView 不支持）----
        Flickable {
            id: cardScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: cardGrid.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            // 拖拽期间锁定滚动，避免 ghost 与滚动竞争。
            interactive: !root.draggingKey && contentHeight > height
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            GridLayout {
                id: cardGrid
                width: cardScroll.width
                columns: root.iconOnly ? 1 : 2
                columnSpacing: 8
                rowSpacing: 8

                Repeater {
                    id: cardRepeater
                    model: root.displayCards

                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        required property int index

                        readonly property bool isActive: root.currentIndex === modelData.page && modelData.page >= 0
                        readonly property bool isDragHover: root.draggingKey !== "" && root.draggingKey !== modelData.key
                                                            && root.dragHoverIndex === index

                        Layout.fillWidth: true
                        Layout.columnSpan: root.iconOnly ? 1 : modelData.span
                        Layout.preferredHeight: root.iconOnly ? 46 : (modelData.span === 2 ? 128 : 60)

                        radius: 12
                        color: isActive ? Theme.selected
                              : isDragHover ? Theme.hover
                              : Theme.content1
                        border.width: isActive || isDragHover ? 1 : 0
                        border.color: isDragHover ? Theme.primary : Theme.primary
                        opacity: root.draggingKey === modelData.key ? 0.25 : 1
                        Behavior on opacity { NumberAnimation { duration: 120 } }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: root.draggingKey === "" ? Qt.PointingHandCursor : Qt.ClosedHandCursor
                            // 拖拽阈值：超过 6px 视为拖动，否则保持点击。
                            property bool dragArmed: false
                            property point pressPos

                            onPressed: (mouse) => {
                                pressPos = Qt.point(mouse.x, mouse.y)
                                dragArmed = false
                            }
                            onPositionChanged: (mouse) => {
                                if (root.iconOnly) return
                                if (!dragArmed && Math.hypot(mouse.x - pressPos.x, mouse.y - pressPos.y) > 6) {
                                    dragArmed = true
                                    root.draggingKey = card.modelData.key
                                    root.dragFromIndex = card.index
                                    root.dragHoverIndex = card.index
                                    dragGhost.icon = card.modelData.icon
                                    dragGhost.title = card.modelData.title
                                    const global = card.mapToItem(root, 0, 0)
                                    dragGhost.x = global.x
                                    dragGhost.y = global.y
                                    dragGhost.width = card.width
                                    dragGhost.height = card.height
                                    dragGhost.visible = true
                                }
                                if (dragArmed && root.draggingKey === card.modelData.key) {
                                    const global = card.mapToItem(root, 0, 0)
                                    dragGhost.x = global.x + (mouse.x - pressPos.x)
                                    dragGhost.y = global.y + (mouse.y - pressPos.y)
                                    root.dragHoverIndex = root.targetIndexFor(dragGhost)
                                }
                            }
                            onReleased: {
                                if (dragArmed && root.draggingKey === card.modelData.key) {
                                    const target = root.targetIndexFor(dragGhost)
                                    root.commitReorder(target)
                                }
                                dragArmed = false
                            }
                            onClicked: (mouse) => {
                                if (dragArmed) return  // 拖拽结束不触发点击
                                if (card.modelData.key === "sysproxy") {
                                    if (appModel)
                                        appModel.setSystemProxyEnabled(!appModel.systemProxyEnabled)
                                } else if (card.modelData.page >= 0) {
                                    root.pageSelected(card.modelData.page)
                                }
                            }
                        }

                        // 折叠态：仅图标（原版 iconOnly + tooltip）
                        Text {
                            anchors.centerIn: parent
                            visible: root.iconOnly
                            text: card.modelData.icon
                            color: card.isActive ? Theme.primary : Theme.textMuted
                            font.pixelSize: 20
                        }

                        // 展开态：图标 + 实时数值 + 标题
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            visible: !root.iconOnly
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    text: card.modelData.icon
                                    color: card.isActive ? Theme.primary : Theme.textMuted
                                    font.pixelSize: 16
                                }
                                Item { Layout.fillWidth: true }
                                // 实时数值区（各卡片形态对齐原版）
                                Label {
                                    visible: card.modelData.key === "proxy" || card.modelData.key === "rule"
                                             || card.modelData.key === "connection"
                                             || card.modelData.key === "profile"
                                    text: {
                                        switch (card.modelData.key) {
                                        case "proxy":
                                            return appModel ? String(Math.max(0, appModel.groupNames.length - 1)) : "-"
                                        case "rule":
                                            return appModel ? String(appModel.rules.length) : "-"
                                        case "profile":
                                            return appModel ? String(appModel.profiles.length) : "-"
                                        default:
                                            return appModel ? String(appModel.connectionCount) : "-"
                                        }
                                    }
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Switch {
                                    visible: card.modelData.key === "sysproxy"
                                    checked: appModel ? appModel.systemProxyEnabled : false
                                    onToggled: if (appModel) appModel.setSystemProxyEnabled(checked)
                                }
                            }

                            // 连接卡：实时上下行速度（原版 ConnCard）
                            ColumnLayout {
                                visible: card.modelData.key === "connection"
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: "↑ " + root.formatSpeed(appModel ? appModel.traffic.upload : 0)
                                    color: Theme.text
                                    font.pixelSize: 13
                                    Layout.alignment: Qt.AlignRight
                                }
                                Label {
                                    text: "↓ " + root.formatSpeed(appModel ? appModel.traffic.download : 0)
                                    color: Theme.text
                                    font.pixelSize: 13
                                    Layout.alignment: Qt.AlignRight
                                }
                            }

                            // 内核卡：运行状态 + 内存（原版 MihomoCoreCard）
                            RowLayout {
                                visible: card.modelData.key === "mihomo"
                                Layout.fillWidth: true
                                spacing: 6
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    color: appModel && appModel.running ? Theme.success : Theme.textDim
                                }
                                Label {
                                    text: appModel ? (appModel.running ? "运行中" : "已停止") : "-"
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: root.formatBytes(appModel ? appModel.memory.inUse : 0)
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }
                            }

                            Item { Layout.fillHeight: true }

                            Label {
                                text: card.modelData.title
                                color: card.isActive ? Theme.text : Theme.textMuted
                                font.bold: true
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }

        // ---- 底部：折叠开关（仅切换视图形态，宽度仍由 MainWindow 手柄控制）----
        Button {
            id: collapseButton
            Layout.fillWidth: !root.iconOnly
            Layout.preferredWidth: root.iconOnly ? 40 : -1
            Layout.alignment: root.iconOnly ? Qt.AlignHCenter : Qt.AlignLeft
            implicitHeight: 30
            flat: true
            onClicked: root.iconOnly = !root.iconOnly
            background: Rectangle {
                radius: 8
                color: collapseButton.hovered ? Theme.hover : "transparent"
            }
            contentItem: Text {
                text: root.iconOnly ? "»" : "« 收起"
                color: Theme.textMuted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // 找到 ghost 中心附近的卡片索引（距离小于卡片高度的一半才算命中）。
    function targetIndexFor(ghost) {
        const center = ghost.mapToItem(cardGrid, ghost.width / 2, ghost.height / 2)
        let bestIndex = -1
        let bestDist = Infinity
        for (let i = 0; i < cardRepeater.count; i++) {
            const item = cardRepeater.itemAt(i)
            if (!item) continue
            const itemKey = item.modelData ? item.modelData.key : ""
            if (itemKey === root.draggingKey) continue
            const c = item.mapToItem(cardGrid, item.width / 2, item.height / 2)
            const dist = Math.hypot(c.x - center.x, c.y - center.y)
            if (dist < bestDist) {
                bestDist = dist
                bestIndex = i
            }
        }
        const nearest = bestIndex >= 0 ? cardRepeater.itemAt(bestIndex) : null
        if (nearest && bestDist < Math.max(nearest.height, 60) * 0.75) return bestIndex
        return root.dragFromIndex
    }

    // 提交重排：把拖拽 key 移动到目标索引，持久化 siderOrder。
    function commitReorder(targetIndex) {
        const from = root.dragFromIndex
        dragGhost.visible = false
        root.draggingKey = ""
        root.dragHoverIndex = -1
        if (from < 0 || targetIndex < 0 || targetIndex === from) return

        const cards = root.displayCards.slice()
        const moved = cards.splice(from, 1)[0]
        cards.splice(targetIndex, 0, moved)
        if (appModel) {
            const keys = cards.map(c => c.key)
            appModel.setSiderOrder(keys)
        }
    }

    // 拖拽 ghost：跟随鼠标的半透明卡片。
    Rectangle {
        id: dragGhost
        visible: false
        z: 99
        radius: 12
        color: Theme.content1
        border.width: 1
        border.color: Theme.primary
        opacity: 0.9
        property string icon: ""
        property string title: ""
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4
            Text {
                text: dragGhost.icon
                color: Theme.primary
                font.pixelSize: 16
            }
            Item { Layout.fillHeight: true }
            Label {
                text: dragGhost.title
                color: Theme.text
                font.bold: true
                font.pixelSize: 13
            }
        }
    }
}
