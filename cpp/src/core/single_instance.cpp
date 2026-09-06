#include "single_instance.h"

#include <QDataStream>

namespace sparkle::core {

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {
  connect(&server_, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
}

SingleInstance::~SingleInstance() {
  server_.close();
  QLocalServer::removeServer(kSocketName);   // 正常退出时移除 socket，避免残留阻塞下次启动
}

bool SingleInstance::tryAcquire() {
  // 1) 存活探测：尝试连接，能连上说明已有主实例在监听。
  QLocalSocket probe;
  probe.connectToServer(kSocketName);
  if (probe.waitForConnected(300)) {
    probe.disconnectFromServer();
    return false;
  }

  // 2) 无存活实例：清理陈旧 socket 文件（崩溃/强杀残留），再抢监听权。
  QLocalServer::removeServer(kSocketName);
  return server_.listen(kSocketName);
}

void SingleInstance::forwardToPrimary(int argc, char** argv) {
  QLocalSocket socket;
  socket.connectToServer(kSocketName);
  if (!socket.waitForConnected(500)) {
    return;
  }
  QByteArray block;
  QDataStream out(&block, QIODevice::WriteOnly);
  QStringList args;
  for (int i = 1; i < argc; ++i) {
    args << QString::fromLocal8Bit(argv[i]);
  }
  out << args;
  socket.write(block);
  socket.flush();
  socket.waitForBytesWritten(500);
}

void SingleInstance::onNewConnection() {
  while (QLocalSocket* socket = server_.nextPendingConnection()) {
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
      if (socket->bytesAvailable() < static_cast<qint64>(sizeof(quint32))) return;
      QDataStream in(socket);
      QStringList args;
      in >> args;
      emit activatedFromSecondary(args);
    });
  }
}

}  // namespace sparkle::core