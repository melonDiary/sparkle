#include "system_proxy.h"

#if defined(Q_OS_LINUX)

#include <QProcess>

namespace sparkle::platform {
namespace {

void gsettings(const QStringList& args) {
  QProcess process;
  process.start(QStringLiteral("gsettings"), args);
  process.waitForFinished(3000);
}

class SystemProxyLinux final : public ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    // GNOME：gsettings org.gnome.system.proxy
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("mode"), QStringLiteral("manual")});
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy.http"),
               QStringLiteral("host"), host});
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy.http"),
               QStringLiteral("port"), QString::number(port)});
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("ignore-hosts"), bypass.join(QLatin1Char(','))});
    // 非 GNOME 环境变量 fallback（仅影响本进程），TODO(phase 1)
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("mode"), QStringLiteral("auto")});
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("autoconfig-url"), pacUrl.toString()});
  }

  void clearProxy() override {
    gsettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
               QStringLiteral("mode"), QStringLiteral("none")});
  }

  ProxyStatus status() override { return ProxyStatus::Disabled; } // TODO(phase 2): 查询

  void setGuardEnabled(bool enabled, bool notify) override {
    Q_UNUSED(enabled);
    Q_UNUSED(notify);
    // TODO(phase 2): dconf watch 守护
  }
};

}  // namespace

std::unique_ptr<ISystemProxy> SystemProxyFactory::create() {
  return std::make_unique<SystemProxyLinux>();
}

}  // namespace sparkle::platform

#endif  // Q_OS_LINUX