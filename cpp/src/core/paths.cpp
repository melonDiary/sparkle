#include "paths.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace sparkle::core {
namespace {

QString s_exePath;
QString s_exeDir;
bool s_portable = false;

QString datedLogName(const QString& prefix) {
  const QDate today = QDate::currentDate();
  const QString date =
      QStringLiteral("%1-%2-%3").arg(today.year()).arg(today.month()).arg(today.day());
  return QStringLiteral("%1%2-%3.log").arg(prefix.isEmpty() ? QString() : prefix + QLatin1Char('-'),
                                          date);
}

}  // namespace

void Paths::initialize(const QString& executablePath, bool portable) {
  s_exePath = executablePath;
  s_exeDir = QFileInfo(executablePath).absolutePath();
  s_portable = portable;

  QDir().mkpath(dataDir());
  QDir().mkpath(logDir());
  QDir().mkpath(profilesDir());
  QDir().mkpath(overrideDir());
  QDir().mkpath(workDir());
}

bool Paths::isPortable() { return s_portable; }

QString Paths::executablePath() { return s_exePath; }

QString Paths::executableDir() { return s_exeDir; }

QString Paths::dataDir() {
  if (s_portable) {
    return QDir(s_exeDir).filePath(QStringLiteral("data"));
  }
#if defined(Q_OS_LINUX)
  // Electron 的 userData 在 Linux 为 ~/.config/<name>，与需求一致。
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
      .filePath(QCoreApplication::applicationName());
#else
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
}

QString Paths::appConfigPath() { return QDir(dataDir()).filePath(QStringLiteral("config.yaml")); }
QString Paths::controlledMihomoConfigPath() {
  return QDir(dataDir()).filePath(QStringLiteral("mihomo.yaml"));
}
QString Paths::profileConfigPath() {
  return QDir(dataDir()).filePath(QStringLiteral("profile.yaml"));
}
QString Paths::overrideConfigPath() {
  return QDir(dataDir()).filePath(QStringLiteral("override.yaml"));
}

QString Paths::profilesDir() { return QDir(dataDir()).filePath(QStringLiteral("profiles")); }
QString Paths::profilePath(const QString& id) {
  return QDir(profilesDir()).filePath(id + QStringLiteral(".yaml"));
}
QString Paths::overrideDir() { return QDir(dataDir()).filePath(QStringLiteral("override")); }
QString Paths::overridePath(const QString& id, const QString& ext) {
  return QDir(overrideDir()).filePath(id + QLatin1Char('.') + ext);
}

QString Paths::workDir() { return QDir(dataDir()).filePath(QStringLiteral("work")); }
QString Paths::profileWorkDir(const QString& idOrDefault) {
  return QDir(workDir()).filePath(idOrDefault);
}
QString Paths::workConfigPath(const QString& idOrWork) {
  if (idOrWork == QLatin1String("work")) {
    return QDir(workDir()).filePath(QStringLiteral("config.yaml"));
  }
  return QDir(profileWorkDir(idOrWork)).filePath(QStringLiteral("config.yaml"));
}

QString Paths::logDir() { return QDir(dataDir()).filePath(QStringLiteral("logs")); }
QString Paths::appLogPath() { return QDir(logDir()).filePath(datedLogName(QStringLiteral("app"))); }
QString Paths::coreLogPath() {
  return QDir(logDir()).filePath(datedLogName(QStringLiteral("core")));
}
QString Paths::substoreLogPath() {
  return QDir(logDir()).filePath(datedLogName(QStringLiteral("sub-store")));
}

QString Paths::controllerSocket() {
#if defined(Q_OS_WIN)
  return QStringLiteral("\\\\.\\pipe\\Sparkle\\mihomo");
#else
  // 无权限回退（原 checkCorePermissionPathSync）：/tmp 不可写时（沙盒/App Store 构建）
  // 落到用户数据目录，避免内核因无法创建 unix socket 而监听失败。
  if (QFileInfo(QStringLiteral("/tmp")).isWritable()) {
    return QStringLiteral("/tmp/sparkle-mihomo-api.sock");
  }
  return QDir(dataDir()).filePath(QStringLiteral("sparkle-mihomo-api.sock"));
#endif
}

QString Paths::corePidPath() {
  return QDir(dataDir()).filePath(QStringLiteral("core.pid.json"));
}

QString Paths::sidecarDir() {
  // 随打包布局调整：优先 SPARKLE_SIDECAR_DIR 环境变量，其次可执行文件目录下 sidecar/。
  const QString env = qEnvironmentVariable("SPARKLE_SIDECAR_DIR");
  if (!env.isEmpty()) {
    return env;
  }
  return QDir(executableDir()).filePath(QStringLiteral("sidecar"));
}

QString Paths::mihomoCorePath(CoreKind kind) {
  QString name = kind == CoreKind::MihomoAlpha ? QStringLiteral("mihomo-alpha")
                                               : QStringLiteral("mihomo");
#if defined(Q_OS_WIN)
  name += QStringLiteral(".exe");
#endif
  return QDir(sidecarDir()).filePath(name);
}

QString Paths::resolveMihomoCorePath() {
  const QString env = qEnvironmentVariable("SPARKLE_MIHOMO_PATH");
  if (!env.isEmpty()) {
    return env;
  }
  return mihomoCorePath(CoreKind::Mihomo);
}

}  // namespace sparkle::core