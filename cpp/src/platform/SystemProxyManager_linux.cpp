#include "system_proxy.h"

#if defined(Q_OS_LINUX)

#include <QProcess>

namespace sparkle::platform {
namespace {

bool runGSettings(const QStringList& arguments, QByteArray* output = nullptr) {
  QProcess process;
  process.start(QStringLiteral("gsettings"), arguments);
  if (!process.waitForFinished(3000)) {
    process.kill();
    process.waitForFinished(500);
    return false;
  }
  if (output) *output = process.readAllStandardOutput().trimmed();
  return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void setString(const QString& schema, const QString& key, const QString& value) {
  runGSettings({QStringLiteral("set"), schema, key, value});
}

class SystemProxyLinux final : public ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("mode"),
              QStringLiteral("manual"));
    setString(QStringLiteral("org.gnome.system.proxy.http"), QStringLiteral("host"), host);
    setString(QStringLiteral("org.gnome.system.proxy.http"), QStringLiteral("port"),
              QString::number(port));
    setString(QStringLiteral("org.gnome.system.proxy.https"), QStringLiteral("host"), host);
    setString(QStringLiteral("org.gnome.system.proxy.https"), QStringLiteral("port"),
              QString::number(port));
    // ignore-hosts 是 GSettings 数组，不能传逗号拼接的裸字符串。
    QStringList values;
    values.reserve(bypass.size());
    for (const QString& item : bypass) values << QStringLiteral("'%1'").arg(item);
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("ignore-hosts"),
              QStringLiteral("[%1]").arg(values.join(QStringLiteral(", "))));
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"),
              QString());
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("mode"),
              QStringLiteral("auto"));
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"),
              pacUrl.toString());
  }

  void clearProxy() override {
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("mode"),
              QStringLiteral("none"));
    setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"),
              QString());
  }

  ProxyStatus status() override {
    QByteArray mode;
    if (!runGSettings({QStringLiteral("get"), QStringLiteral("org.gnome.system.proxy"),
                       QStringLiteral("mode")}, &mode)) {
      return ProxyStatus::Disabled;
    }
    mode = mode.trimmed().toLower();
    if (mode == "manual") return ProxyStatus::Manual;
    if (mode == "auto") return ProxyStatus::Auto;
    return ProxyStatus::Disabled;
  }

  void setGuardEnabled(bool, bool) override {
    // 平台代理守护属于可选能力，基础代理 API 不依赖它。
  }
};

}  // namespace

std::unique_ptr<ISystemProxy> SystemProxyFactory::create() {
  return std::make_unique<SystemProxyLinux>();
}

}  // namespace sparkle::platform

#endif  // Q_OS_LINUX
