#include "system_proxy.h"

#if defined(Q_OS_MACOS)

#include <QRegularExpression>
#include <QProcess>

namespace sparkle::platform {
namespace {

struct ProcessResult {
  int exitCode = -1;
  QByteArray output;
};

ProcessResult runCommand(const QString& program, const QStringList& arguments,
                         int timeoutMs = 3000) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished(500);
    return {};
  }
  return {process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
          process.readAllStandardOutput()};
}

QString defaultService() {
  // route -n get default 给出当前默认接口，例如 en0；再映射到
  // networksetup 的网络服务名（Wi-Fi、USB 10/100/1000 LAN 等）。
  const ProcessResult route = runCommand(
      QStringLiteral("route"), {QStringLiteral("-n"), QStringLiteral("get"),
                                 QStringLiteral("default")});
  if (route.exitCode != 0) return {};

  const QRegularExpression interfacePattern(
      QStringLiteral("(?:interface|ifscope):\\s*([A-Za-z0-9._-]+)"));
  const QRegularExpressionMatch interfaceMatch =
      interfacePattern.match(QString::fromUtf8(route.output));
  if (!interfaceMatch.hasMatch()) return {};
  const QString interfaceName = interfaceMatch.captured(1);

  const ProcessResult services = runCommand(
      QStringLiteral("networksetup"), {QStringLiteral("-listnetworkserviceorder")});
  if (services.exitCode != 0) return {};

  const QString serviceText = QString::fromUtf8(services.output);
  const QRegularExpression servicePattern(
      QStringLiteral("^([^()\\n]+)\\s+\\(Hardware Port:.*Device: ") +
          QRegularExpression::escape(interfaceName) + QStringLiteral("\\)"),
      QRegularExpression::MultilineOption);
  const QRegularExpressionMatch serviceMatch = servicePattern.match(serviceText);
  if (serviceMatch.hasMatch()) return serviceMatch.captured(1).trimmed();

  // 虚拟网卡或非标准输出格式时，选择第一个可用服务作为保底，避免向
  // networksetup 传空服务名导致所有操作静默失败。
  const ProcessResult allServices = runCommand(
      QStringLiteral("networksetup"), {QStringLiteral("-listallnetworkservices")});
  for (const QString& line : QString::fromUtf8(allServices.output).split(QLatin1Char('\n'))) {
    const QString service = line.trimmed();
    if (!service.isEmpty() && !service.startsWith(QLatin1Char('*')) &&
        service != QStringLiteral("An asterisk (*) denotes that a network service is disabled.")) {
      return service;
    }
  }
  return {};
}

bool runNetworksetup(const QStringList& arguments) {
  return runCommand(QStringLiteral("networksetup"), arguments).exitCode == 0;
}

class SystemProxyMac final : public ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    const QString service = defaultService();
    if (service.isEmpty()) return;
    const QString portText = QString::number(port);
    runNetworksetup({QStringLiteral("-setwebproxy"), service, host, portText});
    runNetworksetup({QStringLiteral("-setsecurewebproxy"), service, host, portText});

    QStringList arguments{QStringLiteral("-setproxybypassdomains"), service};
    arguments.append(bypass);
    runNetworksetup(arguments);
    // 清理上一次可能残留的 PAC 配置。
    runNetworksetup({QStringLiteral("-setautoproxystate"), service, QStringLiteral("off")});
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    const QString service = defaultService();
    if (service.isEmpty()) return;
    runNetworksetup({QStringLiteral("-setautoproxyurl"), service, pacUrl.toString()});
    runNetworksetup({QStringLiteral("-setautoproxystate"), service, QStringLiteral("on")});
    runNetworksetup({QStringLiteral("-setwebproxystate"), service, QStringLiteral("off")});
    runNetworksetup({QStringLiteral("-setsecurewebproxystate"), service, QStringLiteral("off")});
  }

  void clearProxy() override {
    const QString service = defaultService();
    if (service.isEmpty()) return;
    runNetworksetup({QStringLiteral("-setwebproxystate"), service, QStringLiteral("off")});
    runNetworksetup({QStringLiteral("-setsecurewebproxystate"), service, QStringLiteral("off")});
    runNetworksetup({QStringLiteral("-setautoproxystate"), service, QStringLiteral("off")});
  }

  ProxyStatus status() override {
    const QString service = defaultService();
    if (service.isEmpty()) return ProxyStatus::Disabled;
    const QString web = QString::fromUtf8(runCommand(
        QStringLiteral("networksetup"), {QStringLiteral("-getwebproxy"), service}).output);
    if (web.contains(QRegularExpression(QStringLiteral("^Enabled:\\s*Yes"),
                                         QRegularExpression::MultilineOption))) {
      return ProxyStatus::Manual;
    }
    const QString pac = QString::fromUtf8(runCommand(
        QStringLiteral("networksetup"), {QStringLiteral("-getautoproxyurl"), service}).output);
    if (pac.contains(QRegularExpression(QStringLiteral("^Enabled:\\s*Yes"),
                                         QRegularExpression::MultilineOption))) {
      return ProxyStatus::Auto;
    }
    return ProxyStatus::Disabled;
  }

  void setGuardEnabled(bool, bool) override {
    // 网络服务变更监听属于可选增强能力，基础代理启停不依赖它。
  }
};

}  // namespace

std::unique_ptr<ISystemProxy> SystemProxyFactory::create() {
  return std::make_unique<SystemProxyMac>();
}

}  // namespace sparkle::platform

#endif  // Q_OS_MACOS
