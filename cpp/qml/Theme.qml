pragma Singleton
import QtQuick

// HeroUI 深色 token 映射（对齐原 Electron 端 fallback，见 docs/UI_MIGRATION_HANDOFF.md §2.4）。
// 原版主题 CSS 通过 --heroui-* HSL 变量注入；这里固化 dark 语义色，后续做在线换肤时
// 只需把解析出的 HSL 写回本单例即可。
QtObject {
    // 基础面板（HeroUI dark: background/content1/content2/content3）
    readonly property color background: "#0b0b10"
    readonly property color content1: "#18181c"
    readonly property color content2: "#232233"
    readonly property color content3: "#27272a"
    readonly property color content4: "#3f3f46"

    // 侧栏与分隔（原版 .side 深色底）
    readonly property color sider: "#151722"
    readonly property color divider: "#2e2e38"

    // 文本
    readonly property color text: "#eceef4"             // foreground
    readonly property color textMuted: "#a1a1aa"        // default-500
    readonly property color textDim: "#63636e"         // default-400 弱化
    readonly property color textOnAccent: "#ffffff"     // primary-foreground

    // 语义色（theme.ts fallback：success 145 79% 44% / warning 37 91% 55% / danger 339 90% 51%）
    readonly property color primary: "#006fee"          // HeroUI 默认蓝
    readonly property color primaryHover: "#0059c9"
    readonly property color success: "#16a34a"
    readonly property color warning: "#f5a524"
    readonly property color danger: "#f31260"

    // 交互反馈
    readonly property color hover: "#2a2a35"
    readonly property color selected: "#1f2a44"
}
