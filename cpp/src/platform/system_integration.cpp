#include "system_integration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace sparkle::platform {
namespace {

int runCommand(const QString& program, const QStringList& args, QString* output = nullptr) {
  QProcess process;
  process.setProgram(program);
  process.setArguments(args);
  process.start();
  if (!process.waitForStarted(3000)) {
    if (output) *output = QStringLiteral("进程无法启动：%1").arg(program);
    return -1;
  }
  if (!process.waitForFinished(8000)) {
    process.kill();
    process.waitForFinished(1000);
    if (output) *output = QStringLiteral("命令超时：%1").arg(program);
    return -2;
  }
  if (output) {
    *output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError())
                  .trimmed();
  }
  return process.exitCode();
}

bool writeTextFile(const QString& path, const QByteArray& content, QString* error = nullptr) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) *error = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
    return false;
  }
  file.write(content);
  file.close();
  return true;
}

QString xmlEscape(const QString& in) {
  QString out = in;
  out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
  out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
  out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
  return out;
}

QString quotedExec(const QString& path) {
  return QStringLiteral("\"%1\"").arg(path);
}

}  // namespace

// ===== 开机自启 =====

AutostartResult setAutostartEnabled(bool enable) {
  AutostartResult result;
#if defined(Q_OS_MACOS)
  const QString agentDir = QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
  const QString plistPath = agentDir + QStringLiteral("/com.sparkle.app.plist");
  if (enable) {
    if (!QDir().mkpath(agentDir)) {
      result.message = QStringLiteral("无法创建目录：%1").arg(agentDir);
      return result;
    }
    const QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>Label</key><string>com.sparkle.app</string>\n"
        "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>KeepAlive</key><false/>\n"
        "</dict></plist>\n")
                            .arg(xmlEscape(QCoreApplication::applicationFilePath()));
    if (!writeTextFile(plistPath, xml.toUtf8(), &result.message)) return result;
    QString out;
    int rc = runCommand(QStringLiteral("/bin/launchctl"),
                        {QStringLiteral("load"), QStringLiteral("-w"), plistPath}, &out);
    if (rc != 0) {
      QFile::remove(plistPath);
      result.message = QStringLiteral("launchctl 注册失败：%1").arg(out);
      return result;
    }
    result.ok = true;
    result.message = QStringLiteral("已注册为登录项");
  } else {
    QString out;
    runCommand(QStringLiteral("/bin/launchctl"),
               {QStringLiteral("unload"), QStringLiteral("-w"), plistPath}, &out);
    QFile::remove(plistPath);
    result.ok = true;
    result.message = QStringLiteral("已移除登录项");
  }
  return result;
#elif defined(Q_OS_WIN)
  QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                   QSettings::NativeFormat);
  const QString appPath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
  if (enable) {
    runKey.setValue(QStringLiteral("Sparkle"), appPath);
    result.ok = true;
    result.message = QStringLiteral("已写入注册表 Run 键");
  } else {
    runKey.remove(QStringLiteral("Sparkle"));
    result.ok = true;
    result.message = QStringLiteral("已从注册表移除");
  }
  return result;
#else
  const QString autoDir =
      QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
      QStringLiteral("/autostart");
  const QString desktopFile = autoDir + QStringLiteral("/sparkle.desktop");
  if (enable) {
    if (!QDir().mkpath(autoDir)) {
      result.message = QStringLiteral("无法创建目录：%1").arg(autoDir);
      return result;
    }
    const QString content =
        QStringLiteral("[Desktop Entry]\nType=Application\nName=Sparkle\nExec=%1\n"
                       "X-GNOME-Autostart-enabled=true\n")
            .arg(quotedExec(QCoreApplication::applicationFilePath()));
    if (!writeTextFile(desktopFile, content.toUtf8(), &result.message)) return result;
    result.ok = true;
    result.message = QStringLiteral("已写入 %1").arg(desktopFile);
  } else {
    QFile::remove(desktopFile);
    result.ok = true;
    result.message = QStringLiteral("已移除自启项");
  }
  return result;
#endif
}

bool isAutostartEnabled() {
#if defined(Q_OS_MACOS)
  return QFile::exists(QDir::homePath() +
                       QStringLiteral("/Library/LaunchAgents/com.sparkle.app.plist"));
#elif defined(Q_OS_WIN)
  QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                   QSettings::NativeFormat);
  return runKey.contains(QStringLiteral("Sparkle"));
#else
  return QFile::exists(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                       QStringLiteral("/autostart/sparkle.desktop"));
#endif
}

// ===== CA 根证书 =====

bool installRootCertificate(const QString& certPem, const QString& name, QString* error) {
  if (certPem.trimmed().isEmpty()) {
    if (error) *error = QStringLiteral("证书内容为空");
    return false;
  }
  QTemporaryFile file(QDir::tempPath() + QStringLiteral("/sparkle-ca-XXXXXX.crt"));
  file.setAutoRemove(false);
  if (!file.open()) {
    if (error) *error = QStringLiteral("无法创建临时证书文件");
    return false;
  }
  file.write(certPem.toUtf8());
  file.close();
  const QString certPath = file.fileName();

  bool ok = false;
  QString out;
#if defined(Q_OS_MACOS)
  const int rc = runCommand(
      QStringLiteral("/usr/bin/security"),
      {QStringLiteral("add-trusted-cert"), QStringLiteral("-d"), QStringLiteral("-r"),
       QStringLiteral("trustRoot"), QStringLiteral("-k"),
       QDir::homePath() + QStringLiteral("/Library/Keychains/login.keychain-db"), certPath},
      &out);
  ok = (rc == 0);
#elif defined(Q_OS_WIN)
  const int rc = runCommand(QStringLiteral("certutil"),
                            {QStringLiteral("-user"), QStringLiteral("-addstore"),
                             QStringLiteral("-f"), QStringLiteral("Root"), certPath},
                            &out);
  ok = (rc == 0);
#else
  const QString db = QStringLiteral("sql:") + QDir::homePath() + QStringLiteral("/.pki/nssdb");
  const int rc = runCommand(QStringLiteral("certutil"),
                            {QStringLiteral("-d"), db, QStringLiteral("-A"), QStringLiteral("-t"),
                             QStringLiteral("C,,"), QStringLiteral("-n"), name,
                             QStringLiteral("-i"), certPath},
                            &out);
  ok = (rc == 0);
#endif
  QFile::remove(certPath);
  if (!ok && error) *error = out.isEmpty() ? QStringLiteral("证书安装失败") : out;
  Q_UNUSED(name);  // macOS 由证书主体确定名称
  return ok;
}

bool isRootCertificateInstalled(const QString& name) {
  if (name.isEmpty()) return false;
  QString out;
#if defined(Q_OS_MACOS)
  const int rc = runCommand(QStringLiteral("/usr/bin/security"),
                            {QStringLiteral("find-certificate"), QStringLiteral("-c"), name,
                             QDir::homePath() + QStringLiteral("/Library/Keychains/login.keychain-db")},
                            &out);
  return rc == 0;
#elif defined(Q_OS_WIN)
  const int rc = runCommand(QStringLiteral("certutil"),
                            {QStringLiteral("-user"), QStringLiteral("-store"), QStringLiteral("Root"), name},
                            &out);
  return rc == 0;
#else
  const QString db = QStringLiteral("sql:") + QDir::homePath() + QStringLiteral("/.pki/nssdb");
  const int rc = runCommand(QStringLiteral("certutil"),
                            {QStringLiteral("-d"), db, QStringLiteral("-L"), QStringLiteral("-n"), name},
                            &out);
  return rc == 0;
#endif
}

// ===== TUN / 虚拟网卡 =====

bool isTunSupportAvailable() {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  // macOS/Linux 内核自带 TUN（utun / tun），mihomo 在运行时创建，无需额外驱动。
  return true;
#elif defined(Q_OS_WIN)
  // Windows 依赖 wintun.dll（mihomo 同目录或 System32）。
  const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/wintun.dll");
  const QString system = QStringLiteral("C:/Windows/System32/wintun.dll");
  return QFile::exists(local) || QFile::exists(system);
#else
  return false;
#endif
}

bool installTunDriver(QString* error) {
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  Q_UNUSED(error);
  return true;  // 内核 TUN，无需安装
#elif defined(Q_OS_WIN)
  const QString local = QCoreApplication::applicationDirPath() + QStringLiteral("/wintun.dll");
  const QString system = QStringLiteral("C:/Windows/System32/wintun.dll");
  if (QFile::exists(local) || QFile::exists(system)) return true;
  if (error)
    *error = QStringLiteral(
        "未找到 wintun.dll：请从 wintun.net 下载解压后，将对应架构的 wintun.dll 放到核心目录 "
        "（%1）或 C:\\Windows\\System32 并重启。")
                 .arg(QCoreApplication::applicationDirPath());
  return false;
#else
  if (error) *error = QStringLiteral("当前平台不支持 TUN");
  return false;
#endif
}

}  // namespace sparkle::platform