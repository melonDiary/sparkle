import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#151722"
    implicitWidth: 220
    width: 220

    property int currentIndex: 0
    signal pageSelected(int index)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 20
            spacing: 10

            Rectangle {
                width: 36
                height: 36
                radius: 10
                color: "#89b4fa"
                Text {
                    anchors.centerIn: parent
                    text: "S"
                    color: "#11111b"
                    font.bold: true
                    font.pixelSize: 22
                }
            }
            Column {
                spacing: 1
                Label {
                    text: "Sparkle"
                    color: "#f5f5f5"
                    font.bold: true
                    font.pixelSize: 18
                }
                Label {
                    text: "Proxy workspace"
                    color: "#7f849c"
                    font.pixelSize: 11
                }
            }
        }

        Label {
            text: "工作区"
            color: "#6c7086"
            font.pixelSize: 11
            leftPadding: 10
            topPadding: 6
            bottomPadding: 4
        }

        Repeater {
            model: [
                { title: "概况", icon: "⌂" },
                { title: "代理", icon: "◈" },
                { title: "规则", icon: "≡" },
                { title: "日志", icon: "▤" },
                { title: "设置", icon: "⚙" },
                { title: "内核", icon: "◇" }
            ]

            delegate: Button {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                implicitHeight: 44
                flat: true
                highlighted: root.currentIndex === index
                text: modelData.title

                contentItem: RowLayout {
                    spacing: 12
                    Text {
                        text: modelData.icon
                        color: parent.parent.highlighted ? "#89b4fa" : "#9399b2"
                        font.pixelSize: 18
                        Layout.preferredWidth: 22
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        text: modelData.title
                        color: parent.parent.highlighted ? "#eef0ff" : "#a6adc8"
                        font.pixelSize: 14
                        Layout.fillWidth: true
                    }
                }

                background: Rectangle {
                    radius: 10
                    color: parent.highlighted ? "#292c3c" : (parent.hovered ? "#202231" : "transparent")
                    // Keep the accent as a separate visual marker.
                    Rectangle {
                        visible: parent.parent.highlighted
                        width: 3
                        height: 22
                        radius: 2
                        color: "#89b4fa"
                        anchors.left: parent.left
                        anchors.leftMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                onClicked: root.pageSelected(index)
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#292c3c"
        }
        Label {
            text: "QuickJS enabled"
            color: "#6c7086"
            font.pixelSize: 11
            leftPadding: 10
            topPadding: 8
        }
    }
}
