# Sparkle UI 迁移对齐调研与交接

本文档记录「能否把 Qt/QML UI 做到与迁移前（原 Electron 客户端）一样」的调研结论，
以及当前 QML 端已完成的 UI 工作和后续对齐路线。目标读者是后续接手 QML UI 移植的开发者。

配套文档：
- 原 Electron 端架构与重构：`REFACTOR_HANDOFF.md`（仓库根）
- C++ 端口设计：`docs/CPP_PORT_DESIGN.md`
- C++ 审计与修复记录：`cpp/docs/CODE_AUDIT_AND_FIXES.md`

---

## 1. 结论速览

- **视觉/布局上可以做到高度相似（约 8 成）**，但「100% 与迁移前一致」是一个大工程——缺的不只是一层皮肤，而是原版有一批 Qt 侧尚未实现的后端能力在支撑。
- 差距分三层：✅ 视觉还原（QML 原生可做）、🟡 中等（需补后端或折中）、🔴 很难/基本做不了（需取舍）。
- 已完成的 UI 工作（见 §5）让当前 6 页 UI 跑通并具备：节点切换、延迟测速、系统代理/MITM/开机自启开关、内存/活动连接展示、暗色主题生效。

---

## 2. 迁移前的 UI 是什么（原 Electron 客户端）

技术栈（`package.json` / `src/renderer/`）：

- **React 19 + TypeScript + electron-vite + Vite 8**
- **HeroUI（原 NextUI）v3**（`@heroui/react`）+ **Tailwind CSS 4**
- 路由 **react-router-dom v7**；数据 **swr**
- 主题 **next-themes**（system/dark/light）+ 可下载 CSS 换肤
- 动效 **framer-motion**；虚拟列表 **react-virtuoso**；拖拽 **@dnd-kit**
- YAML 编辑 **monaco-editor**；新手引导 **driver.js**；图标 **react-icons**
- 其它：qrcode.react、dayjs、pubsub-js、express（substore 内置服务）

### 2.1 总体布局（`src/renderer/src/App.tsx`）

- 左侧 **sider**：默认宽 250px，macOS 折叠态 70px，可拖拽调宽（150–400px，边界吸附）。
- sider 顶部：`Sparkle` 标题 + 更新按钮 + 设置按钮（`IoSettings`）。
- sider 顶部下方：**出站模式切换器**（`OutboundModeSwitcher`）。
- sider 主体：**2 列网格的 13 张「实时状态卡片」**（不是文字菜单），每张是活的小组件，
  点击跳转对应路由，且支持拖拽排序（`@dnd-kit`，顺序持久化到 `appConfig.siderOrder`）。
- 右侧：滚动内容区 = 当前路由页面。
- 折叠态（iconOnly，70px）：卡片退化为仅图标 + tooltip。

### 2.2 sider 卡片清单（`src/renderer/src/components/sider/`）

`sider-cards.tsx` 里 `componentMap` + `defaultSiderOrder`：

| key | 卡片组件 | 对应路由 | 形态 |
|---|---|---|---|
| sysproxy | `SysproxySwitcher` | `/sysproxy` | 开关卡 |
| tun | `TunSwitcher` | `/tun` | 开关卡 |
| dns | `DNSCard` | `/dns` | 状态卡 |
| sniff | `SniffCard` | `/sniffer` | 开关/状态卡 |
| proxy | `ProxyCard` | `/proxies` | 图标 + 分组数量 Chip |
| connection | `ConnCard` | `/connections` | 连接数卡 |
| profile | `ProfileCard` | `/profiles` | 订阅卡 |
| mihomo | `MihomoCoreCard` | `/mihomo` | 内核运行态卡 |
| rule | `RuleCard` | `/rules` | 规则数量卡 |
| resource | `ResourceCard` | `/resources` | 外部资源卡 |
| override | `OverrideCard` | `/override` | 覆写卡 |
| log | `LogCard` | `/logs` | 日志卡 |
| substore | `SubStoreCard` | `/substore` | Sub-Store 卡 |

### 2.3 页面清单（`src/renderer/src/routes/index.tsx`）

默认路由 `/` → `/proxies`。共 14 页：

| 路由 | 页面文件 | 说明 |
|---|---|---|
| `/proxies` | `pages/proxies.tsx` | 代理组/节点 |
| `/rules` | `pages/rules.tsx` | 分流规则 |
| `/logs` | `pages/logs.tsx` | 日志 |
| `/connections` | `pages/connections.tsx` | 连接 |
| `/profiles` | `pages/profiles.tsx` | 订阅/配置 |
| `/override` | `pages/override.tsx` | 覆写（Monaco 编辑） |
| `/mihomo` | `pages/mihomo.tsx` | 内核控制 |
| `/sysproxy` | `pages/syspeoxy.tsx` | 系统代理 |
| `/tun` | `pages/tun.tsx` | TUN |
| `/dns` | `pages/dns.tsx` | DNS |
| `/sniffer` | `pages/sniffer.tsx` | 嗅探 |
| `/resources` | `pages/resources.tsx` | 外部资源 |
| `/settings` | `pages/settings.tsx` | 设置 |
| `/substore` | `pages/substore.tsx` | Sub-Store |

### 2.4 主题系统（`src/main/resolve/theme.ts` + `assets/main.css` + `hero.mjs`）

- HeroUI 默认主题，CSS 变量 `--heroui-*`（HSL 三元组）。
- `.dark` 变体驱动深色；`next-themes` 支持 system/light/dark。
- 主题 CSS 从 theme-hub（`mihomo-party-org/theme-hub`）zip 下载，`insertCSS` 注入；
  `theme.ts::normalizeThemeCss` 做 HeroUI v2→v3 的 token 兼容桥。
- 主题语义色默认值（`theme.ts` 里的 fallback）：
  - success `hsl(145 79% 44%)`
  - warning `hsl(37 91% 55%)`
  - danger  `hsl(339 90% 51%)`
  - primary/background 等取 HeroUI 默认（primary 约 `#006FEE` 蓝）。
- 仓库本地**未内置** `default.css`：`resolveThemes()` 在不存在时返回 `default.css` 占位（落回 HeroUI 内建默认）。

---

## 3. 当前 QML UI 现状与差距

> **2026-09-06 更新：Phase 1（视觉对齐）已完成**，见 §5.1。下表为 Phase 1 完成后的剩余差距。

当前 `cpp/qml/*.qml`（`MainWindow` + `Sidebar` + 6 个页面）：

| 维度 | 原版 | 现状(QML) |
|---|---|---|
| sider | 2 列卡片网格、折叠、拖拽调宽、拖拽排序 | ✅ 2 列实时状态卡 + 折叠 + 拖拽调宽 + 拖拽重排（siderOrder 持久化，ghost 预览） |
| 页面 | 14 个 | 11 个：概况/代理/连接/订阅/TUN/DNS/嗅探/规则/日志/设置/内核（resources/override/sysproxy 专页未做） |
| 主题 | HeroUI token + 在线换肤 | ✅ HeroUI 暗色 token 单例 `Theme.qml`（在线换肤未做） |
| 动效/引导/图表 | framer-motion / driver.js / traffic-chart | 无 |
| 列表 | react-virtuoso 虚拟列表 | Qt ListView（内建虚拟化） |
| 编辑器 | Monaco | 无 |

对应关系：`OverviewPage` ↔ 无直接对应（原版无概况页）；`ProxyPage` ↔ `/proxies`；
`RulesPage` ↔ `/rules`；`LogsPage` ↔ `/logs`；`SettingsPage` ↔ `/settings`（子集）；`CorePage` ↔ `/mihomo`（子集）。

---

## 4. C++ 后端能力覆盖盘点（决定可行性的关键）

UI 差距的根源 = **后端能力差距**，不只是皮肤。核对 `cpp/src/core` 后的覆盖情况：

**✅ 已迁移、可直接接线（数据/能力都在）**
- 连接流：`MihomoApiClient::connectionsUpdated(std::vector<ConnectionItem>)`（WS 节流 200ms），`ConnectionItem` 字段齐全 → **连接页**
- 订阅：`ProfileItem / ProfileConfig`（`models.h`）、`SubscriptionManager`（含 `parseSubscription`）→ **订阅页**
- 覆写：`OverrideItem / OverrideConfig` + QuickJS `runOverrideScript`（`js_engine.h`）→ **覆写数据链路**
- 代理/规则/流量/内存/日志：`fetchProxies/fetchGroups/fetchRules/setRuleDisabled/changeProxy/unfixProxy/testDelay` + `traffic/memory/logs` 流
- 系统代理：`SystemProxyManager`（三平台 + bool 返回 + 轮询守卫）
- MITM：`MITMManager`（启动/停止/脚本重载）

**🟡 部分具备，缺 UI 表单 / 需核对受控字段**
- TUN / DNS / 嗅探：`RuntimeConfigFactory` 已把 `tun/dns/sniffer` 写进受控配置（`replaceControlledMihomoConfig` 供运行时回写），但**无设置表单**，需核对各字段 setter。
- 设置：`ConfigManager::patchAppConfig` 可扩展任意配置项。

**❌ 未实现（Qt 侧暂无对应能力）**
- 自动更新、浮窗（floating window）、托盘菜单、主题中心（在线 CSS）、Sub-Store 前端、Monaco 编辑器、新手引导 tour。

---

## 5. 已完成的 UI 工作（当前进度）

相对 `cpp/docs/CODE_AUDIT_AND_FIXES.md` 之后，QML 端已完成：

1. **关键修复**：`MainWindow.qml` 内 `property var appModel/scriptBridge` 遮蔽上下文属性，导致整界面数据 `undefined` → 已移除遮蔽，页面直接绑定上下文属性。
2. **节点切换 + 延迟测速**：`AppModel::activateNode/testNodeDelay`，`MihomoApiClient::testDelay`；`ProxyPage` 支持点击切换、测速按钮、当前节点高亮。
3. **概况页**：补齐 `memory`/`connectionCount`（接 `memoryUpdated`/`connectionsUpdated`）并在 QML 展示。
4. **设置页**：`mitmEnabled`/`autostartEnabled` 开关（MITM 走 `patchAppConfig({"mitm_enabled":...})` 触发 `mitmConfigChanged` → `AppController` 启停；自启走 `platform::setAutostartEnabled`）。
5. **启动/停止/重启开箱可用**：`main.cpp` 注入默认脚本 `kDefaultProxyScript`（`onStartProxy→core.start()` 等），并提供 `cpp/scripts/example.js` 模板。
6. **暗色样式修复**：`main.cpp` 强制 `QQuickStyle::setStyle("Fusion")` + 暗色 `QPalette`（原 macOS 原生样式会忽略 QML `background/contentItem` 覆盖）。
7. **清理死代码**：移除整套未实例化的 Qt Widgets（`sidebar/main_window/pages/*/theme_manager/page_base/models/*`），`sparkle_ui` 只保留 `app_model/script_bridge`；精简 `AppController`。

---

### 5.1 Phase 1 — 视觉对齐（✅ 已完成，2026-09-06）

1. **HeroUI 暗色 token 单例**：新增 `cpp/qml/Theme.qml`（singleton，`qml/qmldir` 声明），映射
   HeroUI dark 语义色（primary `#006FEE`、success/warning/danger 取 `theme.ts` fallback），
   全部 QML 文件改用 `Theme.*` 引用，替换硬编码色。
2. **sider 重构为原版 2 列实时状态卡网格**（对齐 `sider-cards.tsx` + `defaultSiderOrder`）：
   - 卡片为活组件：系统代理卡带 Switch、连接卡显示实时上下行速度、内核卡显示运行态 + 内存、
     代理/规则卡显示计数。
   - 顶部还原 **出站模式切换器**（规则/全局/直连）：新增 `AppModel::outboundMode` /
     `requestOutboundMode()`，走 `ConfigManager::patchControlledMihomoConfig({mode})` →
     `reloadRequested` → 内核重启（对齐原版 `patchControledMihomoConfig` 链路）。
   - **折叠态**（70px，对齐原版 macOS iconOnly）+ **拖拽调宽**（MainWindow 手柄，
     阈值吸附 ≤150→折叠 / ≤250→250 / ≥400→400，对齐 `App.tsx::updateSiderWidthFromClientX`），
     宽度经 `AppModel::siderWidth` 持久化到 `appConfig.siderWidth`（松手才写盘）。
   - 底部新增状态栏（运行态/版本/状态消息/启动/停止），启停按钮从原顶部工具栏迁入。
   - 说明：用 `Flickable+GridLayout`（`Layout.columnSpan`）而非 `GridView`——后者不支持跨列卡片。
   - 卡片拖拽重排（@dnd-kit 等价）留待 Phase 3。

### 5.2 Phase 2 — 补高频页（✅ 已完成，2026-09-06）

1. **连接页 `ConnectionsPage.qml`**（对应原版 `/connections`）：
   - `AppModel` 订阅 `MihomoApiClient::connectionsUpdated`（WS 200ms 全量推送），维护
     id → item 快照 + 首见顺序，300ms 节流 diff 后 emit `connectionsChanged`（原有
     `connectionCount` 逻辑不变）。
   - 列表展示 主机/目标、进程、规则、链路、上下行流量；搜索过滤；单条关闭
     （`closeConnection` → `DELETE /connections/{id}`）+ 关闭全部
     （`closeAllConnections` → `DELETE /connections`，均为 MihomoApiClient 新增 REST）。
2. **订阅页 `ProfilesPage.qml`**（对应原版 `/profiles`）：
   - `AppModel::profiles/currentProfileId` 读 `ConfigManager::profileConfig()`
     （profile.yaml），`profileConfigChanged` 信号驱动刷新。
   - 导入（`importProfile`：生成条目 → `SubscriptionManager::fetch` 拉取解析 →
     `setProfileRawText` 落盘正文，失败经 `fetchFailed` 提示）、更新（`updateProfile`）、
     切换当前（`setCurrentProfile` → 写 `current` → 重启内核）、删除（`deleteProfile`：
     删条目 + 正文文件 + 更新定时器，删当前订阅自动切到剩余首个）。
   - 当前订阅正文变更后经 `regenerateRuntimeConfig()`（= `CoreManager::restart`，
     与 `reloadRequested` 消费链路一致）重新生成运行配置。
   - 导入/更新有 `importingProfile_` 并发保护；时间显示相对化。
3. **sider 增加连接卡、订阅卡**（各自跳新页，显示实时计数），页面索引映射同步更新
   （MainWindow StackLayout 顺序 = 卡片 page 字段）。

### 5.3 Phase 3 — 表单/重排（✅ 已完成，2026-09-06）

1. **TUN/DNS/嗅探设置表单**（`TunPage.qml` / `DnsPage.qml` / `SnifferPage.qml`）：
   - `AppModel` 新增 `tunConfig/dnsConfig/snifferConfig` 属性（受控配置对应对象整体转
     QVariantMap，`controlledMihomoConfigChanged` 驱动刷新）与通用
     `patchControlledConfig(QVariantMap)`（→ `patchControlledMihomoConfig` 深合并 →
     `reloadRequested` → 内核重启，与原版 `patchControledMihomoConfig + restartCore` 等价）。
   - 字段对齐原版 `tun.tsx`（stack/device/strict-route 按平台显示，macOS 隐藏 device、
     Linux 独有 auto-redirect、dns-hijack/MTU/排除网段略）、`dns.tsx`（ipv6/enhanced-mode/
     fake-ip-range/fake-ip-filter/nameserver 等基础子集，policy/hosts 编辑器未做）、
     `sniffer.tsx`（parse-pure-ip/force-dns-mapping/override-destination/端口嗅探/
     skip-domain/force-domain）。
   - 表单为工作副本 + dirty 保存按钮（对齐原版 changed → 保存 交互）；
     clean* 归一化（空值/默认值剔除）由 RuntimeConfigFactory 已有逻辑兜底。
2. **侧栏卡片拖拽重排**：卡片 delegate MouseArea 内置 6px 拖拽阈值（区分点击），拖起
   显示 ghost（半透明复刻卡片），落点卡片高亮、松手提交；顺序写入 `appConfig.siderOrder`
   （新增 `AppModel::siderOrder` 属性 + `setSiderOrder` 持久化），启动时按保存顺序恢复，
   新增卡片 key 追加默认序（对齐原版 defaultSiderOrder + dnd-kit 行为）。
3. **sider 新增 TUN/DNS/嗅探卡片**（span=1，跳转对应表单页），页面索引重排：
   0概况 1代理 2连接 3订阅 4TUN 5DNS 6嗅探 7规则 8日志 9设置 10内核。

### 5.4 订阅自动更新（✅ 已完成，2026-09-06，对齐原版 profileUpdater.ts）

- `AppModel` 内置定时器池（id → 单发 QTimer，等价原版 `intervalPool`）：
  `rescheduleProfileUpdaters()` 按 `interval`（分钟）为满足
  `type==remote && interval>0 && autoUpdate!=false` 的订阅排期；启动时已到期 → 立即
  后台更新一次并按完整 interval 重排；未到期 → 剩余延迟触发；当前订阅额外 +10s
  错峰（对齐原版）。定时器触发时重新校验订阅仍存在且允许自动更新，然后周期化重排。
- 触发链：`profileConfigChanged`（导入/更新/切换/开关自动更新都会走）→ 重排全部；
  `deleteProfile` → `clearProfileUpdater`；`profileWriteFailed` → 10s 后重试；
  与手动更新共用 `importingProfile_` 互斥，冲突时 10s 后重试。
- 后台更新成功：正文落盘 + 刷 `updated` 时间戳 + 状态栏提示；是当前订阅则重启内核。
- `ProfilesPage`：远程订阅显示「每 X」间隔与自动更新开关（`setProfileAutoUpdate`），
  导入的新订阅默认 `autoUpdate: true`。

## 6. 可行性分层（逐项映射）

### ✅ 视觉还原（QML 原生支持，中低工作量，推荐先做）

| 目标 | QML 方案 |
|---|---|
| sider 卡片网格 | `GridView` + 卡片 delegate（`Repeater`/`GridView`） |
| 折叠/拖拽调宽 | `SplitView` 或自定义拖柄（App.tsx 已给阈值逻辑） |
| 精确配色 | 建 QML 颜色 singleton，映射 HeroUI 深色 token |
| 虚拟列表 | Qt `ListView` 天然虚拟化 |
| 实时状态卡 | 后端信号大多已在（§4 ✅），直接 `Q_PROPERTY` 绑定 |

### 🟡 中等（能做，但需补后端或折中）

| 目标 | 说明 |
|---|---|
| sider 卡片拖拽重排 | `GridView` `move`/`displaced`，近似 @dnd-kit |
| 连接页 / 订阅页 | 后端已就绪，仅缺 QML 页面 |
| 完整内核页 / 设置补全 | 复用 `CoreManager`/`ConfigManager` |
| dns/sniffer/tun 表单 | 需核对受控配置 setter 字段 |
| 换肤 | 读 theme-hub `.css` 的 `--heroui-*` HSL → 映射 QML；只能做到「配色主题」，不是任意 CSS |
| 微动效 | `NumberAnimation`/`Behavior`（≈ framer-motion，不 1:1） |
| 新手引导 | 自绘 QML overlay |

### 🔴 很难/基本做不了（需重新设计或取舍）

| 目标 | 原因 / 替代 |
|---|---|
| Monaco YAML 编辑器 | Qt 无等价物；退化为 `TextArea` + `QSyntaxHighlighter` 基础高亮 |
| Sub-Store | 原版是内置 web 服务 + 前端；需内嵌 `QWebEngine` 且重做单例/端口 |
| 主题中心在线下载 | 可做「下载 + 解析颜色」，但渲染不走 CSS |
| 浮窗/托盘菜单/自动更新 | 逐个平台新做（Qt 侧目前仅 tray `showMessage`） |

---

## 7. 建议的分阶段路线

1. **Phase 1 — 先「像」**：✅ **已完成（2026-09-06）**，详见 §5.1。剩余尾巴：6 页内容密度仍可继续向原版对齐。
2. **Phase 2 — 补高频页**：✅ **已完成（2026-09-06）**，详见 §5.2（连接页、订阅页；内核页为原版子集、设置页含出站模式，暂维持现状）。
3. **Phase 3 — 补齐/润色**：✅ **部分完成（2026-09-06）**：dns/sniffer/tun 表单 + 卡片拖拽重排已落地（见 §5.3）；剩余：覆写（简易编辑器）/ resources 页 + 动效 + 引导。
4. **取舍项**（另行决策）：Monaco、Sub-Store、主题中心、浮窗/托盘菜单/自更新。

---

## 8. 关键参考文件

原 Electron 端：
- 布局/侧栏：`src/renderer/src/App.tsx`
- 路由：`src/renderer/src/routes/index.tsx`、`route-pages.tsx`
- 卡片网格：`src/renderer/src/components/sider/sider-cards.tsx`（及其余 `sider/*.tsx`）
- 页面：`src/renderer/src/pages/*.tsx`
- 主题：`src/main/resolve/theme.ts`、`src/renderer/src/assets/main.css`、`hero.mjs`
- 依赖与构建：`package.json`、`electron.vite.config.ts`

Qt/QML 端：
- QML：`cpp/qml/*.qml`、`cpp/qml/qml.qrc`
- UI 模型/桥：`cpp/src/ui/app_model.{h,cpp}`、`script_bridge.{h,cpp}`
- 组合根：`cpp/src/app/main.cpp`、`app_controller.{h,cpp}`
- 数据层：`cpp/src/core/mihomo_api_client.{h,cpp}`、`config_manager.{h,cpp}`、`subscription_manager.{h,cpp}`、`models.h`