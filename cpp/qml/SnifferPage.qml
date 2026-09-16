import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 域名嗅探设置页（对应原版 /sniffer）：受控配置 sniffer 对象的表单。
// 保存 → AppModel.patchControlledConfig({sniffer:{...}}) → 深合并落盘 → 内核重启。
// 字段对齐原版 sniffer.tsx：parse-pure-ip / force-dns-mapping / override-destination /
// sniff.{HTTP,TLS,QUIC}.ports / skip-domain / force-domain。
Page {
    id: root
    property var appModel
    background: Rectangle { color: Theme.background }

    property var form: ({})
    property bool dirty: false

    function sniffPorts(protocol) {
        const s = root.form.sniff || {}
        const entry = s[protocol]
        return entry && entry.ports !== undefined ? entry.ports.join(",") : ""
    }

    function setSniffPorts(protocol, value) {
        const s = JSON.parse(JSON.stringify(root.form.sniff || {}))
        const entry = s[protocol] || {}
        entry.ports = value.trim() !== "" ? value.split(",").map(x => x.trim()) : []
        s[protocol] = entry
        root.form.sniff = s
        root.dirty = true
    }

    function syncForm() {
        if (!appModel) return
        const s = appModel.snifferConfig
        form = {
            enable: s.enable === true,
            parsePureIP: s["parse-pure-ip"] !== false,
            forceDNSMapping: s["force-dns-mapping"] !== false,
            overrideDestination: s["override-destination"] === true,
            sniff: s.sniff || { HTTP: { ports: [80, 443] }, TLS: { ports: [443] }, QUIC: { ports: [] } },
            skipDomain: s["skip-domain"] !== undefined ? s["skip-domain"].join(",") : "+.push.apple.com",
            forceDomain: s["force-domain"] !== undefined ? s["force-domain"].join(",") : ""
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
        appModel.patchControlledConfig({ sniffer: {
            enable: form.enable,
            "parse-pure-ip": form.parsePureIP,
            "force-dns-mapping": form.forceDNSMapping,
            "override-destination": form.overrideDestination,
            sniff: form.sniff,
            "skip-domain": split(form.skipDomain),
            "force-domain": split(form.forceDomain)
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "域名嗅探"; color: Theme.text; font.pixelSize: 28; font.bold: true }
                Label { text: "对 TLS/QUIC/HTTP 连接嗅探域名，用于规则匹配；保存后内核自动重启"; color: Theme.textMuted; font.pixelSize: 13 }
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
            label: "启用嗅探"
            content: Switch {
                checked: root.form.enable === true
                onToggled: { root.form.enable = checked; root.dirty = true }
            }
        }
        Row {
            label: "对未映射 IP 地址嗅探"
            content: Switch {
                checked: root.form.parsePureIP !== false
                onToggled: { root.form.parsePureIP = checked; root.dirty = true }
            }
        }
        Row {
            label: "对真实 IP 映射嗅探"
            content: Switch {
                checked: root.form.forceDNSMapping !== false
                onToggled: { root.form.forceDNSMapping = checked; root.dirty = true }
            }
        }
        Row {
            label: "覆盖连接地址"
            content: Switch {
                checked: root.form.overrideDestination === true
                onToggled: { root.form.overrideDestination = checked; root.dirty = true }
            }
        }
        Row {
            label: "HTTP 端口嗅探"
            content: TextField {
                text: root.sniffPorts("HTTP")
                implicitWidth: 180
                placeholderText: "80,443"
                selectByMouse: true
                onTextEdited: root.setSniffPorts("HTTP", text)
            }
        }
        Row {
            label: "TLS 端口嗅探"
            content: TextField {
                text: root.sniffPorts("TLS")
                implicitWidth: 180
                placeholderText: "443"
                selectByMouse: true
                onTextEdited: root.setSniffPorts("TLS", text)
            }
        }
        Row {
            label: "QUIC 端口嗅探"
            content: TextField {
                text: root.sniffPorts("QUIC")
                implicitWidth: 180
                placeholderText: "（留空禁用）"
                selectByMouse: true
                onTextEdited: root.setSniffPorts("QUIC", text)
            }
        }
        Row {
            label: "跳过域名嗅探（逗号分割）"
            content: TextField {
                text: root.form.skipDomain || ""
                implicitWidth: 260
                placeholderText: "+.push.apple.com"
                selectByMouse: true
                onTextEdited: { root.form.skipDomain = text; root.dirty = true }
            }
        }
        Row {
            label: "强制域名嗅探（逗号分割）"
            content: TextField {
                text: root.form.forceDomain || ""
                implicitWidth: 260
                placeholderText: "v2ex.com"
                selectByMouse: true
                onTextEdited: { root.form.forceDomain = text; root.dirty = true }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
