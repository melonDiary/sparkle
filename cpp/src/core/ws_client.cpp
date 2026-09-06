#include "ws_client.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QTimer>

namespace sparkle::core {

QByteArray computeWebSocketAccept(const QByteArray& secWebSocketKey) {
  static const char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  return QCryptographicHash::hash(secWebSocketKey + QByteArray(kGuid), QCryptographicHash::Sha1)
      .toBase64();
}

QByteArray encodeWebSocketFrame(int opcode, const QByteArray& payload, bool fin) {
  QByteArray out;
  const quint8 b0 = (fin ? quint8(0x80) : quint8(0x00)) | (quint8(opcode) & quint8(0x0f));
  out.append(static_cast<char>(b0));

  const quint64 len = static_cast<quint64>(payload.size());

  // 客户端帧必须带掩码（MASK=1）。
  quint8 maskKey[4];
  for (quint8& k : maskKey) {
    k = static_cast<quint8>(QRandomGenerator::global()->bounded(256));
  }

  quint8 b1 = quint8(0x80);
  if (len < 126) {
    b1 |= static_cast<quint8>(len);
    out.append(static_cast<char>(b1));
  } else if (len <= 0xffff) {
    b1 |= 126;
    out.append(static_cast<char>(b1));
    out.append(static_cast<char>((len >> 8) & 0xff));
    out.append(static_cast<char>(len & 0xff));
  } else {
    b1 |= 127;
    out.append(static_cast<char>(b1));
    for (int i = 7; i >= 0; --i) {
      out.append(static_cast<char>((len >> (i * 8)) & 0xff));
    }
  }
  out.append(reinterpret_cast<const char*>(maskKey), 4);
  for (int i = 0; i < payload.size(); ++i) {
    out.append(static_cast<char>(payload[i] ^ maskKey[i & 3]));
  }
  return out;
}

bool decodeWebSocketFrame(const QByteArray& buffer, bool& fin, int& opcode, QByteArray& payload,
                          qint64& consumed) {
  if (buffer.size() < 2) return false;
  const quint8 b0 = static_cast<quint8>(buffer[0]);
  const quint8 b1 = static_cast<quint8>(buffer[1]);

  fin = (b0 & 0x80) != 0;
  const bool masked = (b1 & 0x80) != 0;
  opcode = b0 & 0x0f;

  quint64 len = b1 & 0x7f;
  int pos = 2;
  if (len == 126) {
    if (buffer.size() < pos + 2) return false;
    len = (static_cast<quint8>(buffer[pos]) << 8) | static_cast<quint8>(buffer[pos + 1]);
    pos += 2;
  } else if (len == 127) {
    if (buffer.size() < pos + 8) return false;
    len = 0;
    for (int i = 0; i < 8; ++i) {
      len = (len << 8) | static_cast<quint8>(buffer[pos + i]);
    }
    pos += 8;
  }

  quint8 maskKey[4] = {0, 0, 0, 0};
  if (masked) {
    if (buffer.size() < pos + 4) return false;
    for (int i = 0; i < 4; ++i) maskKey[i] = static_cast<quint8>(buffer[pos + i]);
    pos += 4;
  }

  // 保护：单帧过大视为协议异常，直接拒绝（避免恶意/异常体撑爆内存）。
  constexpr quint64 kMaxFrame = 16ull * 1024 * 1024;
  if (len > kMaxFrame) {
    fin = true;
    opcode = 0x8;   // 伪装为 close，让上层断开
    payload.clear();
    consumed = static_cast<qint64>(pos);
    return true;
  }

  if (static_cast<quint64>(buffer.size() - pos) < len) return false;

  payload = buffer.mid(pos, static_cast<int>(len));
  if (masked) {
    for (int i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<char>(payload[i] ^ maskKey[i & 3]);
    }
  }
  consumed = static_cast<qint64>(pos) + static_cast<qint64>(len);
  return true;
}

namespace {
QByteArray generateWebSocketKey() {
  QByteArray key(16, '\0');
  for (int i = 0; i < 16; ++i) {
    key[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
  }
  return key.toBase64();
}
}  // namespace

WsClient::WsClient(QObject* parent) : QObject(parent) {
  retryTimer_.setSingleShot(true);
  connect(&retryTimer_, &QTimer::timeout, this, &WsClient::connectNow);
}

WsClient::~WsClient() {
  stop();
}

void WsClient::configure(const QString& endpoint, const QString& path, int retryBudget,
                         bool serviceMode, const QString& secret) {
  endpoint_ = endpoint;
  path_ = path;
  retryBudget_ = retryBudget;
  retry_ = retryBudget;
  serviceMode_ = serviceMode;
  secret_ = secret;
}

void WsClient::start() {
  if (endpoint_.isEmpty()) return;
  stopRequested_ = false;
  if (handshakeDone_) return;   // 已连接
  if (transport_ && transport_->isConnecting()) return;
  connectNow();
}

void WsClient::stop() {
  stopRequested_ = true;
  retryTimer_.stop();
  retry_ = retryBudget_;
  if (transport_) {
    if (handshakeDone_ || transport_->isOpen()) {
      // 优雅关闭：发 close 帧再断（RFC6455 §7.1）。
      if (handshakeDone_) {
        transport_->writeData(encodeWebSocketFrame(0x8, QByteArray()));
      }
      transport_->flush();
    }
    // 断开信号连接，防止 close() 触发 onDisconnected → scheduleReconnect
    transport_->disconnect(this);
    transport_->close();
    transport_.reset();
  }
  handshakeDone_ = false;
  fragment_.clear();
  fragmentOpcode_ = 0;
}

void WsClient::restart() {
  stop();
  start();
}

void WsClient::resetRetryBudget() { retry_ = retryBudget_; }

bool WsClient::isConnected() const {
  return handshakeDone_ && transport_ && transport_->isOpen();
}

void WsClient::connectNow() {
  retryTimer_.stop();
  if (stopRequested_) return;

  if (serviceMode_) {
    transport_ = std::make_unique<TcpSocketTransport>();
  } else {
    transport_ = std::make_unique<LocalSocketTransport>();
  }
  handshakeDone_ = false;
  everConnected_ = false;
  recvBuffer_.clear();
  fragment_.clear();
  fragmentOpcode_ = 0;

  connect(transport_.get(), &SocketTransport::opened, this, &WsClient::onConnected);
  connect(transport_.get(), &SocketTransport::readyRead, this, &WsClient::onReadyRead);
  connect(transport_.get(), &SocketTransport::closed, this, &WsClient::onDisconnected);
  connect(transport_.get(), &SocketTransport::failed, this, &WsClient::onError);

  if (serviceMode_) {
    const int colon = endpoint_.lastIndexOf(QLatin1Char(':'));
    const QString host = (colon > 0) ? endpoint_.left(colon) : endpoint_;
    const quint16 port = (colon > 0) ? endpoint_.mid(colon + 1).toUShort() : quint16(9090);
    transport_->connectTo(host, port);
  } else {
    transport_->connectTo(endpoint_, 0);
  }
}

void WsClient::onConnected() {
  everConnected_ = true;
  wsKey_ = generateWebSocketKey();
  QByteArray request = QByteArrayLiteral("GET ") + path_.toUtf8() +
                       QByteArrayLiteral(" HTTP/1.1\r\n"
                                          "Host: localhost\r\n"
                                          "Upgrade: websocket\r\n"
                                          "Connection: Upgrade\r\n"
                                          "Sec-WebSocket-Key: ") +
                       wsKey_ +
                       QByteArrayLiteral("\r\n"
                                          "Sec-WebSocket-Version: 13\r\n");
  if (!secret_.isEmpty()) {
    request += QByteArrayLiteral("Authorization: Bearer ") + secret_.toUtf8() +
               QByteArrayLiteral("\r\n");
  }
  request += QByteArrayLiteral("\r\n");
  transport_->writeData(request);
  transport_->flush();
}

void WsClient::onReadyRead() {
  if (!transport_) return;
  recvBuffer_ += transport_->readAllData();

  // 1) 尚未完成握手：等待响应头 "\r\n\r\n"。
  if (!handshakeDone_) {
    const int headerEnd = recvBuffer_.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    QString err;
    if (!validateHandshake(recvBuffer_.left(headerEnd), wsKey_, err)) {
      emit disconnected(QStringLiteral("handshake failed: ") + err);
      stopRequested_ = true;
      transport_->close();
      return;
    }
    handshakeDone_ = true;
    recvBuffer_.remove(0, headerEnd + 4);
    emit connected();
  }

  // 2) 循环解析帧。
  bool fin = false;
  int opcode = 0;
  QByteArray payload;
  qint64 consumed = 0;
  while (!recvBuffer_.isEmpty()) {
    if (!decodeWebSocketFrame(recvBuffer_, fin, opcode, payload, consumed)) break;
    recvBuffer_.remove(0, static_cast<int>(consumed));
    handleFrame(fin, opcode, payload);
  }
}

void WsClient::handleFrame(bool fin, int opcode, const QByteArray& payload) {
  switch (opcode) {
    case 0x1:   // text
    case 0x2:   // binary
      if (fin) {
        deliver(payload);
      } else {
        fragmentOpcode_ = opcode;
        fragment_ = payload;
      }
      break;
    case 0x0:   // continuation
      fragment_ += payload;
      if (fin) {
        deliver(fragment_);
        fragment_.clear();
        fragmentOpcode_ = 0;
      }
      break;
    case 0x8: { // close
      transport_->writeData(encodeWebSocketFrame(0x8, payload));
      transport_->flush();
      stopRequested_ = true;
      emit disconnected(QStringLiteral("closed by server"));
      transport_->close();
      break;
    }
    case 0x9:   // ping → pong
      transport_->writeData(encodeWebSocketFrame(0xA, payload));
      transport_->flush();
      break;
    case 0xA:   // pong
      break;
    default:
      break;
  }
}

void WsClient::deliver(const QByteArray& payload) {
  retry_ = retryBudget_;   // 收到成功消息：重连预算恢复满
  emit messageReceived(payload);
}

void WsClient::onDisconnected() {
  transport_.reset();
  handshakeDone_ = false;
  if (stopRequested_) return;
  emit disconnected(QStringLiteral("transport closed"));
  scheduleReconnect();
}

void WsClient::onError() {
  // 仅在"从未建立过连接"（server not found / connect refused）时在此重连；
  // 已建立连接之后的断链统一由 disconnected 驱动，避免重复计数。
  if (!everConnected_ && !stopRequested_) {
    transport_.reset();
    handshakeDone_ = false;
    scheduleReconnect();
  }
}

void WsClient::scheduleReconnect() {
  if (stopRequested_) return;
  --retry_;
  if (retry_ > 0) {
    retryTimer_.start(1000);
  } else {
    emit disconnected(QStringLiteral("retry budget exhausted"));
  }
}

bool WsClient::validateHandshake(const QByteArray& head, const QByteArray& key, QString& err) {
  const int statusLineEnd = head.indexOf("\r\n");
  if (statusLineEnd < 0) {
    err = QStringLiteral("no status line");
    return false;
  }
  const QList<QByteArray> statusParts = head.left(statusLineEnd).split(' ');
  if (statusParts.size() < 2 || statusParts[1].toInt() != 101) {
    err = QStringLiteral("not 101");
    return false;
  }

  const QByteArray expect = computeWebSocketAccept(key);
  const QList<QByteArray> headerLines = head.mid(statusLineEnd + 2).split('\n');
  bool acceptOk = false;
  for (QByteArray line : headerLines) {
    if (line.endsWith('\r')) line.chop(1);
    const int colon = line.indexOf(':');
    if (colon < 0) continue;
    const QByteArray name = line.left(colon).trimmed().toLower();
    const QByteArray value = line.mid(colon + 1).trimmed();
    if (name == "sec-websocket-accept" && value == expect) acceptOk = true;
  }
  if (!acceptOk) {
    err = QStringLiteral("bad sec-websocket-accept");
    return false;
  }
  return true;
}

}  // namespace sparkle::core