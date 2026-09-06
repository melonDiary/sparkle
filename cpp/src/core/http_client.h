#pragma once

#include <QByteArray>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTimer>
#include <deque>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace sparkle::core {

struct HttpResult {
  int status = 0;        // HTTP 状态码；0 = 网络/解析失败
  QByteArray body;
  bool ok = false;       // 2xx
  QString error;         // 失败原因
};

enum class HttpParseStatus { NeedMore, Done };

// 增量解析一段 HTTP/1.1 响应（仅状态行 + Content-Length 或 Transfer-Encoding: chunked）。
// 直到拿到完整响应体前返回 NeedMore；供 HttpClient 与单元测试复用。
HttpParseStatus parseHttpResponse(const QByteArray& buffer, HttpResult& result);

// Mihomo external controller 的 HTTP/1.1 客户端（对应原 mihomoApi.ts 的 axios 层）。
// 直连模式走 QLocalSocket（unix socket / 命名管道）；service 模式走 QNetworkAccessManager
// 对 TCP 端点发请求，并带 `Authorization: Bearer <secret>`。
class HttpClient final : public QObject {
  Q_OBJECT
public:
  explicit HttpClient(QObject* parent = nullptr);
  ~HttpClient() override;

  void setEndpoint(const QString& socketOrUrl, bool serviceMode);
  void setSecret(const QString& secret);   // service 模式 Bearer 凭据
  void request(const QString& method, const QString& path, const QByteArray& body,
               std::function<void(const HttpResult&)> onDone);
  void get(const QString& path, std::function<void(const HttpResult&)> onDone);

private:
  struct Request {
    QString method;
    QString path;
    QByteArray body;
    std::function<void(const HttpResult&)> onDone;
  };

  void pump();                        // 取队首请求并发起（单连接串行）
  void pumpDirect(Request&& req);     // 直连：QLocalSocket + 手工 HTTP/1.1
  void pumpService(Request&& req);    // service：QNAM over TCP + Bearer
  void onConnected();
  void onReadyRead();
  void onDisconnected();
  void onError(QLocalSocket::LocalSocketError error);
  void finish(const HttpResult& result);

  QString endpoint_;
  bool serviceMode_ = false;
  QString secret_;

  std::deque<Request> pending_;       // 串行请求队列
  std::function<void(const HttpResult&)> currentOnDone_;
  std::unique_ptr<QLocalSocket> socket_;   // 直连模式
  QByteArray recvBuffer_;
  QByteArray outgoing_;
  QTimer timeout_;                    // 单次请求超时
  bool busy_ = false;

  // service 模式（QNAM）
  QNetworkAccessManager* nam_ = nullptr;
  QNetworkReply* currentReply_ = nullptr;
};

}  // namespace sparkle::core