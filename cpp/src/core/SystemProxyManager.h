#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace sparkle::core {
class ConfigManager;
class PacServer;
}  // namespace sparkle::core

namespace sparkle::platform {
class ISystemProxy;
}  // namespace sparkle::platform

namespace sparkle::core {

// 系统代理管理器的跨平台门面。
//
// 该类只负责代理策略、PAC 生命周期和状态信号；Windows/macOS/Linux 的系统设置
// 由 ISystemProxy 的平台实现负责，避免核心逻辑依赖任何平台 API。
class SystemProxyManager final : public QObject {
  Q_OBJECT
public:
  SystemProxyManager(ConfigManager* config, std::unique_ptr<platform::ISystemProxy> backend,
                     QObject* parent = nullptr);
  ~SystemProxyManager() override;

  // enable=true 按配置启用手动代理或 PAC；enable=false 清理所有系统代理设置。
  void setProxy(bool enable);
  void clearProxy();

  // 与旧调用方兼容的别名。
  void disable();

  bool isProxyEnabled() const;
  bool isEnabled() const;

signals:
  void proxyStateChanged(bool enabled);

private:
  void applyProxy(int generation, bool enable);
  QStringList defaultBypass() const;
  unsigned short mixedPort() const;
  void publishState(bool enabled);

  ConfigManager* config_ = nullptr;  // 非 owning，由 AppController 持有
  std::unique_ptr<platform::ISystemProxy> backend_;
  std::unique_ptr<PacServer> pacServer_;

  int generation_ = 0;
  bool desiredEnabled_ = false;
  bool enabled_ = false;
  QTimer retryTimer_;
};

}  // namespace sparkle::core
