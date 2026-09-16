import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// TUN 设置页（对应原版 /tun）：受控配置 tun 对象的表单。
// 保存 → AppModel.patchControlledConfig({tun:{...}}) → 深合并落盘 → 内核重启。
// 字段对齐原版 tun.tsx（device/stack 仅非 macOS 显示 device，对齐 cleanTunConfig）。
Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    // 表单工作副本：打开页面或配置外部变更时同步；dirty 后由保存按钮提交。
    property var form: ({})
    property bool dirty: false
    readonly property bool isMac: Qt.platform.os === "macos"

    function syncForm() {
        if (!appModel) return
        const t = appModel.tunConfig
        form = {
            enable: t.enable === true,
            stack: t.stack || "mixed",
            device: t.device || "",
            autoRoute: t["auto-route"] !== false,
            autoRedirect: t["auto-redirect"] === true,
            autoDetectInterface: t["auto-detect-interface"] !== false,
            strictRoute: t["strict-route"] === true,
            disableIcmpForwarding: t["disable-icmp-forwarding"] === true,
            dnsHijack: t["dns-hijack"] !== undefined ? t["dns-hijack"].join(",") : "any:53",
            mtu: t.mtu !== undefined ? t.mtu : 1500
        }
        dirty = false
    }

    Component.onCompleted: syncForm()
    Connections {
        target: appModel
        function onControlledConfigChanged() { if (!root.dirty) root.syncForm() }
    }

    function save() {
        if (!appModel) return
        const patch = { tun: {
            enable: form.enable,
            stack: form.stack,
            "auto-route": form.autoRoute,
            "auto-redirect": form.autoRedirect,
            "auto-detect-interface": form.autoDetectInterface,
            "strict-route": form.strictRoute,
            "disable-icmp-forwarding": form.disableIcmpForwarding,
            "dns-hijack": form.dnsHijack.trim() !== "" ? form.dnsHijack.split(",").map(s => s.trim()) : [],
            mtu: Math.min(Math.max(parseInt(form.mtu) || 1500, 1), 65535)
        }}
        if (!root.isMac && form.device.trim() !== "") patch.tun.device = form.device.trim()
        appModel.patchControlledConfig(patch)
        dirty = false
    }

    component Row: Rectangle {
        id: rowRoot
        property string label
        property alias content: contentSlot.data
        Layout.fillWidth: true
        implicitHeight: 56
        radius: 10
        color: Theme.content1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12
            Label { text: rowRoot.label; color: Theme.text; font.pixelSize: 14; Layout.fillWidth: true }
            Item { id: contentSlot; Layout.alignment: Qt.AlignRight; implicitWidth: childrenRect.width; implicitHeight: childrenRect.height }
        }
    }

    component InlineSwitch: Switch {
        onToggled: root.dirty = true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "虚拟网卡 (TUN)"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label { text: "接管系统全部流量；保存后内核自动重启"; color: Theme.textMuted; font.pixelSize: 13 }
            }
            Button {
                id: saveButton
                text: "保存"
                visible: root.dirty
                onClicked: root.save()
                background: Rectangle {
                    radius: 8
                    color: root.dirty ? (saveButton.hovered ? Theme.primaryHover : Theme.primary) : Theme.content3
                }
                contentItem: Label {
                    text: saveButton.text
                    color: Theme.textOnAccent
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Row {
            label: "启用 TUN 模式"
            content: InlineSwitch {
                checked: root.form.enable === true
                onToggled: { root.form.enable = checked; root.dirty = true }
            }
        }
        Row {
            label: "TUN 模式堆栈"
            content: RowLayout {
                spacing: 4
                Repeater {
                    model: ["gvisor", "mixed", "system"]
                    delegate: Button {
                        id: stackButton
                        required property var modelData
                        text: modelData
                        implicitHeight: 30
                        flat: true
                        readonly property bool active: root.form.stack === stackButton.modelData
                        background: Rectangle {
                            radius: 8
                            color: stackButton.active ? Theme.primary : Theme.content2
                        }
                        contentItem: Text {
                            text: stackButton.text
                            color: stackButton.active ? Theme.textOnAccent : Theme.textMuted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: { root.form.stack = stackButton.modelData; root.dirty = true }
                    }
                }
            }
        }
        Row {
            visible: !root.isMac
            label: "TUN 网卡名称"
            content: TextField {
                text: root.form.device || ""
                implicitWidth: 130
                selectByMouse: true
                onTextEdited: { root.form.device = text; root.dirty = true }
            }
        }
        Row {
            visible: !root.isMac
            label: "严格路由"
            content: InlineSwitch {
                checked: root.form.strictRoute === true
                onToggled: { root.form.strictRoute = checked; root.dirty = true }
            }
        }
        Row {
            label: "自动设置路由规则"
            content: InlineSwitch {
                checked: root.form.autoRoute !== false
                onToggled: { root.form.autoRoute = checked; root.dirty = true }
            }
        }
        Row {
            visible: Qt.platform.os === "linux"
            label: "自动设置 TCP 重定向"
            content: InlineSwitch {
                checked: root.form.autoRedirect === true
                onToggled: { root.form.autoRedirect = checked; root.dirty = true }
            }
        }
        Row {
            label: "自动选择流量出口"
            content: InlineSwitch {
                checked: root.form.autoDetectInterface !== false
                onToggled: { root.form.autoDetectInterface = checked; root.dirty = true }
            }
        }
        Row {
            label: "ICMP 转发"
            content: InlineSwitch {
                checked: root.form.disableIcmpForwarding !== true
                onToggled: { root.form.disableIcmpForwarding = !checked; root.dirty = true }
            }
        }
        Row {
            label: "MTU"
            content: TextField {
                text: String(root.form.mtu || 1500)
                implicitWidth: 100
                selectByMouse: true
                onTextEdited: { root.form.mtu = parseInt(text) || 1500; root.dirty = true }
            }
        }
        Row {
            label: "DNS 劫持（逗号分割）"
            content: TextField {
                text: root.form.dnsHijack || ""
                implicitWidth: 200
                selectByMouse: true
                onTextEdited: { root.form.dnsHijack = text; root.dirty = true }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
