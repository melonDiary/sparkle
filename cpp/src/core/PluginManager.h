#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

#include "IPlugin.h"

namespace sparkle::core {
class ConfigManager;
class CoreManager;
class LogManager;
class PluginSandbox;

// 插件管理器：扫描 plugins/*.js，并让每个插件在独立沙箱中运行。
class PluginManager final : public QObject {
  Q_OBJECT
public:
  using NotificationHandler = std::function<void(const QString& message)>;

  explicit PluginManager(QString pluginsDirectory, LogManager* log = nullptr,
                         ConfigManager* config = nullptr, QObject* parent = nullptr);
  ~PluginManager() override;

  void discover();
  int loadAll();
  void unloadAll();
  void proxyStarted();
  void proxyStopped();

  QString pluginsDirectory() const;
  QStringList loadedPluginIds() const;
  void setNotificationHandler(NotificationHandler handler);

signals:
  void pluginLoaded(const QString& id);
  void pluginUnloaded(const QString& id);
  void pluginError(const QString& path, const QString& message);

private:
  QString pluginsDirectory_;
  LogManager* log_ = nullptr;
  ConfigManager* config_ = nullptr;
  NotificationHandler notificationHandler_;
  std::vector<std::unique_ptr<PluginSandbox>> discovered_;
};

}  // namespace sparkle::core
