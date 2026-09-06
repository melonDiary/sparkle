#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

#include "socket_transport.h"

namespace sparkle::core {

// —— RFC6455 帧编解码（独立函数，供 WsClient 与单元测试复用）——

// 客户端发送必须掩码：opcode 1=text 2=binary 8=close 9=ping 10=pong（fin 默认 true）。
QByteArray encodeWebSocketFrame(int opcode, const QByteArray& payload, bool fin = true);

// 解析缓冲区开头的一个完整帧。返回 false=数据不足；true 时通过输出参数返回
// fin/opcode/已去掩码 payload/该帧总字节数 consumed。
bool decodeWebSocketFrame(const QByteArray& buffer, bool& fin, int& opcode, QByteArray& payload,
                          qint64& consumed);

// 计算 Sec-WebSocket-Accept = base64(SHA1(key + GUID))。
QByteArray computeWebSocketAccept(const QByteArray& secWebSocketKey);

// 一条 Mihomo 数据流 WebSocket 客户端（对应原 mihomo-stream.ts）：
// 直连模式走 QLocalSocket（unix socket / 命名管道）；service 模式走 QTcpSocket；
// secret 非空时握手携带 `Authorization: Bearer <secret>`。含握手、帧收发、心跳、有界重连。
class WsClient final : public QObject {
  Q_OBJECT
public:
  explicit WsClient(QObject* parent = nullptr);
  ~WsClient() override;

  // endpoint：直连 = unix socket/pipe 路径；service = "host:port"。
  void configure(const QString& endpoint, const QString& path, int retryBudget = 10,
                 bool serviceMode = false, const QString& secret = QString());
  void start();
  void stop();
  void restart();
  void resetRetryBudget();                 // 成功消息后台调用，把重连预算重置满
  bool isConnected() const;

signals:
  void messageReceived(const QByteArray& payload);
  void connected();
  void disconnected(const QString& reason);

private:
  void connectNow();
  void onConnected();
  void onReadyRead();
  void onDisconnected();
  void onError();
  void scheduleReconnect();
  void handleFrame(bool fin, int opcode, const QByteArray& payload);
  void deliver(const QByteArray& payload);
  bool validateHandshake(const QByteArray& head, const QByteArray& key, QString& err);

  QString endpoint_;
  QString path_;
  int retryBudget_ = 10;
  int retry_ = 10;

  bool serviceMode_ = false;
  QString secret_;

  std::unique_ptr<SocketTransport> transport_;
  QTimer retryTimer_;                      // 1s 后重连
  QByteArray recvBuffer_;
  QByteArray wsKey_;                       // 本次握手 key
  QByteArray fragment_;                    // 分片累积
  int fragmentOpcode_ = 0;
  bool handshakeDone_ = false;
  bool everConnected_ = false;             // 是否建立过 TCP/pipe 连接（区分"连不上"与"已连后断"）
  bool stopRequested_ = false;
};

}  // namespace sparkle::core