#include "system_proxy.h"

#if defined(Q_OS_WIN)

#include <QSettings>

#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

namespace sparkle::platform {
namespace {

constexpr auto kRegPath =
    R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings)";

void notifySystemSettingsChanged() {
  InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

class SystemProxyWin final : public ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    QSettings reg(kRegPath, QSettings::NativeFormat);
    reg.setValue(QStringLiteral("ProxyEnable"), 1);
    reg.setValue(QStringLiteral("ProxyServer"), QStringLiteral("%1:%2").arg(host).arg(port));
    reg.setValue(QStringLiteral("ProxyOverride"), bypass.join(QLatin1Char(';')));
    notifySystemSettingsChanged();
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    QSettings reg(kRegPath, QSettings::NativeFormat);
    reg.setValue(QStringLiteral("AutoConfigURL"), pacUrl.toString());
    notifySystemSettingsChanged();
  }

  void clearProxy() override {
    QSettings reg(kRegPath, QSettings::NativeFormat);
    reg.setValue(QStringLiteral("ProxyEnable"), 0);
    notifySystemSettingsChanged();
  }

  ProxyStatus status() override {
    QSettings reg(kRegPath, QSettings::NativeFormat);
    return reg.value(QStringLiteral("ProxyEnable"), 0).toInt() ? ProxyStatus::Manual
                                                               : ProxyStatus::Disabled;
  }

  void setGuardEnabled(bool enabled, bool notify) override {
    Q_UNUSED(enabled);
    Q_UNUSED(notify);
    // TODO(phase 2): RegNotifyChangeKeyValue 守护 + 自动恢复
  }
};

}  // namespace

std::unique_ptr<ISystemProxy> SystemProxyFactory::create() {
  return std::make_unique<SystemProxyWin>();
}

}  // namespace sparkle::platform

#endif  // Q_OS_WIN