#include "core_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>

#include <nlohmann/json.hpp>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "config_manager.h"
#include "core_process_controller.h"
#include "log_manager.h"
#include "mihomo_api_client.h"
#include "paths.h"
#include "runtime_config_factory.h"

namespace sparkle::core {
namespace {

// detached pid 存活判定（POSIX `kill(pid, 0)`；Windows 回退 true，
// 兜底交由控制器 /version 探测）。与下方 readPidFile 同处匿名 ns，对外不可见。
bool isPidAlive(qint64 pid) {
  if (pid <= 0) return false;
#if defined(Q_OS_UNIX)
  if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
  return errno == EPERM;   // 进程存在但无权限发信号 → 也算存活
#else
  return true;
#endif
}

qint64 readPidFile() {
  QFile f(Paths::corePidPath());
  if (!f.open(QIODevice::ReadOnly)) return 0;
  try {
    const nlohmann::json j = nlohmann::json::parse(f.readAll().toStdString());
    return j.value("pid", static_cast<qint64>(0));
  } catch (...) {
    return 0;
  }
}

void writePidFile(qint64 pid, const QString& program) {
  nlohmann::json j;
  j["pid"] = pid;
  j["path"] = program.toStdString();
  j["startedAt"] = QDateTime::currentMSecsSinceEpoch();
  QFile f(Paths::corePidPath());
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QByteArray::fromStdString(j.dump()));
  }
}

// detached spawn：POSIX 用 /usr/bin/env 包装传递环境变量（env exec 替换自身，pid 即 mihomo pid）；
// Windows 临时 qputenv 后 startDetached 再还原。
bool spawnDetached(const QString& program, const QStringList& args,
                   const QProcessEnvironment& env, qint64* pidOut) {
#if defined(Q_OS_WIN)
  const QStringList keys = env.keys();
  QList<QByteArray> names;
  QList<QByteArray> savedVals;
  names.reserve(keys.size());
  savedVals.reserve(keys.size());
  for (const QString& k : keys) {
    const QByteArray name = k.toUtf8();
    names << name;
    savedVals << qgetenv(name.constData());
    qputenv(name.constData(), env.value(k).toUtf8());
  }
  const bool ok = QProcess::startDetached(program, args, QString(), pidOut);
  for (int i = 0; i < names.size(); ++i) {
    if (savedVals[i].isEmpty()) qunsetenv(names[i].constData());
    else qputenv(names[i].constData(), savedVals[i]);
  }
  return ok;
#else
  QStringList wrapped;
  const QStringList keys = env.keys();
  for (const QString& k : keys) {
    wrapped << (k + QLatin1Char('=') + env.value(k));
  }
  wrapped << program;
  wrapped += args;
  return QProcess::startDetached(QStringLiteral("/usr/bin/env"), wrapped, QString(), pidOut);
#endif
}

}  // namespace

CoreManager::CoreManager(ConfigManager* config, RuntimeConfigFactory* factory,
                         MihomoApiClient* api, LogManager* log, QObject* parent)
    : QObject(parent),
      config_(config),
      factory_(factory),
      api_(api),
      log_(log),
      process_(std::make_unique<CoreProcessController>(this)) {
  connect(process_.get(), &CoreProcessController::started, this, &CoreManager::onProcessStarted);
  connect(process_.get(), &CoreProcessController::finished, this, &CoreManager::onProcessFinished);
  connect(process_.get(), &CoreProcessController::stdOutLine, this, &CoreManager::onStdOutLine);
  connect(process_.get(), &CoreProcessController::stdErrLine, this, &CoreManager::onStdErrLine);
  connect(process_.get(), &CoreProcessController::errorOccurred, this,
          &CoreManager::onProcessError);
}

CoreManager::~CoreManager() {
  // RAII：process_ 是 std::unique_ptr<CoreProcessController>。本析构函数执行完后，
  // 成员按声明逆序析构，process_ 先于（声明的）其余成员销毁——此时
  // CoreProcessController::~ 内部会终止 QProcess 子进程。故无需显式 stopCore()。
}

CoreState CoreManager::state() const { return state_; }

void CoreManager::setState(CoreState state) {
  if (state_ == state) return;
  state_ = state;
  emit stateChanged(state_);
}

// ===== 需求核心 API =====

bool CoreManager::startCore(const QString& configPath) {
  // 已启动/启动中则拒绝重复启动。
  if (state_ == CoreState::Starting || state_ == CoreState::Running) {
    return false;
  }
  return launch(configPath);
}

bool CoreManager::stopCore() {
  const bool running = isRunning();
  // 既不在运行、也不在"已发起启动但尚未 spawn"的窗口内，视为无需停止。
  if (!running && state_ != CoreState::Starting) {
    return false;
  }

  stoppingRequested_ = true;   // 标记主动停止，避免被误判为崩溃
  setState(CoreState::Stopping);
  api_->stopStreams();

  // service mode stop is asynchronous. Keep the stopping state until the pid is
  // gone, so restart cannot spawn a replacement while the old process survives.
  if (config_->serviceMode()) {
    const bool restart = restartRequested_;
    suppressDetachedStoppedSignal_ = restart;
    stopDetachedProcess();
    if (detachedStopPid_ == 0) {
      completeDetachedStop();
    }
    return true;
  }

  process_->stop();            // SIGINT → SIGTERM(3s) → SIGKILL(6s)
  return true;
}

bool CoreManager::isRunning() const {
  if (config_->serviceMode()) {
    return state_ == CoreState::Running ||
           (detachedPid_ > 0 && isPidAlive(detachedPid_));
  }
  return process_ && process_->isRunning();
}

bool CoreManager::launch(const QString& configPath) {
  // 1) 解析内核二进制路径（SPARKLE_MIHOMO_PATH 优先）并做存在性检查。
  const QString program = Paths::resolveMihomoCorePath();
  if (program.isEmpty() || !QFileInfo::exists(program)) {
    log_->appendAppLog(
        QStringLiteral("[CoreManager] 内核二进制不存在或未设置 SPARKLE_MIHOMO_PATH: %1\n")
            .arg(program));
    emit coreCrashed(-1);   // 启动失败：约定以 -1 作为退出码
    return false;
  }

  setState(CoreState::Starting);
  log_->setMihomoLogSourceFromConsole(true);

  // 2) 组装启动参数。
  //    -d <工作目录>：内核工作目录（geodata/缓存等运行时文件所在）；
  //      直接取配置文件所在目录 —— diffWorkDir 与非 diff 两种布局都自然成立；
  //    -f <config>：显式指定配置文件（比依赖 -d 自动发现更明确，跨 profile 也正确）；
  //    -ext-ctl-unix/pipe：外部控制器端点（REST + WS 都走这里）。
  const QString workDir = QFileInfo(configPath).absolutePath();
  QStringList args;
  args << QStringLiteral("-d") << workDir;
  args << QStringLiteral("-f") << configPath;
#if defined(Q_OS_WIN)
  args << QStringLiteral("-ext-ctl-pipe") << Paths::controllerSocket();
#else
  args << QStringLiteral("-ext-ctl-unix") << Paths::controllerSocket();
#endif

  // 3) 环境变量（对齐原 createCoreEnvironment）：从 appConfig 读开关与 safePaths。
  const QProcessEnvironment env = buildEnvironment();

  // 4) 记录运行配置中声明的 rule/proxy providers（小写归一），用于就绪前等待其初始化完成。
  pendingProviders_.clear();
  hasProviders_ = false;
  const nlohmann::json rcfg = factory_->runtimeConfig();
  const auto collectProviders = [&](const char* key) {
    if (!rcfg.contains(key) || !rcfg[key].is_object()) return;
    for (auto it = rcfg[key].begin(); it != rcfg[key].end(); ++it) {
      pendingProviders_.insert(QString::fromStdString(it.key()).trimmed().toLower());
    }
  };
  collectProviders("rule-providers");
  collectProviders("proxy-providers");
  hasProviders_ = !pendingProviders_.empty();

  process_->start(program, args, env);

  // 同步确认 spawn 成功（通常 <50ms 返回），使 startCore() 的 bool 具有确定性语义：
  // 仅在“二进制存在且成功 spawn”时为 true；失败立即回报 coreCrashed(-1)。
  if (!process_->waitForStarted(3000)) {
    log_->appendAppLog(QStringLiteral("[CoreManager] 内核 spawn 失败\n"));
    setState(CoreState::Stopped);
    emit coreCrashed(-1);
    return false;
  }
  return true;
}

QProcessEnvironment CoreManager::buildEnvironment() const {
  const nlohmann::json app = config_->appConfig();
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  const auto flag = [&](const char* key, const char* envName) {
    env.insert(QString::fromLatin1(envName),
               app.value(key, false) ? QStringLiteral("true") : QStringLiteral("false"));
  };
  flag("disableLoopbackDetector", "DISABLE_LOOPBACK_DETECTOR");
  flag("disableEmbedCA", "DISABLE_EMBED_CA");
  flag("disableSystemCA", "DISABLE_SYSTEM_CA");
  flag("disableNftables", "DISABLE_NFTABLES");

  QStringList safePaths;
  if (app.contains("safePaths") && app["safePaths"].is_array()) {
    for (const auto& p : app["safePaths"]) {
      if (p.is_string()) safePaths << QString::fromStdString(p.get<std::string>());
    }
  }
  if (!safePaths.isEmpty()) {
    env.insert(QStringLiteral("SAFE_PATHS"), safePaths.join(QDir::listSeparator()));
  }
  return env;
}

bool CoreManager::launchDetached(const QString& configPath) {
  // 1) 重连：pid 文件存在且进程存活 → 不重复 spawn，直接进入就绪探测（附着现有内核）。
  const qint64 existingPid = readPidFile();
  if (existingPid > 0 && isPidAlive(existingPid)) {
    const QString recordedPath = [&] {
      QFile f(Paths::corePidPath());
      if (!f.open(QIODevice::ReadOnly)) return QString();
      try {
        return QString::fromStdString(
            nlohmann::json::parse(f.readAll()).value("path", std::string()));
      } catch (...) {
        return QString();
      }
    }();
    if (!recordedPath.isEmpty() && QFileInfo(recordedPath).canonicalFilePath() !=
                                      QFileInfo(Paths::resolveMihomoCorePath()).canonicalFilePath()) {
      log_->appendAppLog(QStringLiteral("[CoreManager] 忽略属于其他内核的 stale pid=%1\n")
                             .arg(existingPid));
      QFile::remove(Paths::corePidPath());
    } else {
      log_->appendAppLog(
          QStringLiteral("[CoreManager] 检测到已运行的 detached 内核 pid=%1，重连\n").arg(existingPid));
      detachedPid_ = existingPid;
      setState(CoreState::Starting);
      startReadyPoll();
      return true;
    }
  }

  // 2) 内核二进制存在性检查。
  const QString program = Paths::resolveMihomoCorePath();
  if (program.isEmpty() || !QFileInfo::exists(program)) {
    log_->appendAppLog(
        QStringLiteral("[CoreManager] 内核二进制不存在或未设置 SPARKLE_MIHOMO_PATH: %1\n")
            .arg(program));
    emit coreCrashed(-1);
    return false;
  }

  // 3) 组装参数：无 -ext-ctl-unix/-pipe（控制器走运行配置里的 external-controller TCP 端点）。
  const QString workDir = QFileInfo(configPath).absolutePath();
  QStringList args;
  args << QStringLiteral("-d") << workDir;
  args << QStringLiteral("-f") << configPath;

  setState(CoreState::Starting);

  // 4) detached spawn + pid 文件。
  qint64 pid = 0;
  if (!spawnDetached(program, args, buildEnvironment(), &pid) || pid <= 0) {
    log_->appendAppLog(QStringLiteral("[CoreManager] 内核 detached spawn 失败\n"));
    setState(CoreState::Stopped);
    emit coreCrashed(-1);
    return false;
  }
  detachedPid_ = pid;
  writePidFile(pid, program);
  log_->appendAppLog(QStringLiteral("[CoreManager] 内核已 detached 启动 pid=%1\n").arg(pid));
  startReadyPoll();
  return true;
}

void CoreManager::startReadyPoll() {
  detachedStopTimer_.stop();
  detachedStopPid_ = 0;
  detachedStopAttempts_ = 0;
  detachedStopKillSent_ = false;
  readyPollAttempts_ = 0;
  readyPollInFlight_ = false;
  readyPollTimer_.stop();
  connect(&readyPollTimer_, &QTimer::timeout, this, &CoreManager::pollDetachedReady,
          Qt::UniqueConnection);
  readyPollTimer_.start(500);
}

void CoreManager::pollDetachedReady() {
  if (state_ != CoreState::Starting) {
    readyPollTimer_.stop();
    return;
  }
  if (++readyPollAttempts_ > 40) {   // 20s 就绪探测超时
    readyPollTimer_.stop();
    log_->appendAppLog(QStringLiteral("[CoreManager] detached 内核就绪探测超时\n"));
    setState(CoreState::Stopped);
    emit coreCrashed(-1);
    return;
  }
  if (readyPollInFlight_) return;
  readyPollInFlight_ = true;
  api_->fetchVersion([this](const ControllerVersion& v) {
    readyPollInFlight_ = false;
    if (state_ != CoreState::Starting) return;
    if (!v.version.isEmpty()) {
      readyPollTimer_.stop();
      completeInitialization();
    }
  });
}

void CoreManager::stopDetachedProcess() {
  if (detachedStopPid_ != 0) return;

  const qint64 pid = readPidFile();
  if (pid <= 0 || !isPidAlive(pid)) {
    QFile::remove(Paths::corePidPath());
    detachedPid_ = 0;
    return;
  }

  detachedStopPid_ = pid;
  detachedStopAttempts_ = 0;
  detachedStopKillSent_ = false;

#if defined(Q_OS_UNIX)
  // Asynchronous SIGINT -> polling -> SIGKILL. The timer is a member and is
  // stopped before any later launch, so it cannot target a replacement PID.
  ::kill(static_cast<pid_t>(pid), SIGINT);
  log_->appendAppLog(
      QStringLiteral("[CoreManager] detached 内核 SIGINT 已发送 pid=%1\n").arg(pid));

  detachedStopTimer_.setSingleShot(false);
  detachedStopTimer_.setInterval(200);
  disconnect(&detachedStopTimer_, nullptr, this, nullptr);
  connect(&detachedStopTimer_, &QTimer::timeout, this, [this, pid] {
    if (detachedStopPid_ != pid) {
      detachedStopTimer_.stop();
      return;
    }
    if (!isPidAlive(pid)) {
      completeDetachedStop();
      return;
    }
    if (++detachedStopAttempts_ >= 10 && !detachedStopKillSent_) {
      detachedStopKillSent_ = true;
      ::kill(static_cast<pid_t>(pid), SIGKILL);
      log_->appendAppLog(
          QStringLiteral("[CoreManager] detached 内核 SIGKILL pid=%1\n").arg(pid));
    }
    if (detachedStopKillSent_ && detachedStopAttempts_ >= 15) {
      // Do not remove a PID file while the process still exists: a later
      // invocation must not accidentally attach to an untracked process.
      completeDetachedStop();
    }
  });
  detachedStopTimer_.start();
#else
  QProcess::startDetached(QStringLiteral("taskkill"),
                          {QStringLiteral("/PID"), QString::number(pid),
                           QStringLiteral("/T"), QStringLiteral("/F")});
  completeDetachedStop();
#endif
}

void CoreManager::completeDetachedStop() {
  detachedStopTimer_.stop();
  const qint64 pid = detachedStopPid_;
  detachedStopPid_ = 0;
  detachedStopAttempts_ = 0;
  detachedStopKillSent_ = false;
  if (pid > 0 && isPidAlive(pid)) {
    log_->appendAppLog(QStringLiteral("[CoreManager] detached 内核停止超时 pid=%1\n").arg(pid));
    return;
  }
  QFile::remove(Paths::corePidPath());
  detachedPid_ = 0;
  stoppingRequested_ = false;
  setState(CoreState::Stopped);
  const bool restart = restartRequested_;
  restartRequested_ = false;
  if (restart) {
    startup();
  } else if (!suppressDetachedStoppedSignal_) {
    emit coreStopped();
  }
  suppressDetachedStoppedSignal_ = false;
}

// ===== 上层编排 =====

void CoreManager::startup(bool detached) {
  if (state_ == CoreState::Starting || state_ == CoreState::Running) return;

  // service 模式（appConfig.corePermissionMode == "service"）等价于 detached 启动：
  // 控制器改走 TCP + Bearer，进程以独立守护方式存活并由 pid 文件重连。
  detached = detached || config_->serviceMode();
  api_->setServiceMode(detached);

  // 生成运行时配置（profile + override + controlled → work/config.yaml），
  // 再用刚生成的配置路径启动内核。
  factory_->generate();
  if (detached) {
    launchDetached(factory_->generatedPath());
  } else {
    startCore(factory_->generatedPath());
  }
}

void CoreManager::shutdown(bool force) {
  if (force) {
    restartBudget_ = 0;   // 强制停止：不再崩溃自动重启
  }
  if (!stopCore()) {
    // 本就没在运行：直接复位状态并广播停止。
    setState(CoreState::Stopped);
    emit coreStopped();
  }
}

void CoreManager::restart() {
  if (isRunning() || state_ == CoreState::Starting) {
    restartRequested_ = true;   // 在 onProcessFinished 中再触发 startup
    stopCore();
  } else {
    startup();
  }
}

// ===== 进程事件回路由 =====

void CoreManager::onProcessStarted() {
  log_->appendAppLog(QStringLiteral("[CoreManager] 内核进程已启动\n"));
}

void CoreManager::onStdOutLine(const QString& line) {
  emit logReceived(line);                        // 需求：逐行日志广播
  log_->appendRawCoreChunk(line, LogLevel::Info); // 同时写入日志子系统（→ UI/文件）

  // 就绪判定（对齐 startup-chain.ts）：外部控制器监听错误 → controllerListenError；
  // 命中启动标记（RESTful API / external controller）且声明的 providers 全部初始化完成后视为就绪。
  if (state_ == CoreState::Starting) {
    static const QRegularExpression providerRe(QStringLiteral("Start initial provider ([^\"]+)"));
    const auto pm = providerRe.match(line);
    if (pm.hasMatch()) {
      pendingProviders_.erase(pm.captured(1).trimmed().toLower());
    }

    if (line.contains(QLatin1String("listen error"))) {
      emit controllerListenError(line);
    } else if ((line.contains(QLatin1String("RESTful API")) ||
                line.contains(QLatin1String("external controller"))) &&
               (!hasProviders_ || pendingProviders_.empty())) {
      completeInitialization();
    }
  }
  if (line.contains(QLatin1String("operation not permitted"))) {
    emit tunPermissionError();
  }
}

void CoreManager::onStdErrLine(const QString& line) {
  emit logReceived(line);
  log_->appendRawCoreChunk(line, LogLevel::Error);
}

void CoreManager::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
  log_->appendAppLog(QStringLiteral("[CoreManager] 内核进程退出 code=%1 %2\n")
                         .arg(exitCode)
                         .arg(status == QProcess::CrashExit ? QStringLiteral("crash")
                                                            : QStringLiteral("normal")));
  api_->reset();

  const bool wasRunning = (state_ == CoreState::Running);
  const bool wasRequested = stoppingRequested_;
  // 非主动停止、且（崩溃退出 或 非零退出码）→ 视为崩溃。
  const bool crashed = !wasRequested && (status == QProcess::CrashExit || exitCode != 0);
  stoppingRequested_ = false;

  setState(CoreState::Stopped);

  if (crashed) {
    emit coreCrashed(exitCode);   // 需求：通知上层崩溃
  }

  // 主动 restart()
  if (restartRequested_) {
    restartRequested_ = false;
    startup();
    return;
  }

  if (wasRequested) {
    emit coreStopped();           // 正常关闭完成
  } else if (wasRunning && crashed && restartBudget_ > 0) {
    // 崩溃自动重启（预算内，对应原 restartCoreBudget = 10）
    --restartBudget_;
    log_->appendAppLog(
        QStringLiteral("[CoreManager] 内核崩溃，自动重启（剩余预算 %1）\n").arg(restartBudget_));
    startup();
  }
}

void CoreManager::onProcessError(QProcess::ProcessError error) {
  // 关注"启动失败"：QProcess 无法 spawn 该二进制（而非运行中崩溃）。
  if (error == QProcess::FailedToStart) {
    log_->appendAppLog(QStringLiteral("[CoreManager] 内核启动失败（FailedToStart）\n"));
    if (state_ == CoreState::Starting) {
      setState(CoreState::Stopped);
      emit coreCrashed(-1);
    }
  }
}

void CoreManager::completeInitialization() {
  if (state_ != CoreState::Starting) return;
  setState(CoreState::Running);
  log_->setMihomoLogSourceFromConsole(false);
  api_->startStreams();
  restartBudget_ = 10;   // 正常起来后重置崩溃预算
  emit coreStarted();
}

}  // namespace sparkle::core