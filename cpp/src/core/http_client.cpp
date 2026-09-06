#include "http_client.h"

#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace sparkle::core {

HttpParseStatus parseHttpResponse(const QByteArray& buffer, HttpResult& result) {
  // 1) 定位响应头结束 "\r\n\r\n"（状态行 + 头字段）。
  const int headerEnd = buffer.indexOf("\r\n\r\n");
  if (headerEnd < 0) {
    return HttpParseStatus::NeedMore;
  }

  // 2) 状态行 "HTTP/1.1 200 OK"。
  const int statusLineEnd = buffer.indexOf("\r\n");
  if (statusLineEnd < 0 || statusLineEnd >= headerEnd) {
    result.status = -1;
    result.body.clear();
    return HttpParseStatus::Done;   // 畸形，放弃
  }
  const QList<QByteArray> statusParts = buffer.left(statusLineEnd).split(' ');
  result.status = (statusParts.size() >= 2) ? statusParts[1].toInt() : -1;

  // 3) 头字段解析（大小写不敏感）。
  const QByteArray headerBlock = buffer.mid(statusLineEnd + 2, headerEnd - (statusLineEnd + 2));
  int contentLength = -1;
  bool chunked = false;
  const QList<QByteArray> headerLines = headerBlock.split('\n');
  for (QByteArray line : headerLines) {
    if (line.endsWith('\r')) line.chop(1);
    const int colon = line.indexOf(':');
    if (colon < 0) continue;
    const QByteArray name = line.left(colon).trimmed().toLower();
    const QByteArray value = line.mid(colon + 1).trimmed();
    if (name == "content-length") {
      contentLength = value.toInt();
    } else if (name == "transfer-encoding" && value.toLower().contains("chunked")) {
      chunked = true;
    }
  }

  const QByteArray body = buffer.mid(headerEnd + 4);

  // 4) 按传输编码取响应体。
  if (chunked) {
    QByteArray decoded;
    int pos = 0;
    while (true) {
      const int chunkSizeEnd = body.indexOf("\r\n", pos);
      if (chunkSizeEnd < 0) return HttpParseStatus::NeedMore;   // 块头未完整
      bool okHex = false;
      const int chunkSize = body.mid(pos, chunkSizeEnd - pos).trimmed().toInt(&okHex, 16);
      if (!okHex || chunkSize < 0) {
        result.status = -1;
        result.body.clear();
        return HttpParseStatus::Done;   // 畸形 chunk
      }
      if (chunkSize == 0) {             // 终止块
        result.body = decoded;
        return HttpParseStatus::Done;
      }
      pos = chunkSizeEnd + 2;
      if (body.size() - pos < chunkSize + 2) return HttpParseStatus::NeedMore;  // 数据未完整
      decoded += body.mid(pos, chunkSize);
      pos += chunkSize + 2;             // 数据 + CRLF
    }
  } else if (contentLength >= 0) {
    if (body.size() < contentLength) return HttpParseStatus::NeedMore;
    result.body = body.left(contentLength);
    return HttpParseStatus::Done;
  }

  // 5) 无长度（204/304 或连接即关）：视为无响应体。
  result.body.clear();
  return HttpParseStatus::Done;
}

HttpClient::HttpClient(QObject* parent) : QObject(parent) {
  nam_ = new QNetworkAccessManager(this);
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this] {
    if (!busy_) return;
    if (currentReply_) {                 // service 模式：中止在途请求
      QNetworkReply* r = currentReply_;
      currentReply_ = nullptr;
      r->abort();
    }
    HttpResult r;
    r.status = 504;
    r.error = QStringLiteral("request timeout");
    finish(r);
  });
}

HttpClient::~HttpClient() = default;

void HttpClient::setEndpoint(const QString& socketOrUrl, bool serviceMode) {
  endpoint_ = socketOrUrl;
  serviceMode_ = serviceMode;
}

void HttpClient::setSecret(const QString& secret) { secret_ = secret; }

void HttpClient::request(const QString& method, const QString& path, const QByteArray& body,
                         std::function<void(const HttpResult&)> onDone) {
  Request req;
  req.method = method;
  req.path = path;
  req.body = body;
  req.onDone = std::move(onDone);
  pending_.push_back(std::move(req));
  pump();
}

void HttpClient::get(const QString& path, std::function<void(const HttpResult&)> onDone) {
  request(QStringLiteral("GET"), path, QByteArray(), std::move(onDone));
}

void HttpClient::pump() {
  if (busy_ || pending_.empty() || endpoint_.isEmpty()) return;
  busy_ = true;
  timeout_.stop();

  Request req = std::move(pending_.front());
  pending_.pop_front();
  currentOnDone_ = std::move(req.onDone);
  recvBuffer_.clear();

  if (serviceMode_) {
    pumpService(std::move(req));
  } else {
    pumpDirect(std::move(req));
  }
}

void HttpClient::pumpDirect(Request&& req) {
  // 组装 HTTP/1.1 请求（Connection: close，一次请求一连接，简化响应边界）。
  QByteArray head = req.method.toUtf8() + ' ' + req.path.toUtf8() +
                    QByteArrayLiteral(" HTTP/1.1\r\n"
                                      "Host: localhost\r\n"
                                      "Connection: close\r\n");
  if (!secret_.isEmpty()) {
    head += QByteArrayLiteral("Authorization: Bearer ") + secret_.toUtf8() +
            QByteArrayLiteral("\r\n");
  }
  if (!req.body.isEmpty()) {
    head += QByteArrayLiteral("Content-Type: application/json\r\nContent-Length: ") +
            QByteArray::number(req.body.size()) + QByteArrayLiteral("\r\n");
  }
  head += QByteArrayLiteral("\r\n");
  outgoing_ = head + req.body;

  socket_ = std::make_unique<QLocalSocket>();
  connect(socket_.get(), &QLocalSocket::connected, this, &HttpClient::onConnected);
  connect(socket_.get(), &QLocalSocket::readyRead, this, &HttpClient::onReadyRead);
  connect(socket_.get(), &QLocalSocket::disconnected, this, &HttpClient::onDisconnected);
  connect(socket_.get(), &QLocalSocket::errorOccurred, this, &HttpClient::onError);
  socket_->connectToServer(endpoint_);
  timeout_.start(10000);   // 10s 超时
}

void HttpClient::pumpService(Request&& req) {
  const QUrl url(QStringLiteral("http://") + endpoint_ + req.path);
  QNetworkRequest rq(url);
  if (!secret_.isEmpty()) {
    rq.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + secret_.toUtf8());
  }
  if (!req.body.isEmpty()) {
    rq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  }

  const QByteArray verb = req.method.toUtf8().toUpper();
  QNetworkReply* reply =
      (verb == "GET") ? nam_->get(rq) : nam_->sendCustomRequest(rq, verb, req.body);
  currentReply_ = reply;
  // SSL 错误单独记录（便于排查证书/协议问题），但不阻断——finished 回调统一处理。
  connect(reply, &QNetworkReply::errorOccurred, this,
          [this, reply](QNetworkReply::NetworkError error) {
            if (reply != currentReply_) return;
            Q_UNUSED(error);
            // SSL 错误已在 errorString() 中，finished 回调取走；此处仅做日志备用。
          });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    if (reply != currentReply_) {   // 已被超时/停止接管：仅清理
      reply->deleteLater();
      return;
    }
    currentReply_ = nullptr;
    HttpResult r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // 收到 HTTP 状态码即读取 body（4xx/5xx 也是有效响应）；仅无状态码（网络故障）设 error。
    if (r.status > 0) {
      r.body = reply->readAll();
    } else {
      r.error = reply->errorString();
    }
    reply->deleteLater();
    finish(r);
  });
  timeout_.start(15000);   // service 模式稍宽的 15s 超时
}

void HttpClient::onConnected() {
  socket_->write(outgoing_);
  socket_->flush();
}

void HttpClient::onReadyRead() {
  if (!socket_) return;
  recvBuffer_ += socket_->readAll();
  HttpResult result;
  if (parseHttpResponse(recvBuffer_, result) == HttpParseStatus::Done) {
    finish(result);
  }
}

void HttpClient::onDisconnected() {
  if (!busy_) return;   // 已正常完成
  HttpResult r;
  r.status = 0;
  r.error = QStringLiteral("connection closed before response completed");
  finish(r);
}

void HttpClient::onError(QLocalSocket::LocalSocketError error) {
  if (!busy_) return;
  Q_UNUSED(error);
  HttpResult r;
  r.status = 0;
  r.error = QStringLiteral("controller socket error");
  finish(r);
}

void HttpClient::finish(const HttpResult& result) {
  if (!busy_) return;
  busy_ = false;
  timeout_.stop();
  auto cb = std::move(currentOnDone_);
  currentOnDone_ = nullptr;
  currentReply_ = nullptr;
  socket_.reset();   // 直连模式：触发 disconnected，但 busy_ 已置 false，不会重入

  HttpResult r = result;
  r.ok = (r.status >= 200 && r.status < 300);
  if (cb) cb(r);
  pump();            // 处理下一个排队请求
}

}  // namespace sparkle::core