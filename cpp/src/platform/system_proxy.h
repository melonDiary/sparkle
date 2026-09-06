#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>
#include <memory>

namespace sparkle::platform {

enum class ProxyStatus { Disabled, Manual, Auto };

// 平台无关系统代理后端（对应原 sys/sysproxy.ts + sysproxy-go 职责）。
class ISystemProxy {
public:
  virtual ~ISystemProxy() = default;
  virtual void setManualProxy(const QString& host, unsigned short port,
                              const QStringList& bypass) = 0;
  virtual void setAutoProxy(const QUrl& pacUrl) = 0;
  virtual void clearProxy() = 0;
  virtual ProxyStatus status() = 0;
  virtual void setGuardEnabled(bool enabled, bool notify) = 0; // P2
};

// 工厂：按编译平台返回实现（定义在各自的 platform 源文件中）。
class SystemProxyFactory {
public:
  static std::unique_ptr<ISystemProxy> create();
};

}  // namespace sparkle::platform