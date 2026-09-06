# sparkle-cpp

Sparkle 的 C++20 / Qt 6 原生重写（从 Electron + React + TS 迁移）。本目录是**可编译的核心骨架**，不依赖、也不改动仓库根目录的 Electron 源码。

设计文档见 `../docs/CPP_PORT_DESIGN.md`。

## 目录结构

```
cpp/
├── CMakeLists.txt           根构建
├── CMakePresets.json        预设（vcpkg / system-deps）
├── vcpkg.json               依赖清单（manifest 模式）
├── cmake/                   公共 CMake 函数
├── src/
│   ├── app/                 main.cpp 入口
│   ├── core/                应用逻辑（core 命名空间）
│   ├── platform/            平台隔离（platform 命名空间）
│   └── ui/                  Qt Widgets 界面（ui 命名空间）
├── resources/               .qrc + 主题 qss
└── tests/                   Qt Test 单元测试
```

## 构建

### 方式一：vcpkg（manifest 模式，推荐用于交付）

```bash
# 需要已安装 vcpkg 并设置 VCPKG_ROOT；Qt 走系统安装（设置 QTDIR）
cmake --preset vcpkg
cmake --build --preset vcpkg
```

### 方式二：系统依赖（本地验证用，不走 vcpkg）

```bash
# Debian/Ubuntu: sudo apt install qt6-base-dev libspdlog-dev libyaml-cpp-dev nlohmann-json3-dev
# macOS:         brew install qt spdlog yaml-cpp nlohmann-json
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="$QTDIR;$HOMEBREW_PREFIX" \
  -DSPARKLE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### 方式三：无包管理器时自动拉取（FetchContent）

三个 CMake 包（`nlohmann_json` / `spdlog` / `yaml-cpp`）缺失时可用 `-DSPARKLE_FETCH_DEPS=ON` 从 GitHub
（codeload，URL + 固定 SHA256）拉取。**QuickJS**（用于 JS 覆写执行）没有标准 `find_package` 配置，
本地验证同样经 `-DSPARKLE_FETCH_DEPS=ON` 从源码编译（pin 到 bellard/quickjs commit `04be246`；
正式构建可改用 vcpkg 的 `quickjs` 端口），Qt 仍需系统安装（`CMAKE_PREFIX_PATH` 指向 Qt 前缀）：

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x/macos" \
  -DSPARKLE_FETCH_DEPS=ON -DSPARKLE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

> 已在 macOS（AppleClang 21 / Qt 6.10.1）以方式三验证：配置 + 编译 + 4 个测试全部通过，
> `sparkle` 可加载主窗口（offscreen 冒烟无崩溃）。

## 实现状态（骨架）

已实现（可编译、可运行基础窗口）：数据模型、路径管理、日志管理、通用 YAML 存储、配置管理（**含 service 模式 controller 凭据持久化**）、运行时配置生成（**含 service 模式写 external-controller + secret**）、内核进程控制器、内核管理器（**含 service 模式 detached + pid 文件 + 重连 + Bearer 控制器 + 就绪探测**）、HTTP/1.1 + RFC6455 WebSocket 客户端（**SocketTransport 抽象：直连 QLocalSocket / service QTcpSocket + Authorization Bearer**）、JS 覆写（**QuickJS 引擎 + Promise + 真网络 fetch**）、**ScriptEngine RAII 门面（全局函数/类注册、CoreManager 双向绑定、事件回调、异常转换）**、系统代理（三平台，macOS 先用 networksetup 子进程）、单实例、主窗口 + 侧边栏 + 5 个页面、深/浅主题。

脚本示例：仓库根目录 `scripts/example.js`，由应用启动时自动加载；脚本异常会记录到应用日志，不会阻止主窗口启动。

留空占位（`// TODO(phase N)` 标注，按设计文档第 22 节分阶段补齐；**phase 1 / phase 3 骨架项已全部完成**，剩 P2 业务收尾）：

- `MihomoApiClient` 的 proxies/group 节点 REST 端点与延迟历史、节点增删改；
- 系统代理 guard 自动恢复、`onlyActiveDevice` 语义；
- 托盘/悬浮窗/快捷键/网络检测/DNS、Sub-Store/Gist/更新；
- Windows `SetConsoleCtrlEvent(CTRL_BREAK)` 精细化（当前用 QProcess 通用 terminate）；
- 与真实 mihomo 二进制的端到端集成测试（需 `SPARKLE_MIHOMO_PATH` 指向真实二进制）；
- `fetch` 在长请求场景下阻塞 UI（若脚本频繁 fetch，可考虑把 `generate()` 移到工作线程）。

详细交接：`docs/CORE_MANAGER_HANDOFF.md`。

## 命名空间

- `sparkle::core` — 数据模型、内核/配置/系统代理门面、编排。
- `sparkle::ui` — Qt Widgets 界面。
- `sparkle::platform` — 平台隔离实现。

## 与设计文档的偏差说明

- 路径统一用 `QString`（`QStandardPaths`/`QDir`/`QLocalSocket` 更契合），不再用 `std::filesystem::path`。
- 数据模型的字符串/时间字段统一用 `QString`/`qint64`（更契合 Qt 模型与信号槽）；`Timestamps` 用 epoch 毫秒。
- 受控配置/应用配置在骨架阶段以 `nlohmann::json` 承载（完整类型化 `AppConfig` 结构体在后继提交补齐）。