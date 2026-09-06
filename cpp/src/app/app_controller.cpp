#include "app_controller.h"

#include <cstdio>
#include <exception>

#include "config_manager.h"
#include "core_manager.h"
#include "log_manager.h"
#include "main_window.h"
#include "MITMManager.h"
#include "mihomo_api_client.h"
#include "pages/logs_page.h"
#include "pages/proxies_page.h"
#include "pages/rules_page.h"
#include "runtime_config_factory.h"
#include "system_proxy.h"
#include "system_proxy_manager.h"

namespace sparkle::core {

AppController::AppController(QObject* parent, bool createWidgetWindow) : QObject(parent) {
  log_ = std::make_unique<LogManager>();
  config_ = std::make_unique<ConfigManager>();
  factory_ = std::make_unique<RuntimeConfigFactory>(config_.get());
  api_ = std::make_unique<MihomoApiClient>(config_.get(), log_.get());
  core_ = std::make_unique<CoreManager>(config_.get(), factory_.get(), api_.get(), log_.get());
  sysProxy_ =
      std::make_unique<SystemProxyManager>(config_.get(), platform::SystemProxyFactory::create());
  mitm_ = std::make_unique<MITMManager>(config_.get(), log_.get());
  connect(core_.get(), &CoreManager::coreStarted, mitm_.get(), [this] {
    if (config_->mitmEnabled() && !mitm_->isRunning()) mitm_->start(config_->mitmPort());
  });
  connect(core_.get(), &CoreManager::coreStopped, mitm_.get(), [this] {
    if (mitm_->isRunning()) mitm_->stop();
  });
  connect(config_.get(), &ConfigManager::mitmConfigChanged, mitm_.get(), [this] {
    if (config_->mitmEnabled() && core_->state() == CoreState::Running) {
      if (!mitm_->isRunning()) {
        mitm_->start(config_->mitmPort());
      } else if (mitm_->port() != config_->mitmPort()) {
        mitm_->stop();
        mitm_->start(config_->mitmPort());
      }
    } else if (!config_->mitmEnabled() && mitm_->isRunning()) {
      mitm_->stop();
    }
  });
  if (createWidgetWindow) {
    window_ = std::make_unique<ui::MainWindow>();
    window_->proxiesPage()->setApi(api_.get());
    window_->rulesPage()->setApi(api_.get());
    window_->logsPage()->setLogManager(log_.get());
    window_->logsPage()->refresh();
    connect(window_->proxiesPage(), &ui::ProxiesPage::refreshError, this,
            [this](const QString& message) {
              log_->appendAppLog(QStringLiteral("[ProxiesPage] %1\\n").arg(message));
            });
    connect(core_.get(), &CoreManager::coreStarted, window_->proxiesPage(),
            &ui::ProxiesPage::refresh);
    connect(core_.get(), &CoreManager::coreStarted, window_->rulesPage(),
            &ui::RulesPage::refresh);
    connect(window_->rulesPage(), &ui::RulesPage::refreshError, this,
            [this](const QString& message) {
              log_->appendAppLog(QStringLiteral("[RulesPage] %1\\n").arg(message));
            });
  }

  // 配置变更 → 重新生成运行时配置 + 重启内核（对应原 patch 后的 restartCore 链路）
  connect(config_.get(), &ConfigManager::reloadRequested, this, [this] { core_->restart(); });
}

AppController::~AppController() { shutdown(); }

void AppController::startup() {
  if (window_) window_->show();
  core_->startup();
}

void AppController::shutdown() {
  if (mitm_) mitm_->stop();
  if (sysProxy_) sysProxy_->disable();
  if (core_) core_->shutdown(true);
  if (window_) window_->close();
}

LogManager* AppController::logManager() const { return log_.get(); }
ConfigManager* AppController::configManager() const { return config_.get(); }
CoreManager* AppController::coreManager() const { return core_.get(); }
MihomoApiClient* AppController::apiClient() const { return api_.get(); }
SystemProxyManager* AppController::systemProxyManager() const { return sysProxy_.get(); }
MITMManager* AppController::mitmManager() const { return mitm_.get(); }
ui::MainWindow* AppController::mainWindow() const { return window_.get(); }

namespace {

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
  Q_UNUSED(context);
  const char* prefix = "INFO";
  switch (type) {
    case QtDebugMsg: prefix = "DEBUG"; break;
    case QtWarningMsg: prefix = "WARN"; break;
    case QtCriticalMsg: prefix = "CRITICAL"; break;
    case QtFatalMsg: prefix = "FATAL"; break;
    case QtInfoMsg: prefix = "INFO"; break;
  }
  std::fprintf(stderr, "[Qt:%s] %s\n", prefix, msg.toLocal8Bit().constData());
}

void terminateHandler() {
  // TODO(phase 1): 写入日志文件（spdlog）后再终止；Windows SEH / POSIX 信号处理器。
  std::fprintf(stderr, "[sparkle] uncaught exception, terminating\n");
  std::abort();
}

}  // namespace

void installApplicationHandlers() {
  qInstallMessageHandler(messageHandler);
  std::set_terminate(terminateHandler);
}

}  // namespace sparkle::core