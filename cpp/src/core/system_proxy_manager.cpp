#include "system_proxy_manager.h"

#include <nlohmann/json.hpp>

#include <QUrl>

#include "config_manager.h"
#include "pac_server.h"
#include "system_proxy.h"

namespace sparkle::core {

SystemProxyManager::SystemProxyManager(ConfigManager* config,
                                       std::unique_ptr<platform::ISystemProxy> backend,
                                       QObject* parent)
    : QObject(parent),
      config_(config),
      backend_(std::move(backend)),
      pacServer_(std::make_unique<PacServer>(this)) {
  retryTimer_.setSingleShot(true);
  retryTimer_.setInterval(5000);
  connect(&retryTimer_, &QTimer::timeout, this, [this] {
    // 端口尚未就绪（内核未起）→ 按最新期望状态重试（代际守卫在 applyProxy 内）。
    applyProxy(generation_, desiredEnabled_);
  });
}

SystemProxyManager::~SystemProxyManager() = default;

QStringList SystemProxyManager::defaultBypass() const {
#if defined(Q_OS_WIN)
  return {QStringLiteral("localhost"),      QStringLiteral("127.*"),
          QStringLiteral("192.168.*"),      QStringLiteral("10.*"),
          QStringLiteral("172.16.*"),       QStringLiteral("172.17.*"),
          QStringLiteral("172.18.*"),       QStringLiteral("172.19.*"),
          QStringLiteral("172.20.*"),       QStringLiteral("172.21.*"),
          QStringLiteral("172.22.*"),       QStringLiteral("172.23.*"),
          QStringLiteral("172.24.*"),       QStringLiteral("172.25.*"),
          QStringLiteral("172.26.*"),       QStringLiteral("172.27.*"),
          QStringLiteral("172.28.*"),       QStringLiteral("172.29.*"),
          QStringLiteral("172.30.*"),       QStringLiteral("172.31.*"),
          QStringLiteral("<local>")};
#elif defined(Q_OS_MACOS)
  return {QStringLiteral("127.0.0.1/8"), QStringLiteral("192.168.0.0/16"),
          QStringLiteral("10.0.0.0/8"), QStringLiteral("172.16.0.0/12"),
          QStringLiteral("localhost"),  QStringLiteral("*.local"),
          QStringLiteral("*.crashlytics.com"), QStringLiteral("<local>")};
#else
  return {QStringLiteral("localhost"),  QStringLiteral(".local"),
          QStringLiteral("127.0.0.1/8"), QStringLiteral("192.168.0.0/16"),
          QStringLiteral("10.0.0.0/8"), QStringLiteral("172.16.0.0/12"),
          QStringLiteral("::1")};
#endif
}

unsigned short SystemProxyManager::mixedPort() const {
  const nlohmann::json controlled = config_->controlledMihomoConfig();
  if (controlled.contains("mixed-port") && controlled["mixed-port"].is_number()) {
    return static_cast<unsigned short>(controlled["mixed-port"].get<int>());
  }
  return 0;
}

void SystemProxyManager::setProxy(bool enable) {
  const int generation = ++generation_;
  desiredEnabled_ = enable;
  retryTimer_.stop();
  applyProxy(generation, enable);
}

void SystemProxyManager::applyProxy(int generation, bool enable) {
  if (generation != generation_) return;   // 已有更新请求：丢弃本次

  if (!backend_) {
    publishState(false);
    return;
  }

  if (!enable) {
    pacServer_->stopServer();
    backend_->clearProxy();
    publishState(false);
    return;
  }

  if (!config_) {
    publishState(false);
    return;
  }

  const SysProxyConfig cfg = config_->sysProxyConfig();
  const unsigned short port = mixedPort();
  const QString host = cfg.host.isEmpty() ? QStringLiteral("127.0.0.1") : cfg.host;

  if (cfg.mode == SysProxyMode::Manual && port != 0) {
    pacServer_->stopServer();
    backend_->setManualProxy(host, port, cfg.bypass.isEmpty() ? defaultBypass() : cfg.bypass);
  } else {
    // auto 模式：起本地 PAC server，系统 Web 代理指向它。
    // 重复启用时先关闭旧监听，确保 listen(0) 重新取得有效端口。
    pacServer_->stopServer();
    pacServer_->setProxyPort(port);
    if (!pacServer_->start(0)) {
      publishState(false);
      if (port == 0) retryTimer_.start();
      return;
    }
    backend_->setAutoProxy(
        QUrl(QStringLiteral("http://127.0.0.1:%1/pac").arg(pacServer_->port())));
  }
  publishState(true);

  // 端口尚未就绪（内核未启动，mixed-port 为 0）→ 5s 后按最新期望重试。
  // 注：原 triggerSysProxy 的"断网 5s 重试"依赖平台后端回报失败；当前 ISystemProxy 为
  // void 返回，仅实现了"端口未就绪"这一可观测的重试源，断网检测留待后后端接口扩展。
  if (port == 0) {
    retryTimer_.start();
  }
}

void SystemProxyManager::clearProxy() { setProxy(false); }

void SystemProxyManager::disable() { clearProxy(); }

void SystemProxyManager::publishState(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  emit proxyStateChanged(enabled_);
}

bool SystemProxyManager::isProxyEnabled() const { return enabled_; }

bool SystemProxyManager::isEnabled() const { return isProxyEnabled(); }

}  // namespace sparkle::core