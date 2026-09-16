import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// DNS 设置页（对应原版 /dns）：受控配置 dns 对象的表单（基础字段子集）。
// 保存 → AppModel.patchControlledConfig({dns:{...}}) → 深合并落盘 → 内核重启。
// 字段对齐原版 dns.tsx：ipv6/enhanced-mode/fake-ip-range/fake-ip-filter/
// default-nameserver/nameserver/respect-rules/use-hosts/use-system-hosts。
Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    property var form: ({})
    property bool dirty: false

    function syncForm() {
        if (!appModel) return
        const d = appModel.dnsConfig
        form = {
            enable: d.enable === true,
            ipv6: d.ipv6 === true,
            enhancedMode: d["enhanced-mode"] || "fake-ip",
            fakeIPRange: d["fake-ip-range"] || "198.18.0.1/16",
            fakeIPFilter: d["fake-ip-filter"] !== undefined ? d["fake-ip-filter"].join(",") : "+.lan,+.local",
            defaultNameserver: d["default-nameserver"] !== undefined ? d["default-nameserver"].join(",") : "tls://223.5.5.5",
            nameserver: d.nameserver !== undefined ? d.nameserver.join(",") : "https://doh.pub/dns-query",
            respectRules: d["respect-rules"] === true,
            useHosts: d["use-hosts"] === true,
            useSystemHosts: d["use-system-hosts"] === true
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
        const split = s => s.trim() !== "" ? s.split(",").map(x => x.trim()) : []
        appModel.patchControlledConfig({ dns: {
            enable: form.enable,
            ipv6: form.ipv6,
            "enhanced-mode": form.enhancedMode,
            "fake-ip-range": form.fakeIPRange.trim(),
            "fake-ip-filter": split(form.fakeIPFilter),
            "default-nameserver": split(form.defaultNameserver),
            nameserver: split(form.nameserver),
            "respect-rules": form.respectRules,
            "use-hosts": form.useHosts,
            "use-system-hosts": form.useSystemHosts
        }})
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

    component ModeButton: Button {
        id: modeRoot
        required property var modelData
        property string modeKey: ""
        property bool active: root.form.enhancedMode === modeKey
        implicitHeight: 30
        flat: true
        text: modelData
        background: Rectangle {
            radius: 8
            color: modeRoot.active ? Theme.primary : Theme.content2
        }
        contentItem: Text {
            text: modeRoot.text
            color: modeRoot.active ? Theme.textOnAccent : Theme.textMuted
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        onClicked: { root.form.enhancedMode = modeKey; root.dirty = true }
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
                Label { text: "DNS 设置"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label { text: "域名解析与映射模式；保存后内核自动重启"; color: Theme.textMuted; font.pixelSize: 13 }
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
            label: "启用 DNS 模块"
            content: Switch {
                checked: root.form.enable === true
                onToggled: { root.form.enable = checked; root.dirty = true }
            }
        }
        Row {
            label: "IPv6 解析"
            content: Switch {
                checked: root.form.ipv6 === true
                onToggled: { root.form.ipv6 = checked; root.dirty = true }
            }
        }
        Row {
            label: "域名映射模式"
            content: RowLayout {
                spacing: 4
                Repeater {
                    model: [
                        { key: "fake-ip", label: "虚假 IP" },
                        { key: "redir-host", label: "真实 IP" },
                        { key: "normal", label: "取消映射" }
                    ]
                    delegate: ModeButton {
                        modeKey: modelData.key
                        text: modelData.label
                    }
                }
            }
        }
        Row {
            visible: root.form.enhancedMode === "fake-ip"
            label: "虚假 IP 范围 (IPv4)"
            content: TextField {
                text: root.form.fakeIPRange || ""
                implicitWidth: 180
                placeholderText: "198.18.0.1/16"
                selectByMouse: true
                onTextEdited: { root.form.fakeIPRange = text; root.dirty = true }
            }
        }
        Row {
            visible: root.form.enhancedMode === "fake-ip"
            label: "虚假 IP 过滤器（逗号分割）"
            content: TextField {
                text: root.form.fakeIPFilter || ""
                implicitWidth: 260
                placeholderText: "+.lan,+.local"
                selectByMouse: true
                onTextEdited: { root.form.fakeIPFilter = text; root.dirty = true }
            }
        }
        Row {
            label: "基础服务器（逗号分割）"
            content: TextField {
                text: root.form.defaultNameserver || ""
                implicitWidth: 260
                placeholderText: "tls://223.5.5.5"
                selectByMouse: true
                onTextEdited: { root.form.defaultNameserver = text; root.dirty = true }
            }
        }
        Row {
            label: "默认解析服务器（逗号分割）"
            content: TextField {
                text: root.form.nameserver || ""
                implicitWidth: 300
                placeholderText: "https://doh.pub/dns-query"
                selectByMouse: true
                onTextEdited: { root.form.nameserver = text; root.dirty = true }
            }
        }
        Row {
            label: "遵循分流规则解析"
            content: Switch {
                checked: root.form.respectRules === true
                onToggled: { root.form.respectRules = checked; root.dirty = true }
            }
        }
        Row {
            label: "使用配置 hosts"
            content: Switch {
                checked: root.form.useHosts === true
                onToggled: { root.form.useHosts = checked; root.dirty = true }
            }
        }
        Row {
            label: "使用系统 hosts"
            content: Switch {
                checked: root.form.useSystemHosts === true
                onToggled: { root.form.useSystemHosts = checked; root.dirty = true }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
