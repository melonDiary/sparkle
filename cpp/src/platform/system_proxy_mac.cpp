#include "system_proxy.h"

#if defined(Q_OS_MACOS)

#include <QProcess>

namespace sparkle::platform {
namespace {

// 解析默认网络服务名（等价 route -n get default → interface → networksetup 服务）。
QString defaultService() {
  // TODO(phase 2): 复刻 core/network.ts 的 getDefaultService：route 找 interface，
  //   networksetup -listnetworkserviceorder 映射到服务名。骨架阶段返回空，等价"全部服务"。
  return QString();
}

void runNetworksetup(const QStringList& args) {
  QProcess process;
  process.start(QStringLiteral("networksetup"), args);
  process.waitForFinished(3000);
}

class SystemProxyMac final : public ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    const QString service = defaultService();
    const QString portStr = QString::number(port);
    runNetworksetup({QStringLiteral("-setwebproxy"), service, host, portStr});
    runNetworksetup({QStringLiteral("-setsecurewebproxy"), service, host, portStr});
    runNetworksetup({QStringLiteral("-setproxybypassdomains"), service,
                     bypass.join(QLatin1Char(','))});
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    const QString service = defaultService();
    runNetworksetup({QStringLiteral("-setautoproxyurl"), service, pacUrl.toString()});
  }

  void clearProxy() override {
    const QString service = defaultService();
    runNetworksetup({QStringLiteral("-setwebproxystate"), service, QStringLiteral("off")});
    runNetworksetup({QStringLiteral("-setsecurewebproxystate"), service, QStringLiteral("off")});
    runNetworksetup({QStringLiteral("-setautoproxystate"), service, QStringLiteral("off")});
  }

  ProxyStatus status() override { return ProxyStatus::Disabled; } // TODO(phase 2): 查询

  void setGuardEnabled(bool enabled, bool notify) override {
    Q_UNUSED(enabled);
    Q_UNUSED(notify);
    // TODO(phase 2): SystemConfiguration SCDynamicStore 回调守护
  }
};

}  // namespace

std::unique_ptr<ISystemProxy> SystemProxyFactory::create() {
  return std::make_unique<SystemProxyMac>();
}

}  // namespace sparkle::platform

#endif  // Q_OS_MACOS