# Sparkle C++/Qt6 移植设计文档

> 版本：v1.0（供他人实施的交接文档）
> 源工程：`https://github.com/xishang0128/sparkle`（Electron + React + TypeScript，v1.26.7）
> 目标：用 **C++20 + Qt 6 + CMake + vcpkg** 重写为原生桌面应用，功能与现有一致。

---

## 目录

1. [文档目的与阅读对象](#1-文档目的与阅读对象)
2. [现状梳理：Electron 实现映射](#2-现状梳理electron-实现映射)
3. [技术选型与约束](#3-技术选型与约束)
4. [总体架构与线程模型](#4-总体架构与线程模型)
5. [命名空间与目录结构](#5-命名空间与目录结构)
6. [数据模型](#6-数据模型)
7. [路径管理 Paths](#7-路径管理-paths)
8. [日志管理 LogManager](#8-日志管理-logmanager)
9. [配置管理 ConfigManager 与 YamlStore](#9-配置管理-configmanager-与-yamlstore)
10. [运行时配置生成 RuntimeConfigFactory](#10-运行时配置生成-runtimeconfigfactory)
11. [内核进程控制 CoreProcessController](#11-内核进程控制-coreprocesscontroller)
12. [Mihomo 控制器传输层（HTTP/WebSocket）](#12-mihomo-控制器传输层httpwebsocket)
13. [内核管理 CoreManager](#13-内核管理-coremanager)
14. [系统代理 SystemProxyManager 与平台实现](#14-系统代理-systemproxyManager-与平台实现)
15. [应用编排 AppController 与入口 main.cpp](#15-应用编排-appcontroller-与入口-maincpp)
16. [用户界面 ui](#16-用户界面-ui)
17. [构建系统 CMake + vcpkg](#17-构建系统-cmake--vcpkg)
18. [配置文件 Schema（YAML）](#18-配置文件-schemayaml)
19. [数据流与事件总线](#19-数据流与事件总线)
20. [错误处理与异常安全](#20-错误处理与异常安全)
21. [单元测试策略](#21-单元测试策略)
22. [实施计划与里程碑](#22-实施计划与里程碑)
23. [风险与开放问题](#23-风险与开放问题)
24. [附录 A：IPC 能力清单 → C++ 方法映射](#24-附录-aipc-能力清单--c-方法映射)
25. [附录 B：关键实现难点备忘](#25-附录-b关键实现难点备忘)

---

## 1. 文档目的与阅读对象

本文档是**从 Electron 原工程到原生 C++/Qt 的完整迁移设计**。它：

1. 精确记录原工程的实际功能与运行方式（已阅读原源码 `src/main/*`、`src/preload/*`、`src/renderer/src/*`、`src/shared/types/*`）。
2. 给出 C++ 版的目录树、命名空间、类职责、公开接口（头文件完整声明）、关键实现要点、构建文件（CMake/vcpkg/qrc）。
3. 明确哪些函数本期实现、哪些留空占位后续填充（标注 `// TODO(phase N)`）。

阅读对象：接手本项目的 C++ 实施者。要求熟悉 C++20、Qt 6 信号槽/事件循环、CMake、vcpkg，并了解 Mihomo（Clash Meta）外部控制器协议。

> 约定：本文代码块中的头文件为**可直接使用的接口契约**；`*.cpp` 为关键实现示意，能指导实现但未必逐行照抄。平台相关代码用 `#if defined(Q_OS_WIN)` / `#elif defined(Q_OS_MACOS)` / `#elif defined(Q_OS_LINUX)` 隔离，或拆分为同名不同后缀的 `.cpp` / `.mm`。

---

## 2. 现状梳理：Electron 实现映射

### 2.1 原进程边界

```
main/       Electron 主进程：拥有 Mihomo、文件配置、系统代理/TUN、服务模式、托盘/窗口、更新、后台 server
preload/    contextBridge 安全暴露
renderer/   React 界面 + 辅助窗口（悬浮窗、托盘菜单窗）
shared/     类型定义 + IPC 通道/事件名
```

C++ 版无跨进程渲染层：**GUI 本身就是主进程**，原来 `main ↔ renderer` 的 IPC 变成同一进程内的 **信号槽 + 直接调用**。辅助窗口（悬浮窗、托盘菜单）改为 `QWidget`/`QMenu`。

### 2.2 启动流程对照

```text
原：main/index.ts → requestSingleInstanceLock → init → whenReady → registerIpc
    → createWindow → startCore → startMonitor → initShortcut → createTray → (floating)

新：main.cpp → 单实例守卫 → 全局异常钩子 → ConfigManager/Paths 初始化
    → AppController::startup()（异步编排：创建 MainWindow、启动 CoreManager、
      启动流量监控（仅 Win）、注册快捷键、创建托盘）
```

### 2.3 功能页面（14 个）与后台能力清单

| 原页面/路由 | C++ 页面类 | MVP 阶段 |
|---|---|---|
| `/proxies`（代理，默认首页） | `ProxiesPage` | P1 |
| `/rules`（规则） | `RulesPage` | P1 |
| `/logs`（日志） | `LogsPage` | P1 |
| `/settings`（设置） | `SettingsPage` | P1 |
| `/mihomo`（内核） | `MihomoPage` | P1 |
| `/sysproxy`（系统代理） | `SysProxyPage` | P1 |
| `/connections`（连接） | `ConnectionsPage` | P2 |
| `/profiles`（订阅/配置） | `ProfilesPage` | P2 |
| `/override`（覆写） | `OverridePage` | P2 |
| `/tun` | `TunPage` | P2 |
| `/dns` | `DnsPage` | P2 |
| `/sniffer` | `SnifferPage` | P2 |
| `/resources`（资源） | `ResourcesPage` | P2 |
| `/substore` | `SubStorePage` | P3 |

> 说明：原工程**没有**「概况」页，默认落在 `/proxies`。需求描述中的「概况」在本移植中映射为一个可选的 `OverviewPage`（聚合状态/流量/最近日志的仪表盘），作为 P1 的可选项。侧边栏以代理为默认首页。

| 后台能力 | 原模块 | C++ 模块 | MVP 阶段 |
|---|---|---|---|
| 内核启停/重启/自动恢复 | `core/manager.ts`, `core/process-control.ts` | `CoreManager`, `CoreProcessController` | P1 |
| 内核 stdout 解析（就绪/错误/TUN 权限/更新完成/provider 初始化） | `core/startup-chain.ts` | `CoreManager` + `LogLineClassifier` | P1 |
| Mihomo REST（configs/proxies/rules/delay/…） | `core/mihomoApi.ts` | `MihomoApiClient` | P1 |
| Mihomo 数据流（traffic/memory/logs/connections，WebSocket） | `core/mihomoApi.ts`, `core/mihomo-stream.ts`, `utils/latest-sender.ts` | `MihomoApiClient` + `WsClient` + `LatestSender` | P1 |
| 运行时配置生成（profile+override+受控合并） | `core/factory.ts` | `RuntimeConfigFactory` | P1 |
| 应用配置/受控配置/订阅/覆写 | `config/*.ts` | `ConfigManager` + `YamlStore<T>` | P1 |
| 系统代理（auto/manual、bypass、guard、PAC） | `sys/sysproxy.ts`, `resolve/server.ts` | `SystemProxyManager` + `ISystemProxy` + `PacServer` | P1 |
| macOS 自动设置/恢复 DNS | `core/network.ts` | `platform::DnsSetter` | P2 |
| 网络检测自动启停内核 | `core/network.ts` | `platform::NetworkDetector` | P2 |
| 托盘/悬浮窗/快捷键 | `resolve/tray.ts`, `resolve/floatingWindow.ts`, `resolve/shortcut.ts` | `platform::TrayIcon`, `ui::FloatingWindow`, `platform::ShortcutManager` | P2 |
| 服务模式（systemd/launchd/Windows 服务） | `service/*`, `core/service-core-runtime.ts` | `platform::ServiceManager` | P3 |
| 主题（多配色 + 自定义） | `resolve/theme.ts` | `ui::ThemeManager`（QSS） | P1 |
| 单实例 + deep link | `bootstrap/single-instance.ts`, `resolve/deepLink.ts` | `core::SingleInstance` | P1 |
| Gist/WebDAV 备份、Sub-Store、自动更新 | `resolve/gistApi.ts`, `resolve/backup.ts`, `resolve/autoUpdater.ts` | 各对应模块 | P3 |
| 流量监控（Windows TrafficMonitor.exe） | `resolve/trafficMonitor.ts` | `platform::TrafficMonitorHost`（原样调 exe） | P2 |

---

## 3. 技术选型与约束

### 3.1 硬性规定（用户指定）

| 项 | 选型 | 说明 |
|---|---|---|
| 语言 | C++20 | 允许 `concepts`/`ranges`/`std::format`（若编译器支持）/三向比较 |
| 构建 | CMake 3.20+ | `AUTOMOC/AUTORCC/AUTOUIC` |
| 包管理 | vcpkg manifest | `vcpkg.json` + `builtin-baseline` |
| GUI | Qt 6（Widgets 为主，QML 可选） | 本项目基座用 Widgets（菜单/托盘/主题控制更直接） |
| JSON | nlohmann/json | REST 响应解析、`core.pid.json` |
| 日志 | spdlog | 异步 logger + 按日 rolling |
| YAML | yaml-cpp | 机器管理配置的序列化 |
| 测试 | Qt Test 或 GoogleTest | 建议 Qt Test（与 Qt 类型无缝）+ 少量 GoogleTest（纯逻辑） |

### 3.2 原依赖 → C++ 替代

| Electron 依赖 | C++ 方案 |
|---|---|
| `electron` 子进程/服务 | `QProcess` + 平台服务 |
| `ws`（WebSocket） | 自研 `WsClient`（基于 `QLocalSocket`/`QTcpSocket`，见 §12） |
| `axios`（HTTP） | 自研轻量 `HttpClient`（基于 `QLocalSocket` 的 HTTP/1.1 + `QNetworkAccessManager` 用于 TCP） |
| `js-yaml` / `yaml` | yaml-cpp（解析）；原文串保真（见 §9.4） |
| `sysproxy-go`（sparkle-service 的 sysproxy 子命令） | 平台原生实现（注册表/SCDynamicStore/gsettings，见 §14） |
| React/HeroUI/Tailwind | Qt Widgets + QSS 主题 |
| `express`（PAC server） | `PacServer`（`QTcpServer` 内建 HTTP） |
| `vm`（JS 覆写脚本） | `RuntimeConfigFactory` 内嵌 JS：**P3**（需 QuickJS/Duktape，见 §23 风险 7） |
| `age` 加密 | P3，可选 `rage`（C++ age 库）或内置实现 |

---

## 4. 总体架构与线程模型

### 4.1 分层

```
┌─────────────────────────────────────────────────────────────┐
│  ui 层 (sparkle::ui)                                       │
│  MainWindow · Sidebar · Pages · Models(QAbstractListModel) │
│  ThemeManager · FloatingWindow · Tray 窗口                  │
└──────────────▲────────────────────────────┬────────────────┘
               │ 信号/槽（Qt 直连/队列）       │ 数据模型查询
┌──────────────┴────────────────────────────▼────────────────┐
│  core 层 (sparkle::core)：应用逻辑，不依赖具体 OS           │
│  AppController（组合根/编排）                               │
│  CoreManager · ConfigManager · RuntimeConfigFactory        │
│  MihomoApiClient(HTTP+WS) · CoreProcessController          │
│  SystemProxyManager(门面) · PacServer · SingleInstance     │
│  LogManager · Paths · Models(纯数据)                       │
└──────────────▲─────────────────────────────────────────────┘
               │ 抽象接口（ISystemProxy / IService / ...）
┌──────────────┴─────────────────────────────────────────────┐
│  platform 层 (sparkle::platform)：平台隔离                  │
│  SystemProxyImpl(win/mac/linux) · DnsSetter · NetworkDetector│
│  TrayIcon · ShortcutManager · ServiceManager                │
└─────────────────────────────────────────────────────────────┘
```

依赖方向严格单向：`ui → core → platform`。`core` 只通过 `platform::ISystemProxy` 等抽象接口调用平台能力，由 `platform::Factory` 在运行时注入。

### 4.2 组合根

`main.cpp` 是唯一允许做 `new` 的地方（对象所有权清晰）：

```text
main.cpp（栈上或 main 内局部/单例容器）
  └─ AppController（QObject，组合根）
       ├─ Paths（已初始化）
       ├─ LogManager
       ├─ ConfigManager
       ├─ RuntimeConfigFactory
       ├─ MihomoApiClient
       ├─ CoreManager
       ├─ SystemProxyManager（注入 platform::ISystemProxy 实现）
       ├─ PacServer
       ├─ platform::TrayIcon / NetworkDetector / ShortcutManager（P2）
       └─ ui::MainWindow
```

### 4.3 线程模型（关键决策）

**以 Qt 事件循环为主的单线程模型 + 少量工作线程**：

- `QProcess` 本身异步（信号驱动），stdout 解析在 `readyReadStandardOutput` 里做**行缓冲**，不阻塞。
- WebSocket/HTTP 用异步 socket（`QLocalSocket` 信号驱动），**不引入 per-request 线程**。
- 仅以下场景用 `QtConcurrent` 或 `std::jthread` 工作线程：
  - spdlog 异步日志（spdlog 自带后台线程）。
  - 延迟测试并发（原工程 `delayTestConcurrency`）——用 `QtConcurrent::mapped`。
  - 订阅下载/更新的阻塞网络——`QNetworkAccessManager` 异步即可，不另开线程。
- 所有核心对象都 `moveToThread` 到主线程（默认），跨对象通信用 Qt 队列连接。**不强制**每管理器一个线程，避免不必要的锁。

> 约定：`QObject` 子类禁止拷贝，天然满足禁用裸 new/delete 与 RAII（Qt 父子所有权 + `std::unique_ptr` 拥有非 QObject 资源）。

---

## 5. 命名空间与目录结构

### 5.1 命名空间

- `sparkle::core` —— 应用逻辑、数据模型、内核/配置/系统代理门面。
- `sparkle::ui` —— Qt Widgets 界面。
- `sparkle::platform` —— 平台隔离实现（`ISystemProxy` 等抽象接口也放此处）。

### 5.2 目录树

```text
sparkle-cpp/
├── CMakeLists.txt                 # 根构建
├── CMakePresets.json              # 构建预设（vcpkg toolchain）
├── vcpkg.json                     # 依赖清单（manifest 模式）
├── vcpkg-configuration.json       # registry 配置（可选）
├── .clang-format
├── cmake/
│   └── SparkleFunctions.cmake     # 公共宏（sparkle_add_test 等）
├── src/
│   ├── app/
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp               # 入口
│   │   ├── app.rc                 # Windows 资源（图标/版本）
│   │   └── Info.plist.in          # macOS 包信息
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── models.h               # 数据模型 + 枚举（§6）
│   │   ├── models.cpp             # 字符串转换、metatype 注册
│   │   ├── paths.h / paths.cpp    # 路径解析（§7）
│   │   ├── log_manager.h / .cpp   # spdlog 封装（§8）
│   │   ├── yaml_store.h           # 模板：通用 YAML 缓存读写（§9）
│   │   ├── config_manager.h / .cpp
│   │   ├── runtime_config_factory.h / .cpp
│   │   ├── core_process_controller.h / .cpp
│   │   ├── http_client.h / .cpp   # QLocalSocket 上的 HTTP/1.1
│   │   ├── ws_client.h / .cpp     # RFC6455 客户端
│   │   ├── latest_sender.h        # 首尾最新值节流（模板）
│   │   ├── mihomo_api_client.h / .cpp
│   │   ├── core_manager.h / .cpp
│   │   ├── system_proxy_manager.h / .cpp
│   │   ├── pac_server.h / .cpp
│   │   ├── single_instance.h / .cpp
│   │   └── app_controller.h / .cpp
│   ├── platform/
│   │   ├── CMakeLists.txt
│   │   ├── system_proxy.h         # ISystemProxy 接口 + 工厂
│   │   ├── system_proxy_win.cpp
│   │   ├── system_proxy_mac.mm
│   │   ├── system_proxy_linux.cpp
│   │   ├── dns_setter.h / dns_setter_mac.mm
│   │   ├── network_detector.h / .cpp
│   │   ├── tray_icon.h / .cpp
│   │   ├── shortcut_manager.h / .cpp
│   │   └── service_manager.h / .cpp   # P3
│   └── ui/
│       ├── CMakeLists.txt
│       ├── main_window.h / .cpp
│       ├── sidebar.h / .cpp
│       ├── page_base.h
│       ├── pages/
│       │   ├── overview_page.h / .cpp
│       │   ├── proxies_page.h / .cpp
│       │   ├── rules_page.h / .cpp
│       │   ├── logs_page.h / .cpp
│       │   └── settings_page.h / .cpp
│       ├── models/
│       │   ├── proxy_list_model.h / .cpp    # ProxyNode/Group
│       │   ├── log_list_model.h / .cpp      # LogEntry 环形
│       │   └── traffic_model.h / .cpp       # TrafficStats
│       └── theme/
│           └── theme_manager.h / .cpp
├── resources/
│   ├── resources.qrc             # 资源清单示例（§17.4）
│   ├── icons/
│   │   ├── app.png
│   │   └── tray.png
│   └── themes/
│       ├── dark.qss
│       └── light.qss
└── tests/
    ├── CMakeLists.txt
    ├── tst_config_manager.cpp
    ├── tst_core_process_controller.cpp
    ├── tst_ws_client.cpp
    ├── tst_http_client.cpp
    ├── tst_runtime_config_factory.cpp
    └── tst_system_proxy.cpp
```

---

## 6. 数据模型

`src/core/models.h` —— 纯数据结构（POD 风格，无 QObject，跨线程传递前需 `Q_DECLARE_METATYPE` + `qRegisterMetaType`）。

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sparkle::core {

// ===== 枚举 =====

enum class OutboundMode { Rule, Global, Direct };
enum class LogLevel { Silent, Error, Warning, Info, Debug };
enum class CoreState { Stopped, Starting, Running, Stopping, Failed };
enum class CoreKind { Mihomo, MihomoAlpha, System };   // 内核种类

enum class ProxyType {
  Direct, Reject, RejectDrop, Compatible, Pass, Dns, Relay,
  Selector, Fallback, UrlTest, LoadBalance,
  Shadowsocks, ShadowsocksR, Snell, Socks5, Http, Vmess, Vless, Trojan,
  Hysteria, Hysteria2, WireGuard, Tuic, Ssh, Mieru, AnyTls, Sudoku, Masque,
  Unknown
};

enum class SysProxyMode { Auto, Manual };
enum class SysProxySettingMode { Exec, Service };      // 对应原 settingMode

// ===== 基础量 =====

using Bytes = std::uint64_t;
using TimestampMs = std::int64_t;

// ===== 流量 / 内存（对应 ControllerTraffic / ControllerMemory）=====

struct TrafficStats {
  Bytes upload = 0;     // 字节/秒
  Bytes download = 0;   // 字节/秒
};

struct MemoryStats {
  Bytes inUse = 0;
  Bytes osLimit = 0;
};

// ===== 延迟样本（对应 ControllerProxiesHistory）=====

struct DelaySample {
  TimestampMs time = 0;
  int delay = -1;       // -1 表示无数据/超期
};

// ===== 代理节点（对应 proxies/providers 里的节点）=====

struct ProxyNode {
  std::string name;
  ProxyType type = ProxyType::Unknown;
  bool alive = false;
  int delay = -1;                       // -1 = 未测
  std::string providerName;             // 来自 provider 时可空
  std::vector<DelaySample> history;
  std::string dialerProxy;              // 详情提示透传
};

// ===== 代理组（对应 ControllerGroupDetail）=====

struct ProxyGroup {
  std::string name;
  ProxyType type = ProxyType::Unknown;
  std::string now;                      // 当前选中
  std::vector<std::string> all;         // 成员名列表
  bool hidden = false;
  bool fixed = false;                   // fixed（不可切换）
  std::string testUrl;
};

// ===== 日志条目（对应 ControllerLog）=====

struct LogEntry {
  std::uint64_t seq = 0;                // 递增序号（去重启后仍唯一）
  LogLevel level = LogLevel::Info;
  std::string payload;
  TimestampMs time = 0;                 // epoch ms（原始日志带时间则用原文时间）
};

// ===== 连接（对应 ControllerConnectionDetail，字段精简）=====

struct ConnectionItem {
  std::string id;
  std::string network;                  // tcp/udp
  std::string type;
  std::string sourceIp, destinationIp;
  std::string sourcePort, destinationPort;
  std::string host, process, processPath;
  std::vector<std::string> chains;
  std::string rule, rulePayload;
  Bytes upload = 0, download = 0;
  TimestampMs start = 0;
};

// ===== 规则（对应 ControllerRulesDetail）=====

struct RuleItem {
  std::size_t index = 0;
  std::string type, payload, proxy;
  std::size_t size = 0;
  bool disabled = false;
  std::uint64_t hitCount = 0;
  std::uint64_t missCount = 0;
};

// ===== 控制器响应载体 =====

struct ControllerVersion { std::string version; bool meta = false; };

// ===== 应用配置（对应 AppConfig，字段列表见 §18 完整 schema）=====

struct AppConfig {
  // 常用字段见下；完整字段以 §18 的 YAML schema 为准。
  CoreKind core = CoreKind::Mihomo;
  std::string systemCorePath;                    // core == System 时
  std::string corePermissionMode = "elevated";   // "elevated" | "service"
  bool disableLoopbackDetector = false;
  bool disableEmbedCA = false;
  bool disableSystemCA = false;
  bool disableNftables = false;
  std::vector<std::string> safePaths;

  OutboundMode mode = OutboundMode::Rule;        // 快捷键/首页用
  LogLevel logLevel = LogLevel::Info;
  LogLevel realtimeLogLevel = LogLevel::Info;

  bool sysProxyEnable = false;                   // 门面字段，实为 sysProxy.enable
  SysProxyConfig sysProxy;                       // §18

  bool autoRestartOnCrash = true;                // 原工程恒 true（retry=10）
  int coreRestartRetries = 10;
  std::string mihomoCpuPriority = "PRIORITY_NORMAL";

  // ……其余 UI/更新/主题/网络检测/Gist/SubStore 字段在 §18 列出。
};

// ===== 系统代理配置 =====

struct SysProxyConfig {
  bool enable = false;
  std::string host;                    // 默认 127.0.0.1
  SysProxyMode mode = SysProxyMode::Manual;
  std::vector<std::string> bypass;
  SysProxySettingMode settingMode = SysProxySettingMode::Exec;
  bool guard = false;
  bool guardNotify = false;
};

// ===== 订阅/配置（对应 ProfileConfig / ProfileItem）=====

struct ProfileItem {
  std::string id;
  std::string type = "local";         // "remote" | "local"
  std::string name;
  std::string url;                    // remote
  std::string fingerprint;            // remote
  std::string ua;                     // remote
  std::string file;                   // local
  bool verify = false;
  int interval = 0;
  std::optional<std::string> home;
  std::optional<TimestampMs> updated;
  std::vector<std::string> override_; // override 是 C++ 关键字？不，不是；但为避免混淆命名 overrideIds
  bool useProxy = false;
  std::optional<std::string> ageRecipient;
  std::optional<std::string> ageIdentity;
  bool substore = false;
  bool locked = false;
  bool autoUpdate = true;
};

struct ProfileConfig {
  std::optional<std::string> current;
  std::vector<ProfileItem> items;
};

// ===== 覆写（对应 OverrideConfig / OverrideItem）=====

struct OverrideItem {
  std::string id;
  std::string type = "local";         // "remote" | "local"
  std::string ext = "js";             // "js" | "yaml"
  std::string name;
  bool global = false;
  std::optional<TimestampMs> updated;
  std::string url;
  std::string file;
  std::string fingerprint;
};

struct OverrideConfig {
  std::vector<OverrideItem> items;
};

// ===== 受控 Mihomo 配置（UI 可视化的内核配置，对应 MihomoConfig）=====
// 见 §18；以 JSON/YAML 透传保存，UI 表单读写其中字段。

}  // namespace sparkle::core

Q_DECLARE_METATYPE(sparkle::core::TrafficStats)
Q_DECLARE_METATYPE(sparkle::core::LogEntry)
Q_DECLARE_METATYPE(sparkle::core::CoreState)
```

**字符串转换**：`models.cpp` 提供 `toString(OutboundMode)`、`fromString<OutboundMode>()`、`ProxyType ⇆ std::string` 映射表（原 `MihomoProxyType` 联合类型），供 JSON 解析/UI 显示复用。

> 实现要点：`ProxyType` 的映射表覆盖原工程 30+ 种类型；未知值一律 `Unknown` 并保留原始字符串以便前向兼容。

---

## 7. 路径管理 Paths

`src/core/paths.h` —— 单例，初始化后静态提供所有路径。**对应原 `utils/dirs.ts`**。

```cpp
#pragma once

#include <QString>
#include <filesystem>
#include <optional>

#include "models.h"

namespace sparkle::core {

// 路径解析器：初始化时确定 executable 与 portable 模式，随后全部静态。
class Paths {
public:
  static void initialize(const std::filesystem::path& executablePath, bool portable);
  static bool isPortable();

  static std::filesystem::path executablePath();
  static std::filesystem::path executableDir();
  static std::filesystem::path dataDir();                 // portable? exeDir/data : 平台 userData

  static std::filesystem::path appConfigPath();           // dataDir/config.yaml
  static std::filesystem::path controlledMihomoConfigPath(); // dataDir/mihomo.yaml
  static std::filesystem::path profileConfigPath();       // dataDir/profile.yaml
  static std::filesystem::path overrideConfigPath();      // dataDir/override.yaml

  static std::filesystem::path profilesDir();             // dataDir/profiles
  static std::filesystem::path profilePath(const QString& id);
  static std::filesystem::path overrideDir();             // dataDir/override
  static std::filesystem::path overridePath(const QString& id, const QString& ext);

  static std::filesystem::path workDir();                 // dataDir/work
  static std::filesystem::path profileWorkDir(const std::optional<QString>& id); // work/<id|default>
  static std::filesystem::path workConfigPath(const std::optional<QString>& idOrWork);

  static std::filesystem::path logDir();                  // dataDir/logs
  static std::filesystem::path appLogPath();
  static std::filesystem::path coreLogPath();

  // 控制器端点（§12）
  static QString controllerSocket();
  //   Windows:  \\.\pipe\Sparkle\mihomo
  //   Unix:     /tmp/sparkle-mihomo-api.sock（无权限时 -noperm 变体）

  // 内核可执行文件
  static std::filesystem::path mihomoCorePath(CoreKind kind);
  static QString resolveMihomoCorePath();
  //   1) core == System: 用 appConfig.systemCorePath
  //   2) 环境变量 SPARKLE_MIHOMO_PATH 非空则优先（需求指定）
  //   3) 否则 resources/sidecar/mihomo[.exe]

  static std::filesystem::path sidecarDir();

  Paths() = delete;
};

}  // namespace sparkle::core
```

**默认数据目录**（需求指定，与 `QStandardPaths::AppDataLocation` 一致）：

- Linux：`~/.config/sparkle/`
- Windows：`%APPDATA%/sparkle/`（`C:\Users\<u>\AppData\Roaming\sparkle`）
- macOS：`~/Library/Application Support/sparkle/`

用 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` 获取；便携模式用 `exeDir()/data`（原 `PORTABLE` 文件检测逻辑保留）。

---

## 8. 日志管理 LogManager

`src/core/log_manager.h` —— **对应原 `utils/log.ts`**。spdlog 文件写入 + 内存环形缓冲（供日志页 + 最近日志回放）。

```cpp
#pragma once

#include <QObject>
#include <deque>
#include <memory>

#include "models.h"

namespace sparkle::core {

// 三种日志目标（对应原 LogTarget）
enum class LogTarget { App, Core, SubStore };

class LogManager final : public QObject {
  Q_OBJECT
public:
  explicit LogManager(QObject* parent = nullptr);
  ~LogManager() override;

  void configure(bool saveLogs, std::size_t maxFileSizeMB, std::size_t maxEntries);

  void appendAppLog(const QString& line);                 // 应用日志
  void setMihomoLogSourceFromConsole(bool fromConsole);   // 'out' vs 'ws'
  void publishMihomoLog(const LogEntry& entry);           // WebSocket 来源日志
  void appendRawCoreChunk(const QByteArray& chunk, LogLevel fallbackLevel); // stdout 行缓冲 → 解析 logfmt → 广播

  std::vector<LogEntry> cachedMihomoLogs() const;         // 日志页初始化回放（上限 2000）
  void clearCachedMihomoLogs();

signals:
  void mihomoLog(const sparkle::core::LogEntry& entry);

private:
  struct Impl;                          // pimpl：持有 spdlog logger、环形缓冲、写队列
  std::unique_ptr<Impl> d_;
};

}  // namespace sparkle::core
```

**实现要点（映射原 log.ts）**：

1. **logfmt 解析**：原 `parseLogfmtLine` 用正则拆分 `key=value`，处理引号/转义；C++ 用 `std::regex`（`([A-Za-z0-9_.-]+)=("(?:\\.|[^"\\])*"|[^\s]+)`）。把 stdout 里的 `time`/`level`/`msg` 解析成 `LogEntry`，无法解析时按 `fallbackLevel`（info/error）整行作为 payload。
2. **行缓冲**：`QProcess` 输出按 chunk 到达，需 `\r?\n` 切行，残留半行入缓冲（ecapsulate 原始 `flushCoreLogLines`）。
3. **环形缓冲**：上限 2000（原 `cachedLogLimit`），FIFO 裁剪。
4. **文件写入**：`spdlog` 按日 `sinks::daily_file_sink` 或自实现按日路径（原 `datedLogPath`），`maxFileSizeMB` 超限截断（原 `trimLogFileToSize` 保留尾部 70%）。
5. **写队列**：app/core/substore 各自串行写（原 `writeQueue` 防穿插）。
6. **去重/节流**：日志**不节流**（原注释明确：去节流会丢历史，影响诊断）。

---

## 9. 配置管理 ConfigManager 与 YamlStore

### 9.1 通用 YAML 缓存读写 `YamlStore<T>`

**对应原 `config/cached-yaml-store.ts` + `app.ts` 的原子写**。模板头文件 `src/core/yaml_store.h`：

```cpp
#pragma once

#include <filesystem>
#include <functional>
#include <optional>

namespace sparkle::core {

// T 需可被 nlohmann::json 序列化（to_json/from_json 特化，见 §9.4）。
// 行为：lazy load + 内存缓存 + force reload + 序列化 + 原子写 + 缺省初始化。
template <typename T>
class YamlStore {
public:
  using Factory = std::function<T()>;
  using Normalizer = std::function<T(T)>;

  YamlStore(std::filesystem::path path, Factory createDefault,
            Normalizer normalize, bool initializeOnMissing = true)
      : path_(std::move(path)),
        createDefault_(std::move(createDefault)),
        normalize_(std::move(normalize)),
        initializeOnMissing_(initializeOnMissing) {}

  T get(bool force = false);       // 首次/force 时读盘并 normalize，否则返回缓存
  void set(const T& value);        // 缓存 + 序列化 + tmp→rename 原子写
  void clear();

private:
  std::filesystem::path path_;
  Factory createDefault_;
  Normalizer normalize_;
  bool initializeOnMissing_;
  std::optional<T> cache_;
};

}  // namespace sparkle::core
```

不变量（对应原 `cached-yaml-store.test.ts`）：

1. 首次 `get` 失败（文件不存在）→ `initializeOnMissing ? createDefault() : 保留不存在`；
2. 非 `ENOENT` 错误 → 抛出；
3. `set` 后立即回写并更新缓存；`clear` 后下次 `get` 强制重读；
4. `appConfig` 额外保留「读 `.tmp`/`.backup` 修复 + 原子写」特殊逻辑（原 `app.ts::safeWriteConfig`）。

### 9.2 ConfigManager

`src/core/config_manager.h` —— 汇聚 4 个 YamlStore + 节点 CRUD + 变更信号。

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "models.h"
#include "yaml_store.h"

namespace sparkle::core {

class ConfigManager final : public QObject {
  Q_OBJECT
public:
  explicit ConfigManager(QObject* parent = nullptr);

  // ---- App 配置 ----
  AppConfig appConfig(bool force = false);
  void patchAppConfig(const AppConfig& patch);       // 深度合并 + 原子写

  // ---- 受控 Mihomo 配置 ----
  std::map<std::string, nlohmann::json> controlledMihomoConfig(bool force = false);
  void patchControlledMihomoConfig(const std::map<std::string, nlohmann::json>& patch);

  // ---- 订阅/配置 ----
  ProfileConfig profileConfig(bool force = false);
  void setProfileConfig(const ProfileConfig& config);
  ProfileItem profileItem(const std::optional<QString>& id) const;   // id 空 -> default
  ProfileItem currentProfileItem() const;
  void changeCurrentProfile(const QString& id);    // 失败回滚 current，成功后请求内核重启
  void addProfileItem(const ProfileItem& item);
  void removeProfileItem(const QString& id);
  QString profileRawText(const QString& id);       // 原文串（保真）
  void setProfileRawText(const QString& id, const QString& content);

  // ---- 覆写 ----
  OverrideConfig overrideConfig(bool force = false);
  void setOverrideConfig(const OverrideConfig& config);
  QString overrideText(const QString& id, const QString& ext);
  void setOverrideText(const QString& id, const QString& ext, const QString& content);

  // ---- 节点 CRUD（需求 3：名称/类型/服务器/端口/加密/密码）----
  std::vector<ProxyNode> parseProxyNodes();         // 从当前 profile 的 proxies 段解析
  std::vector<ProxyGroup> parseProxyGroups();
  std::vector<RuleItem>  parseRules();
  void upsertProxy(const ProxyNode& node);          // 增/改 → 回写当前 profile 的 proxies 段
  void removeProxy(const QString& name);
  void clearProxies();

signals:
  void appConfigChanged();
  void controlledMihomoConfigChanged();
  void profileConfigChanged();
  void overrideConfigChanged();
  void reloadRequested();        // 订阅变更/受控配置变更 → RuntimeConfigFactory 重新生成 + 内核重启

private:
  struct Impl;
  std::unique_ptr<Impl> d_;      // 持有 4 个 YamlStore
};

}  // namespace sparkle::core
```

### 9.3 变更 → 内核重载的串联

对应原 `patchControledMihomoConfig` 里「先 `generateProfile()` 再写回」：

```text
patchControlledMihomoConfig(patch)
  → 合并到 mihomo.yaml 的内存对象
  → RuntimeConfigFactory::generateRuntimeConfig()   // 重生成 work/config.yaml，且回写 mihomo.yaml 归一化值
  → YamlStore::set(下一版)                           // 落盘 mihomo.yaml
  → emit controlledMihomoConfigChanged()
  →（mode/dns/sniffer 变化等同理；profile 变更最终 restartCore）
```

### 9.4 YAML 保真策略（重要设计决策）

yaml-cpp **不保留注释与原始排版**。因此：

- **机器管理、可重建**的 YAML（`config.yaml`/`mihomo.yaml`/`profile.yaml`/`override.yaml`/`work/config.yaml`）→ 用 yaml-cpp 解析为 `YAML::Node` 或经 `nlohmann::json` 兜底（见 §9.5），序列化重写**可接受**。
- **用户手写、需保留注释**的 profile/override 正文 → **始终以原文串 `QString` 持有**；只有「生成运行时配置」时才解析合并。节点 CRUD 对 profile 的修改需**保留其余部分**：采用「解析为带注释的内存树 + 仅替换 proxies 段」会丢失注释；因此 v1 的节点 CRUD 建议**直接对字符串做 YAML 段落级编辑**，或将此能力标记为 P2（原工程本身也是文本编辑式管理节点，无结构化 CRUD）。

> 结论写入文档：**结构化节点 CRUD 是本次移植新增的便利能力**，实施时优先用「原文串 + 安全重写」，注释保真失败时降级为「整段覆盖 proxies 段」并提示用户。

### 9.5 JSON 序列化桥接

为复用原工程的 JSON 语义（`deepMerge`、字段名带连字符等），设计约定：

- 受控 Mihomo 配置用 `nlohmann::json` 作为「运行时通用对象」（`std::map<std::string, nlohmann::json>`），YAML ⇆ JSON 双向转换（nlohmann 提供 `json → YAML::Node`、`YAML::Node → json` 适配，或经 `yaml-cpp` 的 `<<`/`as`）。
- 深度合并函数 `deepMerge(json& base, const json& patch, bool mergeArrays=false)`（对应原 `utils/merge.ts`），供 `RuntimeConfigFactory` 使用。

---

## 10. 运行时配置生成 RuntimeConfigFactory

`src/core/runtime_config_factory.h` —— **对应原 `core/factory.ts::generateProfile`**。

```cpp
#pragma once

#include <QObject>
#include <QString>

namespace sparkle::core {

class ConfigManager;

class RuntimeConfigFactory final : public QObject {
  Q_OBJECT
public:
  explicit RuntimeConfigFactory(ConfigManager* config, QObject* parent = nullptr);

  // 对应 generateProfile()：raw profile → override 合并 → controlled 深度合并
  //   → clean* 归一化 → 回写受控配置 → 写 work/config.yaml
  void generate();

  std::map<std::string, nlohmann::json> runtimeConfig() const;
  QString runtimeConfigStr() const;    // getRuntimeConfigStr
  QString rawProfileStr() const;
  QString currentProfileStr() const;
  QString overrideProfileStr() const;

signals:
  void generated();                    // CoreManager 据此重启内核

private:
  std::map<std::string, nlohmann::json> cleanProfile(std::map<std::string, nlohmann::json> profile);
  std::map<std::string, nlohmann::json> overrideProfile(const std::optional<QString>& current,
                                                       std::map<std::string, nlohmann::json> profile);
  // ……cleanBoolean/Number/String/Tun/Dns/Sniffer/Proxy、syncControlled、mergeDetailedConfig
};

}  // namespace sparkle::core
```

**必须复刻的归一化规则**（见原 `factory.ts`，逐条照搬，否则内核行为不一致）：

- `log-level` 非 `info/debug` 强制 `info`；
- `mode == rule` 删除；空串字段删除；`port/*-port` 为 0 删除；
- `allow-lan` 与 `lan-allowed/disabled-ips` 联动；`127.0.0.1/8` 兜底加入；
- `authentication` 空则删 `authentication` 与 `skip-auth-prefixes`；
- TUN/DNS/Sniffer 的布尔与空数组清理；`dns.fallback`/`fallback-filter` 删除；
- `proxies/proxy-groups/rules` 空数组、`proxy-providers/rule-providers` 空对象删除；
- UI 接管字段回填（`mode`/`tun.enable`/`dns.enable`/`sniffer.enable`）；
- `controlDns=false` → 删 `dns`+`hosts`；`controlSniff=false` → 删 `sniffer`；
- 受控配置回写（`syncControledMihomoConfig` + `mergeDetailedConfig` 的数组去重/对象深合并/标量覆盖语义）。

差分工作目录 `diffWorkDir=true` 时，将 `work/` 下 `*.db|*.dat` 拷贝到 `work/<profileId>/`（原 `prepareProfileWorkDir`）。

---

## 11. 内核进程控制 CoreProcessController

`src/core/core_process_controller.h` —— **对应原 `core/process-control.ts` + `manager.ts` 的 spawn 部分**。

```cpp
#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace sparkle::core {

// 一个 Mihomo 内核进程的封装：异步启停、优雅终止、行缓冲输出。
class CoreProcessController final : public QObject {
  Q_OBJECT
public:
  explicit CoreProcessController(QObject* parent = nullptr);
  ~CoreProcessController() override;

  // 说明：fires started()/finished()/stdOutLine()/stdErrLine()
  void start(const QString& program, const QStringList& args,
             const QProcessEnvironment& env, bool detached = false);
  void stop();                       // 优雅升级终止见下
  bool isRunning() const;
  qint64 processId() const;

signals:
  void started();
  void finished(int exitCode, QProcess::ExitStatus status);
  void stdOutLine(const QString& line);
  void stdErrLine(const QString& line);
  void errorOccurred(QProcess::ProcessError error);

private:
  QProcess process_;
  QString lineBuffer_;               // 行缓冲
  void onReadyReadOut();
  void onReadyReadErr();
};

}  // namespace sparkle::core
```

**实现要点**：

- `stop()` 复刻原 `stopChildProcess`：
  1. 先发 `SIGINT`（`QProcess::terminate()`，等价 `SIGTERM`？注意：Qt `terminate()` 发 `SIGTERM`。原 JS 先 `SIGINT`）。用 `#ifdef` 区分：Unix 上 `::kill(pid, SIGINT)` 然后 3000ms 后 `SIGTERM`、6000ms 后 `SIGKILL`；Windows 上用 `QProcess::terminate`→`kill` 及 `TerminateProcess`。
  2. 期间反复 `isProcessAlive(pid)`（`kill(pid,0)` 或 `OpenProcess`+`GetExitCode`）。
- `start()` 生成 `QProcessEnvironment`（`DISABLE_LOOPBACK_DETECTOR`/`DISABLE_EMBED_CA`/`DISABLE_SYSTEM_CA`/`DISABLE_NFTABLES`/`SAFE_PATHS`/`PATH`，对应原 `createCoreEnvironment`）。
- 启动参数（对应原 `createCoreSpawnArgs`）：
  `mihomo -d <workDir|profileWorkDir> -ext-ctl-unix <sock> [-post-up/-post-down <cmd>]`
  Windows 用 `-ext-ctl-pipe \\.\pipe\Sparkle\mihomo`。
- 启动后写 `dataDir/core.pid.json`（`pid`/`path`/`startedAt`），退出/停止时删除。
- 设置进程优先级（`setpriority`/`SetPriorityClass`，映射 `PRIORITY_*`）。
- `detached`：`process_.setProcessState` 无关；Qt 无 `unref`，用 `process_.startDetached()` 或 `start()` 后不等待。轻量模式用（原 `keepCoreAlive`）。

---

## 12. Mihomo 控制器传输层（HTTP/WebSocket）

**这是本移植最关键的自研部分**，因为原工程用 **Unix socket / Windows 命名管道** 而非 TCP 连 Mihomo，而 Qt 的 `QNetworkAccessManager`/`QWebSocket` 不直接支持 unix socket。

**核心结论**：Qt 的 `QLocalSocket` 在 Windows 上即命名管道、在 Unix 上即 Unix domain socket，**正好匹配 Mihomo 的 `-ext-ctl-pipe` / `-ext-ctl-unix`**。因此在 `QLocalSocket`（`QIODevice`）之上自研两个小客户端：

- `HttpClient`：HTTP/1.1 请求/响应（仅需 `GET/POST/PUT/PATCH/DELETE`、`Content-Length`/`Transfer-Encoding: chunked` 简单半双工即可，Mihomo API 均为一次性请求-响应）。
- `WsClient`：RFC6455 客户端握手 + 帧编解码（文本帧足够，服务端发送 JSON 文本）。

### 12.1 Http客户端接口

```cpp
#pragma once
#include <QObject>
#include <functional>

namespace sparkle::core {

struct HttpResult {
  int status = 0;
  QByteArray body;
};

class HttpClient final : public QObject {
  Q_OBJECT
public:
  using Callback = std::function<void(bool ok, HttpResult)>;
  explicit HttpClient(QObject* parent = nullptr);

  void request(const QString& method, const QString& path, const QByteArray& body,
               Callback cb);            // 走当前已建立的 QLocalSocket 或 TCP 后端
private:
  // 传输后端（direct: QLocalSocket；service: QTcpSocket + 签名头）
  std::unique_ptr<QIODevice> socket_;
  QString endpoint_;                    // \\.\pipe\... 或 /tmp/...sock 或 http://127.0.0.1
  bool serviceMode_ = false;
};

}  // namespace sparkle::core
```

> 备选（若不想自研 HTTP）：用 `libcurl`（`cpr`），其 `CURLOPT_UNIX_SOCKET_PATH` 原生支持 unix socket，Windows 命名管道则有 `CURLOPT_...` 限制。**设计默认自研 `HttpClient`**（依赖面最小、与 `QLocalSocket` 一致），`cpr` 作为 P3 备选。

### 12.2 WsClient 接口（对应原 `mihomo-stream.ts`）

```cpp
#pragma once
#include <QObject>
#include <QLocalSocket>
#include <functional>

namespace sparkle::core {

// 一条长连接流，含「单一活动 socket + 有界重连 + 成功消息恢复预算」语义。
class WsClient final : public QObject {
  Q_OBJECT
public:
  using ConnectFactory = std::function<std::unique_ptr<QIODevice>(QObject* parent)>;
  using OnMessage = std::function<void(const QByteArray&)>;

  WsClient(QObject* parent = nullptr);
  ~WsClient() override;

  void configure(ConnectFactory connect, OnMessage onMessage, int retryBudget = 10);
  void start();                 // 无活动 socket/待重连时连接
  void stop();                  // 重置预算、清理定时器、静默关闭
  void restart();               // stop + 立即 connect
  void resetRetryBudget();

private:
  // 1s 定时重连（对应 wsReconnectDelay=1000）；onMessage 成功恢复 retry
  ConnectFactory connect_;
  OnMessage onMessage_;
  int retryBudget_ = 10;
  int retry_ = retryBudget_;
  std::unique_ptr<QIODevice> socket_;
  QTimer* reconnectTimer_ = nullptr;
};

}  // namespace sparkle::core
```

**必须复刻的原语义**（见原 `mihomo-stream.ts` 注释）：

- 任意时刻最多一个活动 socket 或一个待定重连；
- 每收到一条消息 → `retry = retryBudget`；
- `onclose` → 若仍有关联 socket 或 `retry==0` 或已有重连定时器则不动作，否则 `retry--` 并 1s 后重连；
- `onerror` → `close()`，由 close 处理器驱动重试；
- `stop()` 重置预算、清定时器、静默关闭；
- `start()` 幂等（活动 socket 或已连接则返回）。

> 对应原 `createMihomoStream` 的 4 个实例：traffic（含 `LatestSender` 节流 100ms）、memory、logs（不节流）、connections（节流 200ms）。

### 12.3 节流 `LatestSender<T>`（对应原 `utils/latest-sender.ts`）

```cpp
#pragma once
#include <QObject>
#include <QTimer>
#include <functional>

namespace sparkle::core {

// 首值立即发射；节流窗口内只保留最新值；clear() 清待发值与定时器。
template <typename T>
class LatestSender final : public QObject {
  Q_OBJECT
public:
  LatestSender(int intervalMs, std::function<void(const T&)> emitFn, QObject* parent = nullptr);
  void send(const T& value);
  void clear();
  // ===== 内部 =====
};

}  // namespace sparkle::core
```

### 12.4 MihomoApiClient（REST + 四大流）

`src/core/mihomo_api_client.h` —— **对应原 `mihomoApi.ts`**。

```cpp
#pragma once

#include <QObject>
#include <functional>
#include "models.h"
#include "http_client.h"
#include "ws_client.h"
#include "latest_sender.h"

namespace sparkle::core {

class ConfigManager;
class LogManager;

// Mihomo 外部控制器客户端：REST 请求 + 4 条 WebSocket 数据流。
class MihomoApiClient final : public QObject {
  Q_OBJECT
public:
  MihomoApiClient(ConfigManager* config, LogManager* log, QObject* parent = nullptr);
  ~MihomoApiClient() override;

  // 端点切换：direct（QLocalSocket）/ service（TCP+签名，P3）
  void setServiceMode(bool service);
  void reset();                 // 对应 resetMihomoApi：停 4 流 + 清节流 + 清 HTTP 缓存

  // ---- REST（callback 风格；UI 调用可再包一层 Promise/QFuture）----
  void fetchVersion(std::function<void(const ControllerVersion&)> ok, std::function<void(QString)> err);
  void fetchConfigs(std::function<void(const nlohmann::json&)>, std::function<void(QString)>);
  void patchConfigs(const nlohmann::json& partial, std::function<void()>, std::function<void(QString)>);
  void fetchProxies(...);
  void fetchGroups(std::function<void(std::vector<ProxyGroup>)>, ...);
  void changeProxy(const QString& group, const QString& proxy, ...);
  void unfixProxy(const QString& group, ...);
  void testProxyDelay(const QString& proxy, const QString& url, int timeout,
                      const std::optional<QString>& provider, std::function<void(int)> ok, ...);
  void testGroupDelay(const QString& group, ...
  void disableRules(const std::map<QString,bool>&, ...);
  void closeConnection(const QString& id, ...);
  void closeConnections(const std::optional<QString>& name, ...);
  void upgrade(const QString& channel, ...);  // POST /upgrade?channel=
  void upgradeGeo(...); void upgradeUi(...);
  void fetchProxyProviders(...); void updateProxyProvider(...);
  void fetchRuleProviders(...); void updateRuleProvider(...);

  // ---- 数据流控制（对应 startMihomoTraffic 等）----
  void startStreams();          // traffic + memory + logs + connections
  void stopStreams();
  void restartLogsStream();     // 对应 restartMihomoLogs
  void restartConnectionsStream();

signals:
  void trafficUpdated(const sparkle::core::TrafficStats&);       // 节流 100ms
  void memoryUpdated(const sparkle::core::MemoryStats&);
  void connectionsUpdated(const std::vector<sparkle::core::ConnectionItem>&); // 节流 200ms
  // mihomo log 经 LogManager 单点广播，这里不重复发

private:
  // 4 个 WsClient 实例 + LatestSender<TrafficStats>/LatestSender<...>
};

}  // namespace sparkle::core
```

**端点与路径速查**（服务端即 Mihomo external controller，URL 前缀 `http://localhost`，Unix socket 直连）：

| 能力 | REST |
|---|---|
| 版本 | `GET /version` |
| 配置 | `GET/PATCH /configs` |
| 代理/组 | `GET /proxies`、`PUT /proxies/{group}`、`DELETE /proxies/{group}` |
| 延迟 | `GET /proxies/{name}/delay?url=&timeout=`、`GET /group/{name}/delay` |
| 规则 | `GET /rules`、`PATCH /rules/disable` |
| 连接 | `GET /connections`、`DELETE /connections/{id}`、`DELETE /connections` |
| providers | `GET/PUT /providers/proxies[...]`、`GET/PUT /providers/rules[...]` |
| 升级 | `POST /upgrade`、`/upgrade/geo`、`/upgrade/ui` |
| 数据流（WS） | `/traffic`、`/memory`、`/logs?level=`、`/connections?interval=` |

---

## 13. 内核管理 CoreManager

`src/core/core_manager.h` —— **对应原 `core/manager.ts`**（本移植 P1 实现直连模式；service 模式留 P3）。

```cpp
#pragma once

#include <QObject>
#include "models.h"

namespace sparkle::core {

class ConfigManager;
class RuntimeConfigFactory;
class CoreProcessController;
class MihomoApiClient;
class LogManager;

class CoreManager final : public QObject {
  Q_OBJECT
public:
  CoreManager(ConfigManager* config, RuntimeConfigFactory* factory,
              MihomoApiClient* api, LogManager* log, QObject* parent = nullptr);

  // ---- 对外接口（需求 2）----
  void startup(bool detached = false);   // 幂等；在途时等待既有启动
  void shutdown(bool force = false);
  void restart();
  CoreState state() const;

signals:
  void stateChanged(sparkle::core::CoreState state);
  void coreStarted();
  void coreStopped();
  void controllerListenError(const QString& message);   // 端口/命名管道被占等致命错误
  void tunPermissionError();                            // TUN 无权限 → 通知 UI 关 TUN
  void updateFinished();                                // Windows updater finished → 自动重启

private:
  void onProcessStarted();
  void onStdOutLine(const QString& line);
  void onStdErrLine(const QString& line);
  void onProcessFinished(int exitCode, QProcess::ExitStatus status);

  void startInternal(bool detached);
  void waitForReady();                 // provider 初始化 + REST 探活（/version）
  void completeInitialization();       // 触发 groupsUpdated/rulesUpdated/logLevel 校正

  ConfigManager* config_;
  RuntimeConfigFactory* factory_;
  MihomoApiClient* api_;
  LogManager* log_;
  std::unique_ptr<CoreProcessController> process_;
  CoreState state_ = CoreState::Stopped;
  int restartBudget_ = 10;             // 崩溃自动重启预算（原 retry=10）
  bool startInProgress_ = false;
};

}  // namespace sparkle::core
```

**实现要点（逐条对应原 `manager.ts`）**：

1. **预检**：读 `appConfig/controlledMihomoConfig/profileConfig` → `factory_->generate()` → `checkProfile()`（profile 合法性校验）→ 失败抛出并通知。
2. **spawn**：解析内核路径 `Paths::resolveMihomoCorePath()`（`core==system` 且失败 → 回退 `mihomo` 并重试，对应原 `patchAppConfig({core:'mihomo'})`）；构造 env 与 args → `process_->start()`。
3. **stdout 就绪检测**（对应原 `waitForCoreReadyByLog` + `startup-chain.ts`）：
   - `isControllerListenError`：`Controller listen error`（Unix socket / pipe 监听失败）→ 视为启动失败；
   - `isControllerReadyLog`：`RESTful API list. listening` / `RESTful API listening` / `external controller ... listening`；
   - `isTunPermissionError`：`Start TUN listening error: ... operation not permitted` → 关 TUN 并提示；
   - `isUpdaterFinishedLog`：`updater: finished`（仅 Win）→ 重载内核；
   - **provider 初始化跟踪**（原 `createProviderInitializationTracker`）：匹配 `Start initial provider "..."`，provider 全就绪后或默认 provider 就绪后，`api_->fetchVersion()` 探活，成功后 `completeInitialization()`。
4. **崩溃自动重启**（需求 2）：`onProcessFinished` 中若 `restartBudget_>0` → `restartBudget_--` → `restart()`；否则 `shutdown()`。
5. **stop 顺序**：恢复 DNS（macOS，P2）→ 停止 4 流 → 优雅停进程 → 删 `core.pid.json` → 重置 API。
6. **节流与日志源切换**：启动前 `LogManager::setMihomoLogSourceFromConsole(true)`（stdout 解析日志），控制器就绪后切 `ws`。
7. **Tailscale 认证日志**：stdout 中 `[Tailscale](...) auth/done` 触发系统通知 → P3（通知系统 P2）。

---

## 14. 系统代理 SystemProxyManager 与平台实现

### 14.1 平台抽象接口（`src/platform/system_proxy.h`）

```cpp
#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>
#include <memory>

namespace sparkle::platform {

enum class ProxyStatus { Disabled, Manual, Auto };

// 平台无关系统代理后端（需求 4）。
class ISystemProxy {
public:
  virtual ~ISystemProxy() = default;
  virtual void setManualProxy(const QString& host, unsigned short port,
                              const QStringList& bypass) = 0;
  virtual void setAutoProxy(const QUrl& pacUrl) = 0;
  virtual void clearProxy() = 0;
  virtual ProxyStatus status() = 0;
  // guard：变更监听 + 自动恢复（P2，原 sysproxy-guard）
  virtual void setGuardEnabled(bool enabled, bool notify) = 0;
};

// 工厂：按编译平台返回实现。
class SystemProxyFactory {
public:
  static std::unique_ptr<ISystemProxy> create();
};

}  // namespace sparkle::platform
```

### 14.2 门面 `SystemProxyManager`（`src/core/system_proxy_manager.h`）

```cpp
#pragma once

#include <QObject>
#include <memory>

namespace sparkle::core {

class ConfigManager;
namespace platform { class ISystemProxy; }

class SystemProxyManager final : public QObject {
  Q_OBJECT
public:
  SystemProxyManager(ConfigManager* config, std::unique_ptr<platform::ISystemProxy> backend,
                     QObject* parent = nullptr);

  void setProxy(bool enable);          // 触发 enable/disable（串行化，见下）
  void disable();                       // clearProxy + guard 关闭 + 停 PAC
  bool isEnabled() const;

signals:
  void proxyStateChanged(bool enabled);

private:
  ConfigManager* config_;
  std::unique_ptr<platform::ISystemProxy> backend_;
  // 串行任务队列 + 5s 断网重试（对应原 triggerSysProxy）
};

}  // namespace sparkle::core
```

**实现要点（对应原 `sys/sysproxy.ts`）**：

- 串行化：所有 `setProxy` 请求入队排队执行（原 `triggerSysProxyTask` 链 + 请求代际 `requestId` 丢弃过期任务）。
- 断网重试：`net.isOnline()` 为假时 5s 后重试 enable。
- bypass 默认值按平台：Linux/macOS/Win 三套（见原 `defaultBypass`），放平台实现或门面常量表。
- `mixed-port` 从受控配置读取，`manual` 模式 `port!=0` 才设置；`auto` 模式先启动 `PacServer` 再设置 PAC URL。
- `PacServer`（§15.6）：本地 `QTcpServer` 提供 `http://127.0.0.1:<port>/pac`，停代理时关闭。

### 14.3 Windows 实现（`system_proxy_win.cpp`，注册表 + WinINet）

对应需求：「Windows：通过注册表（Internet Settings）或 WinHTTP API」。

```cpp
// 伪代码（关键步骤）
void SystemProxyWin::setManualProxy(host, port, bypass) {
  // 1) 写 HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings:
  //    ProxyEnable=1, ProxyServer="host:port", ProxyOverride=bypass.join(";")
  QSettings reg(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings)",
                QSettings::NativeFormat);
  reg.setValue("ProxyEnable", 1);
  reg.setValue("ProxyServer", QString("%1:%2").arg(host).arg(port));
  reg.setValue("ProxyOverride", bypass.join(";"));
  // 2) 通知系统刷新（等价 INTERNET_OPTION_SETTINGS_CHANGED / REFRESH）
  InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}
void SystemProxyWin::clearProxy() {
  reg.setValue("ProxyEnable", 0);
  // ……同上刷新
}
void SystemProxyWin::setAutoProxy(const QUrl& pacUrl) {
  // AutoConfigURL=pacUrl.toString()（对于 WinHTTP 亦可写 ProxySettingsPerUser）
  reg.setValue("AutoConfigURL", pacUrl.toString());
}
```

> 注意：原工程此处用 `sparkle-service`（sysproxy-go 二进制）改系统代理。原生直改注册表 + `InternetSetOption` 是等价实现，且需保证**退出时恢复**（AppController 关闭流程调用 `disable()`，对应原 `disableSysProxySync`）。「仅当前活动设备」语义（`onlyActiveDevice`）在 Win 端对应写每连接/每用户，P2 细化。

### 14.4 macOS 实现（`system_proxy_mac.mm`，SystemConfiguration）

对应需求：「macOS：通过 SystemConfiguration 框架（CFNetwork）」。

```objective-c++
// 使用 SCPreferences + SCDynamicStore，对「网络服务」（Networksetup 等价）写代理
void SystemProxyMac::setManualProxy(id host, unsigned short port, array bypass) {
  // 方案 A（首选）：SCPreferencesCreate + SCPreferencesPathSetValue
  //   路径：/NetworkServices/<ServiceID>/Proxies
  //   HTTPEnable=1, HTTPProxy=host, HTTPPort=port, HTTPSEnable=1,
  //   HTTPSProxy=host, HTTPSPort=port, ProxyAutoConfigEnable=0,
  //   ExceptionsList=bypass(CFArray)
  //   SCPreferencesCommitChanges + SCPreferencesApplyChanges
  // 方案 B（等价）：调用 `networksetup -setwebproxy/-setsecurewebproxy/-setproxybypassdomains`
  //   与原 sysproxy-go 行为一致；但需先 getDefaultService()
}
```

> P1 可先用 `networksetup` 子进程（与原 `core/network.ts` 的 `getDefaultService` 逻辑一致，`route -n get default` → interface → service），P2 再切纯 SystemConfiguration API 以避免命令注入/权限提示。`auto` 模式写 `ProxyAutoConfigEnable=1` + `ProxyAutoConfigURL=pac`。

### 14.5 Linux 实现（`system_proxy_linux.cpp`，gsettings + 环境变量）

对应需求：「Linux：通过 gsettings（GNOME）或环境变量」。

```cpp
void SystemProxyLinux::setManualProxy(host, port, bypass) {
#ifdef Q_OS_LINUX
  // gsettings（GNOME）：
  //   gsettings set org.gnome.system.proxy mode 'manual'
  //   gsettings set org.gnome.system.proxy.http host '127.0.0.1'
  //   gsettings set org.gnome.system.proxy.http port 7890
  //   gsettings set org.gnome.system.proxy.http enabled true
  //   gsettings set org.gnome.system.proxy ignore-hosts "['localhost','127.0.0.1/8',...]"
#else
  // 环境变量 fallback（export http_proxy/https_proxy/all_proxy/no_proxy）
#endif
}
```

环境变量方案注意：**只能影响当前进程及其子进程**，不能改 GNOME 全局；因此 v1 以 gsettings 为主、`NO_GNOME` 时降级环境变量（并文档化其局限）。bypass 用 `QStringList`。

---

## 15. 应用编排 AppController 与入口 main.cpp

### 15.1 main.cpp（入口 + 生命周期 + 单实例 + 全局异常）

`src/app/main.cpp`：

```cpp
#include <QApplication>
#include <QMessageBox>
#include <QSharedMemory>
#include <memory>
#include <spdlog/spdlog.h>

#include "app_controller.h"
#include "log_manager.h"
#include "models.h"
#include "paths.h"

// 全局未捕获异常钩子（对应需求 1）
static void installGlobalExceptionHandlers(sparkle::core::LogManager& log);

namespace {
std::unique_ptr<sparkle::core::AppController> g_controller; // RAII 持有，析构时优雅关闭
}

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("sparkle");
  QApplication::setOrganizationName("sparkle");
  QApplication::setApplicationVersion("1.26.7");

  // 1) 单实例检测（QSharedMemory + QLocalServer 转发 deep link）
  sparkle::core::SingleInstance single;
  if (!single.tryAcquire()) {
    single.forwardToPrimary(argc, argv);
    return 0;                       // 已存在实例
  }

  // 2) 初始化路径与日志（先于一切）
  sparkle::core::Paths::initialize(app.applicationFilePath().toStdString(),
                                   sparkle::core::Paths::isPortable());
  auto log = std::make_unique<sparkle::core::LogManager>();
  installGlobalExceptionHandlers(*log);   // qInstallMessageHandler / std::set_terminate / SEH

  // 3) 组合根：编排整个应用
  g_controller = std::make_unique<sparkle::core::AppController>(std::move(log), &app);

  // 4) 启动（异步不阻塞：窗口显示后返回）
  g_controller->startup();

  return app.exec();                 // 进入事件循环
}
```

`installGlobalExceptionHandlers` 要点：
- `qInstallMessageHandler` 把 `qDebug/qWarning/qCritical` 汇入 spdlog；
- `std::set_terminate` + `std::set_unexpected`（C++17 后无 unexpected，用 terminate）落日志后 `_Exit(1)`；
- Windows：`SetUnhandledExceptionFilter` + `_set_purecall_handler`；
- POSIX：`signal(SIGSEGV/SIGABRT/SIGFPE/SIGILL, handler)` 写日志后恢复默认信号并重新 `raise`（尽力而为）。

### 15.2 SingleInstance（`core/single_instance.h`）

`QSharedMemory`（需求 1 指定）做锁；`QLocalServer` 接收次实例转发（deep link URL），通知主实例。

### 15.3 AppController（`core/app_controller.h`）

```cpp
#pragma once

#include <QObject>
#include <memory>

namespace sparkle::ui { class MainWindow; }
namespace sparkle::core {

// 组合根：拥有所有 manager，串联信号，驱动启动/关闭序列。
class AppController final : public QObject {
  Q_OBJECT
public:
  AppController(std::unique_ptr<LogManager> log, QObject* parent = nullptr);
  ~AppController() override;

  void startup();       // 对应启动任务：窗口 → 内核 → 监控/托盘/快捷键
  void shutdown();      // 对应退出链：恢复 DNS → 停代理 → 停内核 → 隐藏托盘

private:
  void wireSignals();   // 信号槽集中连接

  std::unique_ptr<LogManager> log_;
  std::unique_ptr<ConfigManager> config_;
  std::unique_ptr<RuntimeConfigFactory> factory_;
  std::unique_ptr<MihomoApiClient> api_;
  std::unique_ptr<CoreManager> core_;
  std::unique_ptr<SystemProxyManager> sysProxy_;
  // PacServer, platform 各模块（P2/P3）
  std::unique_ptr<ui::MainWindow> window_;
};

}  // namespace sparkle::core
```

**关闭链**（对应原 `session-end` 与 `appLifecycle`）：`recoverDNS → disableSysProxy → stopCore → (服务模式 stop service) → removePidFile`，期间 `QApplication::setQuitOnLastWindowClosed(false)` 以支持「关闭窗口 ≠ 退出」（托盘常驻）。

---

## 16. 用户界面 ui

### 16.1 MainWindow（`ui/main_window.h`）

```cpp
#pragma once

#include <QMainWindow>
#include <memory>

namespace sparkle::ui {

class Sidebar;

class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // 对应原窗口生命周期
  void showWindow();      // 恢复最小化/置顶/聚焦
  void closeToTray();     // 关闭时隐藏而非退出（受 lightweight/tray 配置影响）

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  Sidebar* sidebar_;                 // 侧边导航
  QStackedWidget* stack_;            // 页面容器
  // 快捷窗口浮层、无边框拖动（useWindowFrame/enableWindowDrag）P2
};

}  // namespace sparkle::ui
```

侧边导航（需求 5）：`概况(可选)、代理、规则、日志、设置` 为 MVP，其余 9 节按 P2/P3 逐步加入（见 §2.3）。代理为默认页（原工程默认 `/proxies`）。

### 16.2 页面基类与页面

```cpp
// page_base.h
#pragma once
#include <QWidget>
namespace sparkle::ui {
// 页面基类：统一标题/刷新/状态栏接口
class PageBase : public QWidget {
  Q_OBJECT
public:
  explicit PageBase(QWidget* parent = nullptr) : QWidget(parent) {}
  virtual void refresh() = 0;     // 内核重启/信号后刷新
};
}
```

- `ProxiesPage`：`ProxyListModel`（组/节点树），切换组选择（`changeProxy`/`unfixProxy`）、延迟测试（`testProxyDelay`/`testGroupDelay`）、显示当前选态与延迟色阶。
- `RulesPage`：规则表 + 禁用开关（`disableRules`）。
- `LogsPage`：`LogListModel` 环形视图 + 级别过滤（对应需求 5「带级别过滤」）+ 实时追加 + 暂停/清空。
- `SettingsPage`：各项 `AppConfig` 表单（主题、日志、网络检测、代理、快捷键……按 §18）。
- `MihomoPage`：内核版本/升级、TUN/模式、`mixed-port`、DNS/Sniffer 开关、`log-level`。
- `SysProxyPage`：代理开关、auto/manual、host/port/bypass、guard。

### 16.3 数据模型适配（QAbstractListModel）

- `LogListModel`：环形缓冲视图，`data()` 提供 `DisplayRole/ForegroundRole`（按级别着色）。
- `ProxyListModel`/`ProxyTreeModel`：`QStandardItemModel` 或自定义树。
- `TrafficModel`：单行 `TrafficStats`，配 `QTimer` 驱动图表（可选 `QtCharts`，P2）。

### 16.4 主题（QSS）

`ThemeManager`：加载 `resources/themes/*.qss`，`app.setStyleSheet()`；`system/light/dark` 由 `QPalette`/`QStyleHints::colorScheme` 判定；自定义主题从 `dataDir/themes` 读取（原 `themesDir`）。深浅切换即时生效（需求 5）。

---

## 17. 构建系统 CMake + vcpkg

### 17.1 根 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(sparkle VERSION 1.26.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt 元对象/资源/ui 自动处理
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/ui)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
```

### 17.2 子目录示例（`src/core/CMakeLists.txt`）

```cmake
add_library(sparkle_core STATIC
  models.cpp
  paths.cpp
  log_manager.cpp
  config_manager.cpp
  runtime_config_factory.cpp
  core_process_controller.cpp
  http_client.cpp
  ws_client.cpp
  mihomo_api_client.cpp
  core_manager.cpp
  system_proxy_manager.cpp
  pac_server.cpp
  single_instance.cpp
  app_controller.cpp
)

target_include_directories(sparkle_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sparkle_core PUBLIC
  Qt6::Core Qt6::Network
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  yaml-cpp::yaml-cpp
  sparkle_platform
)

# 编译选项（质量要求：告警即错误）
target_compile_options(sparkle_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wconversion -Wshadow>
)
```

`src/platform/CMakeLists.txt` 按平台选择源文件：

```cmake
if(WIN32)
  set(PLATFORM_SRC system_proxy_win.cpp)
elseif(APPLE)
  set(PLATFORM_SRC system_proxy_mac.mm)
  enable_language(OBJCXX)
else()
  set(PLATFORM_SRC system_proxy_linux.cpp)
endif()

add_library(sparkle_platform STATIC system_proxy.cpp dns_setter.cpp
  network_detector.cpp tray_icon.cpp shortcut_manager.cpp ${PLATFORM_SRC})
target_link_libraries(sparkle_platform PUBLIC Qt6::Core Qt6::Network Qt6::Widgets)
```

`src/ui/CMakeLists.txt`、`src/app/CMakeLists.txt` 类似（`app` 生成 `sparkle` 可执行目标，`WIN32`/`MACOSX_BUNDLE`）。

### 17.3 `vcpkg.json`

```json
{
  "name": "sparkle",
  "version-string": "1.26.7",
  "description": "Sparkle - native Qt6 Mihomo GUI",
  "homepage": "https://github.com/xishang0128/sparkle",
  "license": "GPL-3.0-only",
  "dependencies": [
    "nlohmann-json",
    "spdlog",
    "yaml-cpp",
    {
      "name": "gtest",
      "default-features": false
    }
  ]
}
```

> Qt 依赖策略：**Qt 6 建议走系统包管理器**（apt `qt6-base-dev` / homebrew `qt` / 官方在线安装器），**不放进 vcpkg**（vcpkg 编译 Qt 极慢且与系统 SDK 易冲突）。spdlog/nlohmann/yaml-cpp/gtest 走 vcpkg manifest。若团队坚持全 vcpkg，可加 `qtbase`/`qtdeclarative`，但需在 `CMakePresets.json` 指定 `CMAKE_TOOLCHAIN_FILE` 与 triplet。

`CMakePresets.json`（示例片段）：

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "default",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
        "VCPKG_TARGET_TRIPLET": "$env{VCPKG_DEFAULT_TRIPLET}"
      }
    }
  ]
}
```

### 17.4 `resources/resources.qrc`

```xml
<RCC>
  <qresource prefix="/">
    <file>icons/app.png</file>
    <file>icons/tray.png</file>
    <file>themes/dark.qss</file>
    <file>themes/light.qss</file>
  </qresource>
</RCC>
```

---

## 18. 配置文件 Schema（YAML）

`paths.appConfigPath()` = `config.yaml`（原 `appConfigPath`），字段与 `src/shared/types/app.d.ts` 一一对应，此处列全（实施时 `AppConfig` 结构体按此展开，用 `std::optional` 表达可选）：

```yaml
core: mihomo                 # mihomo | mihomo-alpha | system
systemCorePath: ""           # core==system
corePermissionMode: elevated # elevated | service
coreStartupMode: log         # post-up | log
disableLoopbackDetector: false
disableEmbedCA: false
disableSystemCA: false
disableNftables: false
safePaths: []
mihomoCpuPriority: PRIORITY_NORMAL
saveLogs: true
maxLogDays: 7
maxLogFileSizeMB: 20
maxLogEntries: 2000
realtimeLogLevel: info       # silent|error|warning|info|debug

sysProxy:
  enable: false
  host: 127.0.0.1
  mode: manual               # manual | auto
  bypass: []
  settingMode: exec          # exec | service
  guard: false
  guardNotify: false
onlyActiveDevice: false

# —— UI ——
appTheme: system             # system|light|dark
customTheme: ''
siderOrder: []               # 侧边栏排序
siderWidth: 0
useWindowFrame: false
enableWindowDrag: false
silentStart: false
disableTray: false
showFloatingWindow: false
spinFloatingIcon: false
disableAnimation: false
disableGPU: false
displayIcon: true
displayAppName: true
useDockIcon: true
customTrayIcon: ''
useCustomTrayMenu: false
proxyInTray: false
trayProxyDelayLayout: same-line
showGroupSelectedProxy: false
showProxyDetailTooltip: false
proxyDisplayOrder: default   # default|delay|name
proxyDisplayLayout: hidden
groupDisplayLayout: hidden
proxyCols: auto
# ……其余卡片布局/连接排序/延迟测试/网络检测/快捷键/Gist/SubStore/WebDAV/自动更新 字段
# 见 src/shared/types/app.d.ts 全量。
```

其他三个文件：`mihomo.yaml`（受控内核配置，节选 `tun/dns/sniffer/mode/mixed-port/log-level` 等）、`profile.yaml`（`{current, items[]}`）、`override.yaml`（`{items[]}`）。

**配置变更通知**：`ConfigManager` 四个 `*Changed` 信号对应原 `appConfigUpdated / controledMihomoConfigUpdated / profileConfigUpdated / overrideConfigUpdated`。

---

## 19. 数据流与事件总线

以 `AppController` 为总线中心，信号单向流：

```text
QProcess stdout ─▶ LogManager::appendRawCoreChunk ─▶ LogEntry ─▶ LogListModel/日志页
Mihomo WS /traffic ─▶ MihomoApiClient::trafficUpdated ─▶ TrafficModel/托盘/悬浮窗
Mihomo WS /logs   ─▶ LogManager::publishMihomoLog ─▶ LogEntry（不节流）
Mihomo REST       ─▶ 各 fetch* 完成回调 ─▶ 对应页 Model
ConfigManager     ─▶ *Changed ─▶ 页面刷新 + RuntimeConfigFactory::generate → CoreManager::restart
CoreManager       ─▶ stateChanged/coreStarted/coreStopped ─▶ UI 启动按钮态与托盘
SystemProxyManager─▶ proxyStateChanged ─▶ SysProxyPage
```

跨线程队列连接（如后续把某 manager `moveToThread`）统一用信号槽；数据用值拷贝（`LogEntry` 等已 `Q_DECLARE_METATYPE`）。

---

## 20. 错误处理与异常安全

- **RAII**：`QObject` 父子所有权、`std::unique_ptr`（pimpl、QProcess 等非 QObject 资源、平台句柄注册表 `HKEY`/`SCDynamicStore`/`gsettings` 用 RAII 包裹器）；禁止裸 `new/delete`。
- **五法则**：任何自定义析构的类同时显式声明或 `=default` 拷贝/移动（多数业务类直接 `Q_DISABLE_COPY`，因 `QObject` 不可拷贝）。
- **`#pragma once`**、前置声明（`class ConfigManager;` 减少编译依赖）。
- **异常安全**：所有跨层边界（`YamlStore::get/set`、`CoreManager::startup`、平台代理设置）用 try-catch，失败后 `LogManager::appendAppLog` + 用户通知，异常不得穿越 Qt 槽边界（槽内捕获）。
- **新式信号槽**：函数指针连接（`connect(a, &A::sig, b, &B::slot)`）。
- **拷贝规避**：函数参数大对象用 `const&`，返回值依赖 NRVO/`std::move`；热路径（日志广播、流量节流）用 `QByteArray`/`std::move`。

---

## 21. 单元测试策略

建议 **Qt Test**（核心 Qt 类型），纯逻辑可 GoogleTest。测试隔离 IO：`YamlStore` 用临时目录注入路径（对应原 `cached-yaml-store.test.ts` 的 `mock fs` 手法）。

| 测试 | 覆盖 |
|---|---|
| `tst_config_manager` | YamlStore 懒加载/强刷/缺省初始化/ENOENT 保留/写后缓存/非 ENOENT 抛错 |
| `tst_core_process_controller` | SIGINT→TERM→KILL 升级、行缓冲、pid 文件 |
| `tst_http_client` | 对本地 `QTcpServer` 假 Mihomo 发 GET/PATCH，断言路径/body/状态 |
| `tst_ws_client` | 重连预算、成功消息恢复预算、stop/restart 语义、单一活动 socket |
| `tst_runtime_config_factory` | deepMerge、clean* 规则、UI 字段回填、受控回写 |
| `tst_latest_sender` | 首尾节流、clear 清待发 |
| `tst_system_proxy` | 平台后端接口的契约测试（注入假后端） |

---

## 22. 实施计划与里程碑

| 阶段 | 内容 | 可验收产出 |
|---|---|---|
| **P0 骨架** | §5 目录、根+子 CMake、vcpkg.json、qrc、main.cpp、空 `AppController`/`MainWindow`（能起窗口、退出清理） | `cmake -B build && cmake --build build` 成功，可运行 |
| **P1 核心 MVP** | Paths、LogManager、ConfigManager/YamlStore、RuntimeConfigFactory、CoreProcessController、HttpClient/WsClient、MihomoApiClient、CoreManager、SystemProxyManager + 三平台代理、MainWindow + 5 页面（代理/规则/日志/设置/内核） | 能启动/停止/重启内核、切模式、改配置重载、设置/清除系统代理、看流量与日志 |
| **P2 完整性** | 连接/TUN/DNS/Sniffer/资源/订阅/覆写页面、托盘/悬浮窗/快捷键、网络检测、macOS DNS、TrafficMonitor(Windows)、多主题 | 与原 UX 基本一致 |
| **P3 高级** | 服务模式（elevated/service）、Sub-Store、Gist/WebDAV 备份、自动更新、age 加密、JS 覆写脚本（QuickJS）、deep link、轻量模式 | 全功能对齐 |

**留空占位（后续填充）**：本骨架中以下函数只给接口与注释占位——

- `platform::SystemProxyMac` 的 SCDynamicStore 版本（P1 先用 `networksetup` 子进程）；
- `CoreManager` 的 service 分支（P3）；
- `RuntimeConfigFactory::overrideProfile` 对 JS 脚本的求值（P3）；
- `PacServer` 的 PAC 脚本生成器默认模板 `TODO`；
- 订阅下载/更新/Gist/SubStore 网络爬取函数。

---

## 23. 风险与开放问题

1. **WebSocket/HTTP 自研**（§12）工作量最大，需 RFC6455 + HTTP chunked；务必先写 `tst_ws_client` 驱动。备选 `libcurl`（HTTP）与 `libwebsockets`/`boost.beast`（WS）作为兜底。
2. **YAML 保真**：yaml-cpp 丢注释；profile/override 保真见 §9.4。
3. **命名管道与 Unix socket 抽象**：`QLocalSocket` 两端语义已对齐，但 Windows 上 Mihomo 的 `-ext-ctl-pipe` serverName 格式需实测（`\\.\pipe\Sparkle\mihomo` vs 无前缀）。
4. **系统代理自动恢复（guard）**：原依赖 `sparkle-service` 的守护；原生实现需各平台「代理被外部修改」监听（Win: `RegNotifyChangeKeyValue`；mac: SCDynamicStore 回调；Linux: dconf watch），P2。
5. **服务模式 / 提权**：原 `corePermissionMode=service` 走独立服务进程与签名 HTTP。原生实现需一套跨平台服务（Windows Service / launchd / systemd）+ IPC，面大，P3，**不建议**在 P1 强行复刻——P1 仅实现 `elevated`（直接 `QProcess`）。
6. **更新/自下载内核**：原内置 stable/alpha 内核并自动更新（`mihomoUpgrade`/updater）。P1 仅「使用本地 sidecar 或 `SPARKLE_MIHOMO_PATH`」，自更新 P3。
7. **JS 覆写脚本**：原覆写支持 JS（`vm.runInContext`）。C++ 需 QuickJS/Duktape；否则 P1 仅支持 **yaml 覆写**，JS 覆写 P3。需在文档/设置页明示功能降级。
8. **`age` 加密 / Sub-Store / Gist / WebDAV / Tailscale 通知**：均为附加生态，P3，需第三方库或自实现。
9. **Qt 主题与 HeroUI/Tailwind 观感差异**：视觉非 1:1；用 QSS 尽量贴近，接受差异。
10. **平台构建矩阵**：三平台 CI 需覆盖；macOS `.mm` 需 `OBJCXX` 语言开启；Windows 需 `WIN32` 可执行与图标/版本资源。

---

## 24. 附录 A：IPC 能力清单 → C++ 方法映射

原 `IPC_CHANNELS`（完整 150+ 通道见 `src/shared/ipc.ts`）与 C++ 方法对应关系（供实施拆解用）：

| 原 channel（节选） | C++ 方法 |
|---|---|
| `getAppConfig` / `patchAppConfig` / `resetAppConfig` | `ConfigManager::appConfig` / `patchAppConfig` / `resetAppConfig` |
| `getControledMihomoConfig` / `patchControledMihomoConfig` | `ConfigManager::controlledMihomoConfig` / `patchControledMihomoConfig` |
| `getProfileConfig` / `setProfileConfig` / `getCurrentProfileItem` | `ConfigManager::profileConfig` / `setProfileConfig` / `currentProfileItem` |
| `getProfileStr` / `setProfileStr` / `getRawProfileStr` | `ConfigManager::profileRawText` / `setProfileRawText` + `RuntimeConfigFactory::rawProfileStr` |
| `changeCurrentProfile` / `addProfileItem` / `removeProfileItem` | `ConfigManager::changeCurrentProfile` / `addProfileItem` / `removeProfileItem` |
| `getOverrideConfig` / `setOverrideConfig` / `addOverrideItem` / `removeOverrideItem` | `ConfigManager::overrideConfig` / `setOverrideConfig` / `addOverrideItem` / `removeOverrideItem` |
| `getRuntimeConfig` / `getRuntimeConfigStr` | `RuntimeConfigFactory::runtimeConfig` / `runtimeConfigStr` |
| `startCore`(`restartCore`) / `stopCore` / `quitWithoutCore` | `CoreManager::startup` / `shutdown` / `AppController::quitKeepingCore` |
| `mihomoVersion` / `mihomoConfig` / `patchMihomoConfig` | `MihomoApiClient::fetchVersion` / `fetchConfigs` / `patchConfigs` |
| `mihomoGroups` / `mihomoProxies` | `MihomoApiClient::fetchGroups` / `fetchProxies` |
| `mihomoChangeProxy` / `mihomoUnfixedProxy` | `MihomoApiClient::changeProxy` / `unfixProxy` |
| `mihomoProxyDelay` / `mihomoGroupDelay` | `MihomoApiClient::testProxyDelay` / `testGroupDelay` |
| `mihomoRules` / `mihomoRulesDisable` | `MihomoApiClient::fetchRules` / `disableRules` |
| `mihomoCloseConnection` / `mihomoCloseConnections` | `MihomoApiClient::closeConnection` / `closeConnections` |
| `mihomoUpgrade` / `mihomoUpgradeGeo` / `mihomoUpgradeUI` | `MihomoApiClient::upgrade` / `upgradeGeo` / `upgradeUi` |
| `mihomoProxyProviders` / `mihomoUpdateProxyProviders` | `MihomoApiClient::fetchProxyProviders` / `updateProxyProvider` |
| `mihomoRuleProviders` / `mihomoUpdateRuleProviders` | `MihomoApiClient::fetchRuleProviders` / `updateRuleProvider` |
| `getCachedMihomoLogs` / `clearCachedMihomoLogs` | `LogManager::cachedMihomoLogs` / `clearCachedMihomoLogs` |
| `getInterfaces` / `platform` / `getVersion` / `getAppName` | `platform::NetworkDetector::interfaces` / 编译期 / `QApplication::applicationVersion` / `applicationName` |
| `getFilePath` / `readTextFile` / `openFile` / `copyEnv` | `QFileDialog` / `QFile::readAll` / `QDesktopServices::openUrl` / `Clipboard` |
| `registerShortcut` / `enableAutoRun` / `disableAutoRun` / `checkAutoRun` | `platform::ShortcutManager` / `platform::AutoStart` |
| `downloadSubStore` / `initService` / `installService` / `restartService` | P3 对应模块 |
| `applyTheme` / `fetchThemes` / `importThemes` / `resolveThemes` | `ui::ThemeManager` |

原 `IPC_EVENTS`（24 个推送事件）→ 对应 C++ 信号：

| 原事件 | C++ 信号 |
|---|---|
| `appConfigUpdated` / `controledMihomoConfigUpdated` / `profileConfigUpdated` / `overrideConfigUpdated` | `ConfigManager::*Changed` |
| `groupsUpdated` / `rulesUpdated` | `CoreManager::coreStarted`（启动完成后置刷新）或 `MihomoApiClient` 拉取完成回调 |
| `core-started` / `core-stopped` / `core-status-changed` | `CoreManager::coreStarted` / `coreStopped` / `stateChanged` |
| `mihomoTraffic` / `mihomoMemory` / `mihomoConnections` / `mihomoLogs` | `MihomoApiClient::trafficUpdated` / `memoryUpdated` / `connectionsUpdated` / `LogManager::mihomoLog` |
| `app-notification` / `app-notification-dismiss` | `platform::NotificationService`（P2） |
| `trayIconUpdate` / `updateTrayMenu` / `updateFloatingWindow` | `platform::TrayIcon` / `ui::FloatingWindow`（P2） |

---

## 25. 附录 B：关键实现难点备忘

1. **`QLocalSocket` 双端语义**：Windows serverName 即命名管道名，Unix 即 socket 文件路径。Mihomo 侧 `-ext-ctl-pipe`（Win）/`-ext-ctl-unix`（Unix）传入的字符串需与 `QLocalSocket::connectToServer` 的 serverName 精确一致。Unix 下注意 `/tmp/sparkle-mihomo-api.sock` 与「无权限回退 `-noperm`」两种路径（原 `dirs.ts::mihomoIpcPath`）。

2. **HTTP over QLocalSocket**：Mihomo external controller 是 plain HTTP/1.1，无 `keep-alive` 强需求，可实现「每请求建立/复用连接、读 `Content-Length`(或 chunked) 直至结束」。超时 15s（原 axios 超时）。响应体为 JSON，用 nlohmann 解析。

3. **WebSocket over QLocalSocket**：握手 `GET <path> HTTP/1.1` + `Sec-WebSocket-Key`，`101 Switching Protocols` 后进入帧循环；仅处理 `opcode=0x1(text)`，`0x8(close)`/`0x9(ping)→0xA(pong)` 按 RFC 处理。URL 形如 `ws+unix:/tmp/sparkle-mihomo-api.sock:/traffic` 需解析成 (socket, path) 两元组。

4. **service 模式（P3）**：REST 走 `http://localhost/core/controller`（TCP）+ 签名头（原 `getServiceAuthHeaders`）；数据流走 service 的 unix socket 并加同样 header。需引入「请求签名」能力（密钥 `serviceAuthKey`）、流式事件订阅（原 `service/api.ts` 的 event stream）。这是 P3 的核心复杂性。

5. **崩溃自动重启与就绪判定的竞态**：`onProcessFinished` 与「控制器就绪」是两个异步路径，需用状态标志避免「进程已退出仍探活」/「就绪回调后进程又退」。建议 CoreManager 内维护一个 generation 计数器（类似原 `networkDetectionGeneration`），每次 start 递增，过期回调丢弃。

6. **代理串行化与代际**：系统代理开/关有「执行中又触发」与「断网 5s 重试」两条异步路径，须用请求代际号丢弃过期任务（原 `triggerSysProxyRequest`）。

7. **平台 source 选择**：macOS 用 `.mm` 且需在 `CMakeLists` 中 `enable_language(OBJCXX)`；平台专属源仅加入对应平台的 `add_library`（见 §17.2）。

8. **`QSharedMemory` 单实例的极端 case**：正常退出必须 `detach()`；崩溃残留段时需「附着失败 → 视为无实例」处理，避免永远无法启动。

---

*（文档完）*