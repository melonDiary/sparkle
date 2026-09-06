#pragma once

#include <QTcpServer>
#include <QString>

namespace sparkle::core {

// 本地 PAC HTTP server（对应原 resolve/server.ts）。auto 代理模式下提供 pac 脚本：
// 系统把 Web 代理指向本机该端口，浏览器取回的 FindProxyForURL 再指向前置代理端口。
class PacServer final : public QTcpServer {
  Q_OBJECT
public:
  explicit PacServer(QObject* parent = nullptr);

  bool start(quint16 port);        // listen 本地回环；port=0 由系统分配
  void stopServer();
  quint16 port() const;

  void setPacScript(const QString& script);   // 用户自定义脚本（appConfig.pacScript）；空=模板
  void setProxyPort(quint16 port);            // 模板中的前置代理端口（mixed-port）

private:
  void onNewConnection();
  QString pacScript() const;                  // 自定义优先，否则默认模板

  quint16 port_ = 0;
  quint16 proxyPort_ = 0;
  QString pacScript_;
};

}  // namespace sparkle::core