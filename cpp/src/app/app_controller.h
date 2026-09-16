#pragma once

#include <QObject>
#include <memory>

namespace sparkle::core {

class LogManager;
class ConfigManager;
class RuntimeConfigFactory;
class MihomoApiClient;
class CoreManager;
class SystemProxyManager;
class MITMManager;
class SubscriptionManager;

// 组合根（对应原 main/index.ts 的编排）：拥有全部 manager 与主窗口，串联信号，驱动启停序列。
// 物理上位于 app 层（依赖 core + ui + platform）。
class AppController final : public QObject {
  Q_OBJECT
public:
  explicit AppController(QObject* parent = nullptr);
  ~AppController() override;

  void startup();
  void shutdown();

  LogManager* logManager() const;
  ConfigManager* configManager() const;
  CoreManager* coreManager() const;
  MihomoApiClient* apiClient() const;
  SystemProxyManager* systemProxyManager() const;
  MITMManager* mitmManager() const;
  SubscriptionManager* subscriptionManager() const;

private:
  std::unique_ptr<LogManager> log_;
  std::unique_ptr<ConfigManager> config_;
  std::unique_ptr<RuntimeConfigFactory> factory_;
  std::unique_ptr<MihomoApiClient> api_;
  std::unique_ptr<CoreManager> core_;
  std::unique_ptr<SystemProxyManager> sysProxy_;
  std::unique_ptr<MITMManager> mitm_;
  std::unique_ptr<SubscriptionManager> subscription_;
};

// 全局消息/异常钩子（对应需求 1）。
void installApplicationHandlers();

}  // namespace sparkle::core