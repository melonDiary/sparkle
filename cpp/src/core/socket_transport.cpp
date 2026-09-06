#include "socket_transport.h"

#include <QAbstractSocket>

namespace sparkle::core {

LocalSocketTransport::LocalSocketTransport(QObject* parent) : SocketTransport(parent) {
  connect(&socket_, &QLocalSocket::connected, this, &SocketTransport::opened);
  connect(&socket_, &QLocalSocket::readyRead, this, &SocketTransport::readyRead);
  connect(&socket_, &QLocalSocket::disconnected, this, &SocketTransport::closed);
  connect(&socket_, &QLocalSocket::errorOccurred, this, &SocketTransport::failed);
}

void LocalSocketTransport::connectTo(const QString& endpoint, quint16 port) {
  Q_UNUSED(port);
  socket_.connectToServer(endpoint);
}

QByteArray LocalSocketTransport::readAllData() { return socket_.readAll(); }

qint64 LocalSocketTransport::writeData(const QByteArray& data) { return socket_.write(data); }

void LocalSocketTransport::flush() { socket_.flush(); }

void LocalSocketTransport::close() { socket_.disconnectFromServer(); }

bool LocalSocketTransport::isOpen() const {
  return socket_.state() == QLocalSocket::ConnectedState;
}

bool LocalSocketTransport::isConnecting() const {
  return socket_.state() == QLocalSocket::ConnectingState;
}

TcpSocketTransport::TcpSocketTransport(QObject* parent) : SocketTransport(parent) {
  connect(&socket_, &QTcpSocket::connected, this, [this] {
    everOpened_ = true;
    emit opened();
  });
  connect(&socket_, &QTcpSocket::readyRead, this, &SocketTransport::readyRead);
  // 连接失败（QTcpSocket::errorOccurred）时同样会触发 disconnected；此处只在真正连上后
  // 才上报 closed，避免"连接被拒"被重复计为一次断链（对齐 QLocalSocket 语义）。
  connect(&socket_, &QTcpSocket::disconnected, this, [this] {
    if (everOpened_) emit closed();
  });
  connect(&socket_, &QTcpSocket::errorOccurred, this, &SocketTransport::failed);
}

void TcpSocketTransport::connectTo(const QString& endpoint, quint16 port) {
  everOpened_ = false;
  socket_.connectToHost(endpoint, port);
}

QByteArray TcpSocketTransport::readAllData() { return socket_.readAll(); }

qint64 TcpSocketTransport::writeData(const QByteArray& data) { return socket_.write(data); }

void TcpSocketTransport::flush() { socket_.flush(); }

void TcpSocketTransport::close() { socket_.disconnectFromHost(); }

bool TcpSocketTransport::isOpen() const {
  return socket_.state() == QAbstractSocket::ConnectedState;
}

bool TcpSocketTransport::isConnecting() const {
  return socket_.state() == QAbstractSocket::ConnectingState;
}

}  // namespace sparkle::core