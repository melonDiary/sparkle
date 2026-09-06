#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <memory>
#include <set>

#include "models.h"

namespace sparkle::core {

class ConfigManager;
class RuntimeConfigFactory;
class MihomoApiClient;
class LogManager;
class CoreProcessController;

// 内核管理器（原 core/manager.ts 中 Node child_process 逻辑的 C++ 迁移目标）。
//
// 职责划分：
// - 本类：子进程生命周期编排、日志转发、状态查询、崩溃/启动失败通知。
// - CoreProcessController：底层 QProcess（std::unique_ptr 持有）+ 优雅终止升级。
//
// RAII：CoreManager 持有 std::unique_ptr<CoreProcessController>；当 CoreManager 析构时，
// 该 unique_ptr 先析构，进而触发 CoreProcessController::~（其中终止 QProcess 子进程），
// 因此任何情况下子进程都不会成为孤儿。
class CoreManager final : public QObject {
  Q_OBJECT
public:
  CoreManager(ConfigManager* config, RuntimeConfigFactory* factory, MihomoApiClient* api,
              LogManager* log, QObject* parent = nullptr);
  ~CoreManager() override;   // RAII：随成员析构链自动终止子进程

  // —— 需求核心 API ——
  bool startCore(const QString& configPath);  // 以指定配置文件路径启动内核
  bool stopCore();                             // 优雅停止内核进程
  bool isRunning() const;                      // 状态查询

  // —— 上层编排（与原 AppController / UI 兼容）——
  void startup(bool detached = false);   // 生成运行时配置后调用 startCore
  void shutdown(bool force = false);     // 停止；force=true 清零崩溃自动重启预算
  void restart();                        // 重启（先优雅停止再启动）
  CoreState state() const;

signals:
  void logReceived(const QString& line);       // 需求：逐行内核日志
  void coreCrashed(int exitCode);               // 需求：崩溃 / 启动失败通知（启动失败=-1）
  void stateChanged(sparkle::core::CoreState state);
  void coreStarted();
  void coreStopped();
  void controllerListenError(const QString& message);
  void tunPermissionError();

private:
  // 真正的启动路径：解析二进制 → 存在性检查 → 组装参数/环境 → QProcess::start。
  // 返回 true 表示"已接收启动请求"；spawn 是否成功由 started/errorOccurred 异步回报。
  bool launch(const QString& configPath);

  // service 模式（detached + TCP 控制器 + pid 文件 + 重连）：
  bool launchDetached(const QString& configPath);
  void startReadyPoll();
  void pollDetachedReady();
  void stopDetachedProcess();
  void completeDetachedStop();
  QProcessEnvironment buildEnvironment() const;

  void setState(CoreState state);
  void onProcessStarted();
  void onStdOutLine(const QString& line);
  void onStdErrLine(const QString& line);
  void onProcessFinished(int exitCode, QProcess::ExitStatus status);
  void onProcessError(QProcess::ProcessError error);
  void completeInitialization();

  ConfigManager* config_;
  RuntimeConfigFactory* factory_;
  MihomoApiClient* api_;
  LogManager* log_;
  std::unique_ptr<CoreProcessController> process_;  // RAII：间接持有 QProcess

  CoreState state_ = CoreState::Stopped;
  int restartBudget_ = 10;         // 崩溃自动重启预算（对应原 restartBudget = 10）
  bool stoppingRequested_ = false; // 区分"主动停止"与"崩溃/异常退出"
  bool restartRequested_ = false;  // 主动 restart() 标记

  // provider 就绪追踪（startup-chain.ts 的 createProviderInitializationTracker）：
  // 运行配置中声明的 rule/proxy providers（小写归一），全部 "Start initial provider" 后放行就绪。
  std::set<QString> pendingProviders_;
  bool hasProviders_ = false;

  // service（detached）模式状态：pid + 就绪探测。
  qint64 detachedPid_ = 0;
  QTimer readyPollTimer_;
  int readyPollAttempts_ = 0;
  bool readyPollInFlight_ = false;

  // detached stop is asynchronous; keep its state on the manager so an old
  // stop callback can never act on a process started by a later restart.
  QTimer detachedStopTimer_;
  qint64 detachedStopPid_ = 0;
  int detachedStopAttempts_ = 0;
  bool detachedStopKillSent_ = false;
  bool suppressDetachedStoppedSignal_ = false;
};

}  // namespace sparkle::core