#include "system_proxy.h"

#if defined(Q_OS_WIN)

#include <QSettings>

#include <windows.h>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

namespace sparkle::platform {
namespace {

constexpr auto kInternetSettings =
    R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Internet Settings)";

void notifySystemSettingsChanged() {
  InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
  InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

class SystemProxyWindows final : public ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    QSettings settings(kInternetSettings, QSettings::NativeFormat);
    settings.setValue(QStringLiteral("ProxyEnable"), 1);
    settings.setValue(QStringLiteral("ProxyServer"),
                      QStringLiteral("%1:%2").arg(host).arg(port));
    settings.setValue(QStringLiteral("ProxyOverride"), bypass.join(QLatin1Char(';')));
    // 手动模式和 PAC 模式互斥，避免旧 AutoConfigURL 继续影响浏览器。
    settings.setValue(QStringLiteral("AutoConfigURL"), QString());
    settings.setValue(QStringLiteral("AutoDetect"), 0);
    settings.sync();
    notifySystemSettingsChanged();
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    QSettings settings(kInternetSettings, QSettings::NativeFormat);
    settings.setValue(QStringLiteral("ProxyEnable"), 0);
    settings.setValue(QStringLiteral("AutoConfigURL"), pacUrl.toString());
    settings.setValue(QStringLiteral("AutoDetect"), 0);
    settings.sync();
    notifySystemSettingsChanged();
  }

  void clearProxy() override {
    QSettings settings(kInternetSettings, QSettings::NativeFormat);
    settings.setValue(QStringLiteral("ProxyEnable"), 0);
    settings.setValue(QStringLiteral("AutoConfigURL"), QString());
    settings.setValue(QStringLiteral("AutoDetect"), 0);
    settings.sync();
    notifySystemSettingsChanged();
  }

  ProxyStatus status() override {
    QSettings settings(kInternetSettings, QSettings::NativeFormat);
    if (settings.value(QStringLiteral("ProxyEnable"), 0).toInt() != 0) {
      return ProxyStatus::Manual;
    }
    if (!settings.value(QStringLiteral("AutoConfigURL")).toString().isEmpty()) {
      return ProxyStatus::Auto;
    }
    return ProxyStatus::Disabled;
  }

  void setGuardEnabled(bool, bool) override {
    // 目前由 SystemProxyManager 在应用生命周期内维护状态；注册表变更监听
    // 可在后续加入，不影响基础 set/clear API。
  }
};

}  // namespace

std::unique_ptr<ISystemProxy> SystemProxyFactory::create() {
  return std::make_unique<SystemProxyWindows>();
}

}  // namespace sparkle::platform

#endif  // Q_OS_WIN
