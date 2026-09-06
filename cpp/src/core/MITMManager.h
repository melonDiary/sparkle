#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>
#include <optional>
#include <vector>

#include <QPointer>

#include "MITMRequest.h"
#include "MITMResponse.h"

namespace sparkle::core {
class ConfigManager;
class LogManager;

// 本地 MITM/HTTP 代理服务。
//
// 支持普通 HTTP 请求的规则拦截、响应改写以及 HTTPS CONNECT 透明隧道。
// CONNECT 隧道不解密 TLS；启用真正的 HTTPS MITM 需要后续接入受信任 CA、
// 动态站点证书和 QSslSocket TLS 终结，不能用临时不受信证书替代。
class MITMManager final : public QObject {
  Q_OBJECT
public:
  explicit MITMManager(ConfigManager* config = nullptr, LogManager* log = nullptr,
                       QObject* parent = nullptr);
  ~MITMManager() override;

  bool start(quint16 port = 0);
  void stop();
  bool isRunning() const;
  quint16 port() const;

  void setScriptDir(const QString& directory);
  QString scriptDir() const;
  bool reloadScripts();

signals:
  void started(quint16 port);
  void stopped();
  void requestIntercepted(const sparkle::core::MITMRequest& request);
  void responseIntercepted(const sparkle::core::MITMResponse& response);
  void errorOccurred(const QString& message);

private:
  class ClientConnection;
  struct RuleScript;

  void onNewConnection();
  void cleanupConnections();
  void onDirectoryChanged(const QString& path);
  void onFileChanged(const QString& path);
  void scheduleReload();
  void reportError(const QString& message);
  bool loadScriptFile(const QString& path, std::unique_ptr<RuleScript>& output);
  bool applyRequestRules(MITMRequest& request, std::optional<MITMResponse>& synthetic,
                         bool& aborted, QString& error);
  bool applyResponseRules(MITMResponse& response, QString& error);

  ConfigManager* config_ = nullptr;
  LogManager* log_ = nullptr;
  QTcpServer server_;
  QFileSystemWatcher watcher_;
  QTimer reloadTimer_;
  QString scriptDir_;
  std::vector<std::unique_ptr<RuleScript>> scripts_;
  std::vector<std::unique_ptr<ClientConnection>> connections_;
  quint16 port_ = 0;
  bool running_ = false;
};

}  // namespace sparkle::core
