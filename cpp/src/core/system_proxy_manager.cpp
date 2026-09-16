#include "system_proxy_manager.h"

#include <nlohmann/json.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUrl>

#include "config_manager.h"
#include "pac_server.h"
#include "paths.h"
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

  guardTimer_.setSingleShot(true);
  guardTimer_.setInterval(5000);
  connect(&guardTimer_, &QTimer::timeout, this, [this] { guardTick(); });

  // guard 开关在运行期变化时同步守卫状态，无需重新 setProxy。
  if (config_) {
    connect(config_, &ConfigManager::appConfigChanged, this, [this] { updateGuard(); });
  }
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
  updateGuard();
}

void SystemProxyManager::applyProxy(int generation, bool enable) {
  if (generation != generation_) return;   // 已有更新请求：丢弃本次

  if (!backend_) {
    publishState(false);
    return;
  }

  if (!enable) {
    clearProxyMarker();   // 正常关闭：去掉"代理归本进程所有"的标记
    pacServer_->stopServer();
    const bool ok = backend_->clearProxy();
    if (!ok) qWarning().noquote() << QStringLiteral("[SystemProxyManager] 系统代理关闭失败");
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
  bool ok = false;

  if (cfg.mode == SysProxyMode::Manual && port != 0) {
    writeProxyMarker();    // 即将接管系统代理：落盘 owner 标记，供崩溃后还原
    pacServer_->stopServer();
    const QStringList bypass = cfg.bypass.isEmpty() ? defaultBypass() : cfg.bypass;
    ok = backend_->setManualProxy(host, port, bypass);
    if (ok) {
      appliedMode_ = platform::ProxyStatus::Manual;
      appliedHost_ = host;
      appliedPort_ = port;
      appliedBypass_ = bypass;
      appliedPacUrl_.clear();
    }
  } else {
    // auto 模式（以及端口未就绪时的兜底）：起本地 PAC server，系统 Web 代理指向它。
    writeProxyMarker();
    pacServer_->stopServer();
    pacServer_->setProxyPort(port);
    if (!pacServer_->start(0)) {
      publishState(false);
      if (port == 0) retryTimer_.start();
      return;
    }
    const QUrl pacUrl =
        QUrl(QStringLiteral("http://127.0.0.1:%1/pac").arg(pacServer_->port()));
    ok = backend_->setAutoProxy(pacUrl);
    if (ok) {
      appliedMode_ = platform::ProxyStatus::Auto;
      appliedPacUrl_ = pacUrl.toString();
    }
  }

  if (!ok) {
    // 后端申请系统代理失败：清理 owner 标记、尽量回滚半设置状态，并如实上报 + 延迟重试。
    clearProxyMarker();
    appliedMode_ = platform::ProxyStatus::Disabled;
    if (cfg.mode == SysProxyMode::Manual) {
      backend_->clearProxy();       // 回滚可能遗留的半设置手动代理
    } else {
      pacServer_->stopServer();
    }
    qWarning().noquote() << QStringLiteral("[SystemProxyManager] 设置系统代理失败，已回滚");
    publishState(false);
    retryTimer_.start();            // 断网/瞬时失败 → 5s 后按最新期望重试
    return;
  }

  publishState(true);

  // 端口尚未就绪（内核未启动，mixed-port 为 0）→ 5s 后重试以拿到真实端口。
  if (port == 0) {
    retryTimer_.start();
  }
}

void SystemProxyManager::clearProxy() { setProxy(false); }

void SystemProxyManager::disable() { clearProxy(); }

void SystemProxyManager::updateGuard() {
  const bool guardOn = desiredEnabled_ && config_ && config_->sysProxyConfig().guard;
  if (guardOn) {
    if (!guardTimer_.isActive()) guardTimer_.start();
  } else {
    guardTimer_.stop();
  }
}

void SystemProxyManager::guardTick() {
  const bool guardOn = config_ && config_->sysProxyConfig().guard;
  if (!desiredEnabled_ || !guardOn || !backend_) return;  // 不再续期，停止轮询

  // 系统代理被外部关闭 → 用上次成功参数原样恢复到后端（不重启 PAC，避免端口抖动）。
  if (backend_->status() == platform::ProxyStatus::Disabled) {
    bool ok = false;
    if (appliedMode_ == platform::ProxyStatus::Manual) {
      ok = backend_->setManualProxy(appliedHost_, appliedPort_, appliedBypass_);
    } else if (appliedMode_ == platform::ProxyStatus::Auto && !appliedPacUrl_.isEmpty()) {
      ok = backend_->setAutoProxy(QUrl(appliedPacUrl_));
    }
    if (ok) {
      qWarning().noquote()
          << QStringLiteral("[SystemProxyManager] 检测到系统代理被外部关闭，已自动恢复");
      publishState(true);
    } else {
      qWarning().noquote() << QStringLiteral("[SystemProxyManager] 系统代理自动恢复失败");
      appliedMode_ = platform::ProxyStatus::Disabled;
    }
  }
  guardTimer_.start();  // 续期
}

QString SystemProxyManager::proxyMarkerPath() const {
  return QDir(Paths::dataDir()).filePath(QStringLiteral("sysproxy-owner.json"));
}

void SystemProxyManager::writeProxyMarker() {
  nlohmann::json j;
  j["pid"] = static_cast<qint64>(QCoreApplication::applicationPid());
  QFile f(proxyMarkerPath());
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QByteArray::fromStdString(j.dump()));
  }
}

void SystemProxyManager::clearProxyMarker() { QFile::remove(proxyMarkerPath()); }

void SystemProxyManager::recoverIfPreviousCrashed() {
  const QString path = proxyMarkerPath();
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return;   // 无标记：上次干净退出

  const auto j = nlohmann::json::parse(f.readAll().toStdString(), nullptr, false);
  f.close();

  const qint64 ownerPid =
      (j.is_object() && j.contains("pid") && j["pid"].is_number_integer())
          ? j["pid"].get<qint64>()
          : 0;
  if (ownerPid == static_cast<qint64>(QCoreApplication::applicationPid())) return;

  // 标记存在但归属非本进程 → 上次异常退出（崩溃/强杀）遗留的系统代理，先还原。
  qWarning().noquote()
      << "[SystemProxyManager] 检测到上次异常退出遗留的系统代理，正在还原...";
  if (backend_) backend_->clearProxy();
  clearProxyMarker();
}

void SystemProxyManager::publishState(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  emit proxyStateChanged(enabled_);
}

bool SystemProxyManager::isProxyEnabled() const { return enabled_; }

bool SystemProxyManager::isEnabled() const { return isProxyEnabled(); }

}  // namespace sparkle::core