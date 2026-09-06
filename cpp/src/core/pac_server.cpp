#include "pac_server.h"

#include <QHostAddress>
#include <QTcpSocket>

namespace sparkle::core {

PacServer::PacServer(QObject* parent) : QTcpServer(parent) {
  connect(this, &QTcpServer::newConnection, this, &PacServer::onNewConnection);
}

bool PacServer::start(quint16 port) {
  // 端口为 0 时由操作系统分配临时端口，必须保存 listen() 后的真实端口，
  // 否则上层生成的 PAC URL 会错误地指向 127.0.0.1:0。
  if (!listen(QHostAddress::LocalHost, port)) {
    port_ = 0;
    return false;
  }
  port_ = serverPort();
  return true;
}

void PacServer::stopServer() {
  close();
  port_ = 0;
}

quint16 PacServer::port() const { return port_; }

void PacServer::setPacScript(const QString& script) { pacScript_ = script; }

void PacServer::setProxyPort(quint16 port) { proxyPort_ = port; }

QString PacServer::pacScript() const {
  if (!pacScript_.isEmpty()) return pacScript_;
  return QStringLiteral(
             "function FindProxyForURL(url, host) {\n"
             "  return \"PROXY 127.0.0.1:%1; DIRECT\";\n"
             "}\n")
      .arg(proxyPort_);
}

void PacServer::onNewConnection() {
  while (QTcpSocket* socket = nextPendingConnection()) {
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    // 丢弃请求行即可：本地脚本服务无论请求什么路径都返回同一份 PAC。
    socket->readAll();

    const QByteArray body = pacScript().toUtf8();
    QByteArray response =
        QByteArrayLiteral("HTTP/1.1 200 OK\r\n"
                          "Content-Type: application/x-ns-proxy-autoconfig\r\n"
                          "Content-Length: ") +
        QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") +
        body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
  }
}

}  // namespace sparkle::core