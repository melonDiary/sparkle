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

bool setString(const QString& schema, const QString& key, const QString& value) {
  return runGSettings({QStringLiteral("set"), schema, key, value});
}

class SystemProxyLinux final : public ISystemProxy {
public:
  bool setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    bool ok = runGSettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
                            QStringLiteral("mode"), QStringLiteral("manual")});
    ok = setString(QStringLiteral("org.gnome.system.proxy.http"), QStringLiteral("host"),
                   host) &&
         ok;
    ok = setString(QStringLiteral("org.gnome.system.proxy.http"), QStringLiteral("port"),
                   QString::number(port)) &&
         ok;
    ok = setString(QStringLiteral("org.gnome.system.proxy.https"), QStringLiteral("host"),
                   host) &&
         ok;
    ok = setString(QStringLiteral("org.gnome.system.proxy.https"), QStringLiteral("port"),
                   QString::number(port)) &&
         ok;
    // ignore-hosts 是 GSettings 数组，不能传逗号拼接的裸字符串。
    QStringList values;
    values.reserve(bypass.size());
    for (const QString& item : bypass) values << QStringLiteral("'%1'").arg(item);
    ok = setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("ignore-hosts"),
                   QStringLiteral("[%1]").arg(values.join(QStringLiteral(", ")))) &&
         ok;
    ok = setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"),
                   QString()) &&
         ok;
    return ok;
  }

  bool setAutoProxy(const QUrl& pacUrl) override {
    bool ok = runGSettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
                            QStringLiteral("mode"), QStringLiteral("auto")});
    ok = setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"),
                   pacUrl.toString()) &&
         ok;
    return ok;
  }

  bool clearProxy() override {
    bool ok = runGSettings({QStringLiteral("set"), QStringLiteral("org.gnome.system.proxy"),
                            QStringLiteral("mode"), QStringLiteral("none")});
    ok = setString(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("autoconfig-url"),
                   QString()) &&
         ok;
    return ok;
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
