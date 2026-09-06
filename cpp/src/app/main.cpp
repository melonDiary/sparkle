#include <QApplication>
#include <QDir>
#include <QSystemTrayIcon>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/qqml.h>

#include <nlohmann/json.hpp>

#include "app_controller.h"
#include "config_manager.h"
#include "core_manager.h"
#include "log_manager.h"
#include "models.h"
#include "paths.h"
#include "plugin_manager.h"
#include "script_engine.h"
#include "single_instance.h"
#include "app_model.h"
#include "script_bridge.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("sparkle"));
  QApplication::setApplicationVersion(QStringLiteral("1.26.7"));
  sparkle::core::registerCoreMetatypes();

  const QString exeDir = QFileInfo(app.applicationFilePath()).absolutePath();
  const bool portable = QFileInfo::exists(exeDir + QStringLiteral("/PORTABLE"));
  sparkle::core::Paths::initialize(app.applicationFilePath(), portable);

  sparkle::core::SingleInstance single;
  if (!single.tryAcquire()) {
    single.forwardToPrimary(argc, argv);
    return 0;
  }
  sparkle::core::installApplicationHandlers();

  using namespace sparkle;
  // QML 是唯一的可见 UI；不创建历史 Qt Widgets 主窗口，避免两套 UI 同时存在。
  auto controller = std::make_unique<core::AppController>(nullptr, false);
  core::ScriptEngine scriptEngine(controller->logManager());
  ui::AppModel appModel;
  ui::ScriptBridge scriptBridge;
  appModel.setCoreManager(controller->coreManager());

  if (scriptEngine.initialize()) {
    try {
      scriptEngine.bindCoreManager(controller->coreManager());
      scriptEngine.setUiStatusHandler([&appModel](const QString& message) {
        appModel.setStatusMessage(message);
      });

      const QStringList scriptCandidates = {
          QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("scripts/example.js")),
          QDir(QDir::currentPath()).filePath(QStringLiteral("scripts/example.js")),
          QDir(QDir::currentPath()).filePath(QStringLiteral("cpp/scripts/example.js"))};
      for (const QString& scriptPath : scriptCandidates) {
        if (QFileInfo(scriptPath).isFile()) {
          scriptEngine.loadScript(scriptPath.toStdString());
          break;
        }
      }
    } catch (const core::ScriptError& error) {
      controller->logManager()->appendAppLog(
          QStringLiteral("[main] 示例脚本加载失败：%1\n").arg(QString::fromUtf8(error.what())));
    }
  }

  appModel.setApiClient(controller->apiClient());
  appModel.setLogManager(controller->logManager());
  appModel.setSystemProxyManager(controller->systemProxyManager());
  QObject::connect(&appModel, &ui::AppModel::errorMessage, &appModel,
                   [&appModel](const QString& message) {
                     appModel.setStatusMessage(message);
                   });
  scriptBridge.setScriptEngine(&scriptEngine);
  // 插件沙箱不直接依赖 UI；通过回调把通知交给 Qt 系统托盘，便于测试和
  // 后续替换为原生通知中心实现。
  QSystemTrayIcon trayIcon;
  trayIcon.setToolTip(QStringLiteral("Sparkle"));
  trayIcon.setVisible(true);

  core::PluginManager plugins(QDir(core::Paths::dataDir()).filePath(QStringLiteral("plugins")),
                              controller->logManager(), controller->configManager());
  plugins.setNotificationHandler([&trayIcon](const QString& message) {
    if (trayIcon.isVisible()) {
      trayIcon.showMessage(QStringLiteral("Sparkle 插件"), message,
                           QSystemTrayIcon::Information, 5000);
    }
  });
  plugins.discover();
  plugins.loadAll();
  QObject::connect(controller->coreManager(), &core::CoreManager::coreStarted,
                   &plugins, &core::PluginManager::proxyStarted);
  QObject::connect(controller->coreManager(), &core::CoreManager::coreStopped,
                   &plugins, &core::PluginManager::proxyStopped);
  QObject::connect(controller->coreManager(), &core::CoreManager::stateChanged,
                   &appModel, [&appModel](core::CoreState state) {
                     Q_UNUSED(state);
                     appModel.refresh();
                   });

  // 保留 qmlRegisterType，便于后续 QML 页面直接创建这些可复用类型。
  qmlRegisterType<ui::AppModel>("Sparkle.Models", 1, 0, "AppModel");
  qmlRegisterType<ui::ScriptBridge>("Sparkle.Models", 1, 0, "ScriptBridge");

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appModel"), &appModel);
  engine.rootContext()->setContextProperty(QStringLiteral("scriptBridge"), &scriptBridge);
  const QUrl mainUrl(QStringLiteral("qrc:/qml/MainWindow.qml"));
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [mainUrl] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.load(mainUrl);
  if (engine.rootObjects().isEmpty()) return -1;

  // QML 交互通过 ScriptBridge → ScriptEngine → 脚本函数 → CoreManager。
  controller->startup();
  const int rc = app.exec();
  controller->shutdown();
  return rc;
}
