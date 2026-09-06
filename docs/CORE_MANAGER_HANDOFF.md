# 进程管理 + 网络通信 — 交接文档

> 本文档是内核子进程管理（原 Electron 的 Node `child_process`）与外部控制器网络通信
> （原 `mihomoApi.ts` + `mihomo-stream.ts`）两个领域的**专门交接说明**。
> 总体设计见 `../docs/CPP_PORT_DESIGN.md`；构建运行见 `../cpp/README.md`。

## 1. 交付内容

实现文件（均在 `cpp/src/core/`）：

| 文件 | 职责 |
|------|------|
| `core_manager.h/.cpp` | 内核管理编排：直连 / service（detached+TCP+Bearer）双模式启停、就绪探测、崩溃/启动失败通知 |
| `core_process_controller.h/.cpp` | 底层 `QProcess`（`unique_ptr` 持有）+ 优雅终止升级（仅直连模式使用） |
| `single_instance.h/.cpp` | QLocalServer 唯一锁 + 存活探测 |
| `socket_transport.h/.cpp` | `SocketTransport` 抽象 + `LocalSocketTransport`/`TcpSocketTransport`（直连 / service 共享传输层） |
| `http_client.h/.cpp` | HTTP/1.1：直连走 `QLocalSocket`，service 走 `QNetworkAccessManager` over TCP + `Authorization: Bearer` |
| `ws_client.h/.cpp` | RFC6455 WebSocket：`SocketTransport` 复用，service 走 TCP + Bearer 握手 |
| `mihomo_api_client.h/.cpp` | REST + 4 条 WS 流编排（traffic/memory/logs/connections），模式与凭据透传 |
| `runtime_config_factory.h/.cpp` | 运行配置生成：覆写合并 / cleanup / 受控回写 / diffWorkDir / **service 模式写入 external-controller + secret** |
| `js_engine.h/.cpp` | QuickJS 引擎封装：`main(profile)` 执行 + Promise 等待 + console/b64/yaml/Buffer + **真网络 fetch** |

## 2. 进程管理

### 2.1 API

```cpp
// CoreManager
bool startCore(const QString& configPath);  // 已运行则 false；spawn 失败 false + coreCrashed(-1)
bool stopCore();                            // 发起优雅停止成功返回 true
bool isRunning() const;                     // 直连看 QProcess；service 看 pid 存活
void startup(bool detached = false);        // 自动按 corePermissionMode 决定走 service 模式
void shutdown(bool force = false);          // force 清零崩溃自动重启预算
void restart();                             // 先优雅停止，退净后再启动

signals: logReceived(QString), coreCrashed(int), stateChanged(CoreState),
         coreStarted(), coreStopped(), controllerListenError(), tunPermissionError();
```

`detached` 等价于"`startup(detached=true)` 或 `appConfig.corePermissionMode == "service"`"——后者
视为触发 service 模式的开关。

### 2.2 关键决策

- **RAII + 双 `unique_ptr`**：`CoreManager` 持 `unique_ptr<CoreProcessController>`，后者持
  `unique_ptr<QProcess>`（**不设 Qt 父对象**，避免对象树与 unique_ptr 双重 delete）。析构链自动
  终止子进程，任何退出路径不留孤儿。
- **优雅终止升级**：`stop()` = SIGINT → 3s SIGTERM → 6s SIGKILL；Windows 退回 terminate() → kill()。
- **停止代际 `stopGeneration_`**：每次 start()/stop() 递增；旧升级定时器只在代际一致时生效，
  防"重启后旧定时器误杀新进程"竞态。
- **主动/被动退出区分**：`stoppingRequested_` 使正常关闭走 `coreStopped`，崩溃才发 `coreCrashed`；
  启动失败（二进制缺失 / `FailedToStart` / spawn 失败）发 `coreCrashed(-1)`。
- **显式 `-f <config> -d <workDir>`**：`-f` 指定配置文件，`-d` 定运行时目录（geodata/缓存），
  跨 profile 不易串配置。

### 2.3 本轮评审修复

1. **`pid==0` 误信号整组（严重）**：`terminateAt` 在 `QProcess::Starting` 窗口内 `processId()` 为 0，
   原 `::kill(0, SIGINT)` 会信号**整个进程组**。现加 `state()==Running && pid>0` 双重守卫；
   `Starting` 窗口改走 `QProcess::kill()`。
2. **`startCore` 返回值确定性**：新增 `waitForStarted()`，`launch()` 同步确认 spawn 成功，
   失败即 `coreCrashed(-1)` + 返回 false（不再只返回"已接收请求"）。
3. **重复启动守卫**：`CoreProcessController::start` 已 Running 时拒绝。
4. **单实例残留锁**：原 `QSharedMemory` 被 SIGKILL 强杀后会残留，二次启动误判"已运行"。
   改为 **QLocalServer 唯一锁 + 真实连接探测**，死进程不在监听即可清理重启。

### 2.4 service 模式（detached + TCP 控制器 + Bearer）

> 与 Electron 原版的 `corePermissionMode==='service'` 守护进程（SPARKLE-AUTH-V2 签名 +
> 独立 service 二进制）不同；本实现走 mihomo 原生 `external-controller` + `secret`，
> 进程以 detached 方式独立存活，由 pid 文件维持跨重启状态。

**触发**：`appConfig.corePermissionMode == "service"`（或显式 `startup(true)`）。

**进程生命周期**（`launchDetached`）：
- 重连：`dataDir/core.pid.json` 存在且 pid 仍存活 → 不重复 spawn，直接进入就绪探测。
- 新启动：`QProcess::startDetached`（POSIX 用 `/usr/bin/env` 包装传环境变量；
  Windows 临时 `qputenv` + `startDetached` 再还原）；写 `core.pid.json = {pid, path, startedAt}`。
- 优雅停止（`stopDetachedProcess`）：按 pid 发 `SIGINT` → 1s 等待 → 仍存活则 `SIGKILL`；
  Windows `taskkill /PID /T /F`；最后删除 pid 文件。
- `isRunning()`：service 模式看 `state==Running || (detachedPid>0 && isPidAlive)`，
  直连模式仍看 `QProcess::isRunning()`。

**环境变量**：`buildEnvironment()` 抽出直连/service 共享（`DISABLE_LOOPBACK_DETECTOR`、
`DISABLE_EMBED_CA` / `DISABLE_SYSTEM_CA` / `DISABLE_NFTABLES` / `SAFE_PATHS`），从
`appConfig` 读开关。

**就绪判定**：detached 模式没有 stdout（`stdio: 'ignore'`），无法走 provider 追踪；
改为**轮询控制器 `/version`（带 Bearer）**，每 500ms 一次，至多 20 次（20s）；命中即
`completeInitialization()` → `startStreams` + `coreStarted`。

**生成配置**：`RuntimeConfigFactory::generate()` 在 service 模式下额外写入
`external-controller: 127.0.0.1:<port>` + `secret: <随机>`；不再使用 `-ext-ctl-unix/-pipe` 参数。

**凭据持久化**：`ConfigManager::serviceControllerEndpoint()` 读
`appConfig.serviceController = {host, port, secret}`；缺失/端口被占用时自动**生成空闲端口
+ 32 字节随机 secret（hex）并持久化**；secret 仅在 `serviceMode()` 为真时生成，避免误写。

## 3. 网络通信

### 3.1 `HttpClient`（HTTP/1.1：QLocalSocket 直连 / QNAM over TCP service）

```cpp
void setEndpoint(const QString& socketOrHostPort, bool serviceMode);
void setSecret(const QString& secret);   // service 模式 Bearer 凭据
void request(method, path, body, std::function<void(const HttpResult&)>);
void get(path, cb);
struct HttpResult { int status; QByteArray body; bool ok; QString error; };
```

- 直连：一次请求一条 `QLocalSocket` 连接（`Connection: close`），串行队列 + 10s 超时；
  secret 非空时附加 `Authorization: Bearer <secret>`（兼容 mihomo unix 控制器启用 secret）。
- service：交 `QNetworkAccessManager` 发 TCP 请求（URL = `http://` + hostport + path），
  强制带 `Authorization: Bearer <secret>`；超时 15s。`currentReply_` 防超时/正常完成路径
  双重 finish 的引用追踪。
- 响应解析支持 **Content-Length** 与 **Transfer-Encoding: chunked** 两种形态；
  核心是独立可测函数 `parseHttpResponse()`。

### 3.2 `WsClient`（RFC6455：SocketTransport 抽象）

```cpp
void configure(const QString& endpoint, const QString& path, int retryBudget = 10,
               bool serviceMode = false, const QString& secret = QString());
void start(); stop(); restart(); resetRetryBudget(); bool isConnected() const;
signals: messageReceived(QByteArray), connected(), disconnected(QString);
```

- **传输层抽象**（`SocketTransport`）：`LocalSocketTransport` / `TcpSocketTransport` 统一暴露
  `opened/readyRead/closed/failed` 信号与读写接口；WsClient 按 `serviceMode` 选择构造。
  TCP 仅在"真正连上后再断"时上报 `closed`（`everOpened_`），对齐 QLocalSocket 在连接被拒时
  仅触发 error 的语义，避免 onError/onDisconnected 重复计数。
- 握手：`Sec-WebSocket-Key`（16 随机字节 base64）+ `Sec-WebSocket-Accept` 校验（SHA1）；
  secret 非空时额外发送 `Authorization: Bearer <secret>`。
- 帧编解码：**客户端帧强制掩码**；解帧支持 7/16/64 位长度、掩码/无掩码、分片（continuation）、
  ping→pong、close 应答；单帧上限 16MB 防内存异常。
- 有界重连：失败 1s 重试、预算 `retryBudget_` 递减；**收到成功消息即恢复满预算**；
  `everConnected_` 区分"连不上"与"已连后断"，避免 error/disconnected 双重计数。
- 结束语义：`stop()` 发 close 帧再断；`restart()` = stop+start。

### 3.3 `MihomoApiClient`（流编排）

- 4 条 WS：`/traffic`→`trafficUpdated`（LatestSender **100ms**）、`/memory`→`memoryUpdated`、
  `/logs`→`LogManager::publishMihomoLogLines`（不节流）、`/connections`→`connectionsUpdated`
  （LatestSender **200ms**）。
- REST：`fetchVersion()` = GET `/version`，解析 `version`/`meta`。
- 端点：直连 = `Paths::controllerSocket()`；service = `host:port`（从
  `config_->serviceControllerEndpoint()` 取，懒生成并持久化）。
- 凭据透传：`setServiceMode(true)` 后 `secret()` 返回对应 secret，写入 HttpClient 与 4 条
  WsClient 的握手/请求头。

## 4. 与原 Node 代码映射

| 原逻辑 | 现实现 |
|--------|--------|
| `child_process.spawn` | `CoreProcessController::QProcess::start` |
| `child.stdout/stderr.on('line')` | `readyRead*` → `stdOutLine`/`stdErrLine` → `logReceived` |
| `child.on('exit')` + 崩溃重启预算 10 | `onProcessFinished` + `restartBudget_` |
| `child.kill('SIGINT/TERM/KILL')` | `stop()` 三级升级 |
| axios over unix socket | `HttpClient`（QLocalSocket） |
| `mihomo-stream.ts` 4 条 WS | `WsClient` × 4 + `MihomoApiClient` 编排 |
| `latest-sender.ts` 节流 | `LatestSender<T>` |
| `log.ts` 分源（console/ws） | `LogManager::setMihomoLogSourceFromConsole` + `publishMihomoLogLines` |

## 5. 待完成 / 后续（P2；`TODO(phase 3)` 已全部清除）

**进程 / 配置生成**
- Windows `QProcess::kill` / `SetConsoleCtrlEvent(CTRL_BREAK)` 精细化（现用 QProcess 通用 terminate）。
- 与真实 mihomo 二进制的端到端集成测试（需 `SPARKLE_MIHOMO_PATH` 指向真实二进制）。
- detached 模式的就绪探测超时后清理 pid 文件 / 复位状态（仅打 `coreCrashed(-1)`，未移除残留 pid 文件）。

**网络**
- proxies/group 的 REST 端点（`/proxies`、节点延迟历史、节点增删改 `PUT /proxies/<name>`）。
- `/logs` 实际行格式按真实 mihomo 输出回归确认（当前按 logfmt 解析，与 stdout 一致）。
- TCP 模式下的 HTTPS 部署（生产 mihomo 不启用 TLS，但若用户开启了 TLS，需补齐）。
- service 模式 secret 落盘加密（当前 `appConfig` 直接明文持久化到 yaml，可接受但不强）。

**JS 覆写 / fetch**
- 已实现真网络 `fetch`（见下），但调用在主线程同步阻塞（约等于 Node 的 main 进程同步 await）；
  若脚本中存在长时间 fetch，会阻塞 UI/生成流程。可作为后续工作：把 `generate()` 移到工作线程。
- `fetch` 仅支持 `http(s)`；其他 scheme（`unix:`/`data:`）当前直接报错。

> 已在本轮完成（原 phase 1）：env 回填（`DISABLE_LOOPBACK_DETECTOR`/`DISABLE_EMBED_CA`/`DISABLE_SYSTEM_CA`/`DISABLE_NFTABLES`/`SAFE_PATHS`）、
> provider 初始化追踪 + `controllerListenError`、`diffWorkDir` 差分工作目录（`-d` 跟随配置目录）、
> `factory.ts` 完整 `generateProfile`（覆写合并 / `restoreUiControlledFields` / `clean*` / `syncControledMihomoConfig` /
> `prepareProfileWorkDir`）、HTTP/WS 传输层、系统代理串行 + PAC server、YAML store 错误区分与写回滚、
> logfmt 完整解析与 `saveLogs=false`、`publishMihomoLogLines`。
>
> 已在本轮完成（JS 覆写）：集成 **bellard/QuickJS**（FetchContent 拉取 codeload + 固定 SHA256，pin 到
> commit `04be246`，编译 quickjs/dtoa/libregexp/libunicode/cutils 5 个 C 文件），`js_engine.cpp` 提供
> `runOverrideScript(script, profile, logPath)`：执行脚本 `main(profile)`，支持 Promise（`JS_PromiseThen`+
> `JS_ExecutePendingJob` 排空任务队列），内置 `console.log/info/error/debug`、`b64e/b64d`、`yaml.parse/stringify`、
> `Buffer` 垫片；10s 中断超时 + 256MB 内存上限；日志写入 `override/<id>/log`。
>
> 已在本轮完成（JS 覆写 fetch 真网络）：`__nativeFetch` 原生 C 函数用 `QNetworkAccessManager` +
> 嵌套 `QEventLoop` 同步执行 HTTP(S)（timeout 30s），经 JS 包装暴露**类 Web `fetch` 接口**：
> 返回 already-resolved native Promise 的 `Response`，含 `.ok`/`.status`/`.statusText`/
> `.headers.get()/has()`/`.text()/`.json()`（`.text()/.json()` 也返回 already-resolved Promise，
> 与 Web 语义一致），headers 在 JS 端经 `JSON.stringify` 序列化跨 C/JS 边界。

## 6. 验证结果（macOS / AppleClang 21 / Qt 6.10.1）

```text
cmake --build cpp/build      → exit 0
ctest --test-dir cpp/build   → 5/5 通过
  - tst_yaml_store
  - tst_latest_sender
  - tst_core_process_controller（启停 / 优雅停止 / stdout 逐行捕获）
  - tst_http_ws（HTTP Content-Length/chunked 解析；RFC6455 帧编解码 + 已知握手向量；
                + service 模式 TCP + Bearer Authorization 头回环验证）
  - tst_js_engine（同步/异步 main、console/b64/yaml/Buffer 助手、异常与非法返回、
                  fetch 真网络 + 本地 HTTP 服务器往返）
offscreen 冒烟启动           → 正常常驻无崩溃
```