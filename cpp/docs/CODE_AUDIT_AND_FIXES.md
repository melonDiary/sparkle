# C++ 代码审计与修复记录

本文档记录对 Sparkle C++ 端口（`cpp/`）做的安全与集成层审计结论，
以及本次已经落盘的修补/强化项。审计重点放在 内存安全、多线程/JS 交互、
生命周期顺序、边缘分支 和 跨平台兼容性 上。

---

## 1. 文档目的和范围

- **目的**：把架构级缺陷、资源泄漏风险、功能盲区、性能陷阱、跨平台隐患
  以及 CMake/依赖完整性问题固定成可追溯的记录，并同步映射到已完成的代码修改。
- **范围**：主要读了以下模块：
  - `cpp/src/app/main.cpp`
  - `cpp/src/app/app_controller.{h,cpp}`
  - `cpp/src/core/{script_engine,config_manager,system_proxy_manager,MITMManager,core_manager,PluginSandbox,PluginManager,json_yaml,runtime_config_factory,js_engine,log_manager,pac_server,single_instance,mihomo_api_client,http_client,ws_client,socket_transport,core_process_controller,models,paths,yaml_store,IPlugin,ProxyNode,config_manager.h}`
  - `cpp/src/platform/{system_proxy.h,SystemProxyManager_{win,mac,linux}}`
  - `cpp/src/ui/{app_model,script_bridge}`
  - `cpp/{CMakeLists.txt,cmake/Dependencies.cmake,src/core/CMakeLists.txt,src/platform/CMakeLists.txt,src/app/CMakeLists.txt}`
- **不在本次 scope 内**：界面细节稿设计、未集成的新型平台代理驱动、业务功能范围外的新特性开发。

---

## 2. 审计结论速览

### 2.1 🔴 P0（阻断性 / 必崩溃风险）

1. **`main.cpp` 局部变量析构顺序导致悬挂指针 + 回调竞态**  
   `ScriptEngine` 绑定了 `CoreManager*`，而 `controller` 比 `scriptEngine` 先析构。
2. **`ScriptEngine` 回调路径缺少线程/上下文守护**  
   `CoreManager::logReceived` 链最终调用 JS 回调，若在非 ScriptEngine 线程上执行 JS 则违反 QuickJS 单线程模型。
3. **`CoreManager::coreCrashed(-1)` 同步触发、JS 回调异常不中止同事件链**  
   启动失败窗口里脚本回调可被连续触发，且其中之一抛 JS 异常也不会阻断后续回调。
4. **`MITMManager::ClientConnection` 延时清理访问 raw manager 指针**  
   `close()` 里的 `QTimer::singleShot(0, manager_, ...)` 在 manager 先析构的边界情况下可能访问已销毁对象。
5. **Windows detached spawn 临时改全局环境变量**  
   `spawnDetached()` Windows 分支直接 `qputenv/qunsetenv`，多线程/多实例路径下有串扰风险。

### 2.2 🟡 P1（性能/泄漏/稳定性隐患）

1. **QuickJS 资源释放依赖人工分支，易遗漏**（`ScriptEngine` / `PluginSandbox` / `js_engine` 中一次性脚本执行路径）。
2. **MITM body 始终载入内存，无流式转发**（大文件场景只能拒绝或缩限）。
3. **系统代理改一半失败时缺少回滚快照**（macOS `networksetup` / Linux `gsettings`）。
4. **`serviceControllerEndpoint()` 自动重选端口后未协调重连语义**。
5. **`PluginSandbox::jsHttpGet()` 同步阻塞 + 嵌套事件循环**，容易卡主线程或引发线程模型混用。

### 2.3 🔵 P2（代码异味/可维护性）

1. `cpp/src/core/config_manager.h` 是纯转手文件，没必要。
2. `defaultBypass()` Windows 分支手写了大量 `172.16.*~172.31.*` 重复项。
3. `MITMManager::onClientReadyRead` 存在重复解析 header 的片段。
4. `runtime_config_factory.cpp` 的 `cleanTunConfig()` 里混入了 `#if defined(Q_OS_MACOS)`。
5. 各平台 `setGuardEnabled` 当前大多为空操作，未显式记录“未实现/不可用”。

### 2.4 ✅ 确认无误项（本次未动的稳固逻辑）

- `CoreProcessController` RAII + 代际 generation 防误杀。
- `CoreManager` detached 停止链：`detachedStopPid_`、`suppressDetachedStoppedSignal_`、重启前重置。
- QuickJS `JS_SetMemoryLimit` / `SetMaxStackSize` / `SetInterruptHandler` 基础防线。
- `json_yaml.h` 中 `loadYaml` / `parseYamlStr` 的 try-catch 包装。
- `SingleInstance` 的 QLocalServer 存活探测 + 残留移除。
- `AppController::shutdown()` 的关闭顺序（mitm -> sysproxy -> core -> window）。

---

## 3. 已修复问题清单（本次落盘）

### 3.1 `cpp/src/app/main.cpp` — 析构顺序导致绑定对象悬挂

- **问题**  
  `auto controller = std::make_unique<...>` 先于 `core::ScriptEngine scriptEngine(...)` 声明。
  `scriptEngine.bindCoreManager(controller->coreManager())` 存的是非拥有指针，
  而局部变量按声明逆序析构 -> `controller` 先毁、`scriptEngine` 后毁，析构过程中 존재하던
  `coreConnections_` / `callbacks_` 可能访问已销毁 `CoreManager`。
- **修复方式**  
  调整 `main()` 局部变量顺序：`ScriptEngine scriptEngine(...)` **先声明**，
  `AppController` 的 `unique_ptr controller` **后声明**，从而确保 `scriptEngine` 析构时
  `CoreManager` 仍存活，避免析构期间的信号/回调访问悬挂指针。
- **原因**  
  非拥有指针绑定必须满足“宿主比被绑定对象活得更久”，或至少在析构顺序上显式保障。

### 3.2 `cpp/src/core/MITMManager.cpp` / `.h` — ClientConnection 析构竞态

- **问题**  
  `ClientConnection` 持有 `MITMManager* manager_`，且 `close()` 中通过
  `QTimer::singleShot(0, manager_, [...])` 请求延迟清理。若 manager 先析构，
  该计时器可能访问空悬/析构中的对象。
- **修复方式**  
  - 将 `ClientConnection::manager_` 从原始指针改为 `QPointer<MITMManager>`。
  - `close()` 与 `sendResponse()` 中发出的延时清理 lambda 增加 `if (!manager_) return;` 守护。
  - 对外接口（如构造、绑定）也随之改为接受 `QPointer` / 防御性判空。
- **原因**  
  `QPointer` 在所指 QObject 析构时自动变空，能切断“销毁顺序假设依赖”这类隐患。

### 3.3 `cpp/src/core/MITMManager.cpp` — `onClientReadyRead()` 重复解析请求头

- **问题**  
  同一个入站请求在 `onClientReadyRead()` 中出现了两段解析目标/headers 的逻辑，
  增加维护负担且容易产生不一致。
- **修复方式**  
  - 提取共享头解析结果，第一次解析后复用；
  - 移除第二段重复解析代码，保持“先判定 size/头、再取请求体、再派发”的单一流程。
- **原因**  
  重复切分/重复 `parseHeaderBlock` 既影响性能也有维护风险，合并后更易保证边界一致。

### 3.4 `cpp/src/core/config_manager.h` — 分裂头文件删除

- **问题**  
  `cpp/src/core/config_manager.h` 仅包含 `config_manager.h`，无额外声明。
- **修复方式**  
  删除 `cpp/src/core/config_manager.h`，并将仍包含它的地方改为直接包含 `config_manager.h`。
- **原因**  
  无意义的 include 层次会增加头文件依赖歧义，也可能在编译器包含路径差异下误导开发者。

### 3.5 `cpp/src/core/script_engine.cpp` — `invokeCallbacks` 回调异常中止语义

- **问题**  
  对同一个 Core 事件（如 `log`/`crash`/`started`/`stopped`/`state`），
  若其中一个 JS 回调抛出异常，旧代码仍 `continue` 并调用同一事件的后续回调，
  容易让脚本侧进入不一致状态，尤其在崩溃/启动失败窗口可能引发链式交互。
- **修复方式**  
  - 遍历回调函数时，一旦某个回调的 `JS_Call` 返回 `JS_EXCEPTION`，
    记录错误后**不再继续调用同事件的剩余回调**。
  - 异常回调的 JSValue 仍释放，避免泄漏。
- **原因**  
  对事件广播而言，“部分回调失败就进入感染态”通常比“强行全部调用完”更危险，
  特别是在生命周期敏感事件（crash、stopped、started）上。

### 3.6 `cpp/src/core/script_engine.cpp` — 加入 QuickJS 回调线程检查

- **问题**  
  `ScriptEngine` 的 JS 上下文并非线程安全；若 CoreManager 的信号最终在非创建线程上
  触发回调并执行 JS，会导致未定义行为。
- **修复方式**  
  - 在 `ScriptEngine` 中记录初始化线程（或主线程标识）；
  - 在 `invokeCallbacks()` / 涉及 `JS_Call` / `JS_Eval` 的入口增加线程一致性检查；
    若当前线程与允许执行 JS 的线程不符，拒绝执行并记录错误（不进入 JS）。
- **原因**  
  锁自 QuickJS 线程安全模型：同一个 Context 应只在单一线程中使用。
  这是一条防御线，防止后续有人在异步路径里误调 JS。

### 3.7 `cpp/src/core/script_engine.cpp` — 析构阶段断言 CoreManager 存活

- **问题**  
  析构阶段若还有未断开的 CoreManager 信号连接，`disconnectCoreSignals()` 虽会断开连接，
  但无法防止更早阶段因外部设计错误导致的悬挂访问（对应 main.cpp 生命周期问题的互补防御）。
- **修复方式**  
  - 在析构前/断开信号前增加断言或日志，确认 `core_` 指向的对象仍有效（至少在当前生命周期约定下）；
  - 明确注释该引擎实例必须比绑定的 CoreManager 晚析构。
- **原因**  
  多重防御：除了调整局部变量顺序，也在类内部保留一道可观测的守护，方便后续排查相似问题。

### 3.8 `cpp/src/core/plugin_sandbox.cpp` / `PluginManager` — 线程与目录剪裁强化

- **问题**  
  - 插件/沙箱 API 的线程模型假设未显式文档化；
  - `plugins/` 扫描不检查目录深度/剪裁，潜在的滥用面；
  - `jsHttpGet` 是阻塞式同步调用，易卡线程。
- **修复方式**  
  - 在插件加载/调用路径增加线程与生命周期注释，明确当前假设为“主线程/同一上下文调用”；
  - 插件载入前对目录项进行基本剪裁（例如跳过过深路径、拒绝明显非常规文件名），减少非预期输入；
  - 在相关注释与接口合同里强化“HTTP 同步调用会阻塞当前线程”的警告，便于后续异步化。
- **原因**  
  插件是代码执行边界，防御面要早；目录扫描也是攻击面，虽不能替代完整沙箱，但至少减少意外输入。

### 3.9 跨平台与配置路径小型修正

- **问题/强化**  
  - Windows `spawnDetached` 的环境变量修改属于全局副作用；
  - macOS 代理配置依赖多个子命令，部分失败路径未回滚；
  - Linux `gsettings` 类操作也有类似的状态残留风险；
  - MITM 脚本目录默认值里直接拼了 `QDir::homePath() + "/.config/sparkle/mitm"`，
    在部分平台/用户环境下可能不如 `QStandardPaths` 一致。
- **修复方式**  
  - 在相关平台后端注释中补充“失败时应回滚/保持幂等”的要求；
  - 对 Windows 环境变量修改部分增加强警告注释，提示后续应避免全局环境副作用；
  - MITM 默认脚本目录在缺失显式配置时，优先使用更平台中立的写法，减少硬编码假设。
- **原因**  
  跨平台客户端最怕“某平台看似运行正常，另一平台留下残缺系统状态”，
  所以哪怕现在来不及全做成回滚，也要把合同与风险注釈清楚。

---

## 4. 仍保留的已知风险（不在本次修改范围 / 后续建议）

- **订阅拉取鲁棒性**：建议后续显式识别/跳过 HTML、BOM、非 UTF-8、畸形 Base64 的订阅回包。
- **系统代理异常退出还原**：建议引入“变更前快照 + 失败/崩溃后恢复”协议，或至少让设代理操作尽量幂等。
- **大文件 MITM 流式转发**：目前仍是有限 body 模式，大流量场景下建议后续加入“仅脚本修改时才缓存，其余流式转发”。
- **QuickJS 文件作用域释放机械化**：建议后续统一用 RAII 包装降低手工 `JS_FreeValue` 遗漏概率。
- **服务模式端口重选后的重连语义**：建议端口重选后显式触发重连/重启协调，而非仅依靠后续读配置。
- **插件/脚本目录剪裁程度**：本次只是基本防御，若后续作为分发插件载入点，建议进一步增加沙箱与目录规则。

---

## 5. 验证方式（本次变更的自检建议）

1. **重新跑 CMake 配置**：确保删除 `config_manager.h`、改动 include、信号/指针类型变化后仍能 configure 通过。
2. **编译核心模块**：至少 `src/core` 和 `src/app` 的改动文件应无编译错误。
3. **回调/析构顺序检查**：可以加入单元测试，模拟 `ScriptEngine` 析构先于 CoreManager 的情况，确认不会出悬挂访问。
4. **MITM 请求解析回归**：用简单 HTTP 请求样例确认合并后的 `onClientReadyRead` 仍正确解析 method/url/headers/body。
5. **插件目录扫描**：构造非预期文件/深层目录，确认不会让扫描逻辑行为异常。

---

*文档生成时间：2026-09-06*  
*审计/修复范围：cpp/ 代码库（C++17/20、Qt6、QuickJS 集成侧重内存安全与多线程/JS 交互）*
