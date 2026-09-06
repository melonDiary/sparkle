#include "plugin_manager.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

#include "config_manager.h"
#include "core_manager.h"
#include "log_manager.h"
#include "plugin_sandbox.h"

namespace sparkle::core {

PluginManager::PluginManager(QString pluginsDirectory, LogManager* log, ConfigManager* config,
                             QObject* parent)
    : QObject(parent),
      pluginsDirectory_(QDir::cleanPath(std::move(pluginsDirectory))),
      log_(log),
      config_(config) {}

PluginManager::~PluginManager() { unloadAll(); }

QString PluginManager::pluginsDirectory() const { return pluginsDirectory_; }

void PluginManager::setNotificationHandler(NotificationHandler handler) {
  notificationHandler_ = std::move(handler);
  for (auto& plugin : discovered_) plugin->setNotificationHandler(notificationHandler_);
}

void PluginManager::discover() {
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path directory(pluginsDirectory_.toStdString());
  fs::create_directories(directory, error);
  if (error) {
    if (log_) log_->appendAppLog(QStringLiteral("[PluginManager] 创建插件目录失败：%1\\n")
                                     .arg(QString::fromStdString(error.message())));
    return;
  }

  std::vector<fs::path> files;
  for (fs::directory_iterator it(directory, error), end; it != end && !error;
       it.increment(error)) {
    // 不跟随符号链接，防止 plugins/ 中的链接把插件入口指向目录之外。
    if (it->is_symlink(error) || error) continue;
    if (!it->is_regular_file(error) || error) continue;
    const fs::path& path = it->path();
    if (path.extension() == ".js") files.push_back(path);
  }
  if (error) {
    if (log_) log_->appendAppLog(QStringLiteral("[PluginManager] 扫描插件目录失败：%1\\n")
                                     .arg(QString::fromStdString(error.message())));
    return;
  }
  std::sort(files.begin(), files.end());

  unloadAll();
  discovered_.clear();
  for (const fs::path& file : files) {
    const QString path = QString::fromUtf8(file.string().c_str());
    discovered_.push_back(
        std::make_unique<PluginSandbox>(path, log_, config_, notificationHandler_));
  }
}

int PluginManager::loadAll() {
  int loaded = 0;
  for (auto& plugin : discovered_) {
    if (plugin->load()) {
      ++loaded;
      emit pluginLoaded(plugin->id());
    } else {
      emit pluginError(plugin->filePath(), plugin->lastError());
    }
  }
  return loaded;
}

void PluginManager::unloadAll() {
  for (auto& plugin : discovered_) {
    if (plugin->isLoaded()) {
      plugin->unload();
      emit pluginUnloaded(plugin->id());
    }
  }
}

void PluginManager::proxyStarted() {
  for (auto& plugin : discovered_) {
    if (!plugin->isLoaded()) continue;
    const QString previousError = plugin->lastError();
    plugin->onProxyStart();
    if (plugin->lastError() != previousError) {
      emit pluginError(plugin->filePath(), plugin->lastError());
    }
  }
}

void PluginManager::proxyStopped() {
  for (auto& plugin : discovered_) {
    if (!plugin->isLoaded()) continue;
    const QString previousError = plugin->lastError();
    plugin->onProxyStop();
    if (plugin->lastError() != previousError) {
      emit pluginError(plugin->filePath(), plugin->lastError());
    }
  }
}

QStringList PluginManager::loadedPluginIds() const {
  QStringList ids;
  for (const auto& plugin : discovered_) {
    if (plugin->isLoaded()) ids << plugin->id();
  }
  return ids;
}

}  // namespace sparkle::core
