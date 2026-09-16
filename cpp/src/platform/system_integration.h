#pragma once

#include <QString>

namespace sparkle::platform {

// ===== 开机自启（登录项）=====

struct AutostartResult {
  bool ok = false;
  QString message;
};

// 注册/注销本应用到系统登录项。
AutostartResult setAutostartEnabled(bool enable);
bool isAutostartEnabled();

// ===== MITM CA 根证书导入系统信任库 =====

// 将 PEM 格式根证书安装到当前用户信任库（改名 name）。
bool installRootCertificate(const QString& certPem, const QString& name, QString* error = nullptr);
bool isRootCertificateInstalled(const QString& name);

// ===== TUN / 虚拟网卡 =====

// 当前平台是否有可用的 TUN 能力（macOS/Linux 由内核提供；Windows 依赖 wintun.dll）。
bool isTunSupportAvailable();
bool installTunDriver(QString* error = nullptr);

}  // namespace sparkle::platform