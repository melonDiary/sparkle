#pragma once

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QTcpSocket>

namespace sparkle::core {

// 统一 QLocalSocket / QTcpSocket 的最小流式套接字抽象：
// - 直连模式：QLocalSocket（unix socket / 命名管道）
// - service 模式：QTcpSocket（mihomo 原生 TCP external-controller）
// 信号均为无参数归一化形式，供 HttpClient / WsClient 复用（连接、读、写、断、错）。
class SocketTransport : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  ~SocketTransport() override = default;

  virtual void connectTo(const QString& endpoint, quint16 port) = 0;
  virtual QByteArray readAllData() = 0;
  virtual qint64 writeData(const QByteArray& data) = 0;
  virtual void flush() = 0;
  virtual void close() = 0;         // 关闭连接（不触发业务侧重连等副作用）
  virtual bool isOpen() const = 0;
  virtual bool isConnecting() const = 0;

signals:
  void opened();
  void readyRead();
  void closed();
  void failed();
};

// QLocalSocket 实现（unix socket / 命名管道）。
class LocalSocketTransport final : public SocketTransport {
  Q_OBJECT
public:
  explicit LocalSocketTransport(QObject* parent = nullptr);
  void connectTo(const QString& endpoint, quint16 port) override;
  QByteArray readAllData() override;
  qint64 writeData(const QByteArray& data) override;
  void flush() override;
  void close() override;
  bool isOpen() const override;
  bool isConnecting() const override;

private:
  QLocalSocket socket_;
};

// QTcpSocket 实现（service 模式 TCP 控制器）。
class TcpSocketTransport final : public SocketTransport {
  Q_OBJECT
public:
  explicit TcpSocketTransport(QObject* parent = nullptr);
  void connectTo(const QString& endpoint, quint16 port) override;
  QByteArray readAllData() override;
  qint64 writeData(const QByteArray& data) override;
  void flush() override;
  void close() override;
  bool isOpen() const override;
  bool isConnecting() const override;

private:
  QTcpSocket socket_;
  bool everOpened_ = false;   // 仅"真正连上后再断"才报 closed（对齐 QLocalSocket 语义）
};

}  // namespace sparkle::core