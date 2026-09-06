#pragma once

#include <QString>

#include "models.h"

namespace sparkle::core {

// 路径解析器：初始化后静态提供全部路径（对应原 utils/dirs.ts）。
// 数据目录 = 便携模式 exe/data，否则平台 userData（与 Electron userData 对齐）：
//   Linux   ~/.config/sparkle
//   Windows %APPDATA%/sparkle
//   macOS   ~/Library/Application Support/sparkle
class Paths {
public:
  static void initialize(const QString& executablePath, bool portable);
  static bool isPortable();

  static QString executablePath();
  static QString executableDir();
  static QString dataDir();

  static QString appConfigPath();              // dataDir/config.yaml
  static QString controlledMihomoConfigPath(); // dataDir/mihomo.yaml
  static QString profileConfigPath();          // dataDir/profile.yaml
  static QString overrideConfigPath();         // dataDir/override.yaml

  static QString profilesDir();                // dataDir/profiles
  static QString profilePath(const QString& id);
  static QString overrideDir();                // dataDir/override
  static QString overridePath(const QString& id, const QString& ext);

  static QString workDir();                    // dataDir/work
  static QString profileWorkDir(const QString& idOrDefault);
  static QString workConfigPath(const QString& idOrWork);

  static QString logDir();                     // dataDir/logs
  static QString appLogPath();
  static QString coreLogPath();
  static QString substoreLogPath();

  // 控制器端点（Mihomo external controller 的 unix socket / 命名管道地址）
  static QString controllerSocket();

  // detached 模式的 pid 记录文件（dataDir/core.pid.json，对齐原 manager.ts）
  static QString corePidPath();

  static QString sidecarDir();                 // resources/sidecar
  static QString mihomoCorePath(CoreKind kind);
  static QString resolveMihomoCorePath();      // SPARKLE_MIHOMO_PATH 优先

  Paths() = delete;
};

}  // namespace sparkle::core