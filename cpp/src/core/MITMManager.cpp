#include "MITMManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#include <nlohmann/json.hpp>

// QuickJS 是第三方 C 头文件；静默其在 C++ 告警选项下的兼容性告警。
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
extern "C" {
#include "quickjs.h"
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "config_manager.h"
#include "log_manager.h"
#include "quickjs_raii.h"

namespace sparkle::core {
namespace {

using nlohmann::json;

// 需要脚本改写的请求/响应体上限。超过则请求被拒绝（对齐 MITMRequest.h 的说明），
// 响应则不进入 JS、按流直放，避免大文件把整个 body 载入内存。
constexpr qint64 kMaxRequestBody = 1 * 1024 * 1024;    // 1 MiB
constexpr qint64 kMaxResponseBody = 16 * 1024 * 1024;  // 16 MiB
constexpr qint64 kJsTimeoutMs = 5000;

// ===== 头解析 =====

struct RequestHead {
  QString method;
  QString target;              // 绝对 URI（代理请求）或 authority（CONNECT）
  QString version = QStringLiteral("HTTP/1.1");
  QHash<QString, QString> headers;  // 键小写
  int contentLength = -1;
  bool chunked = false;
};

// 解析请求头。返回 headEnd（"\r\n\r\n" 之后的位置）。找不到完整头部返回 false。
bool parseRequestHead(const QByteArray& buf, RequestHead& out, int& headEnd) {
  const int end = buf.indexOf("\r\n\r\n");
  if (end < 0) return false;
  headEnd = end + 4;

  const int lineEnd = buf.indexOf("\r\n");
  if (lineEnd < 0 || lineEnd >= end) return true;  // 畸形请求行：交给上层拒掉

  const QList<QByteArray> parts = buf.left(lineEnd).split(' ');
  if (parts.size() >= 3) {
    out.method = QString::fromLatin1(parts[0]).trimmed().toUpper();
    out.target = QString::fromLatin1(parts[1]);
    out.version = QString::fromLatin1(parts[2]);
  } else if (parts.size() >= 2) {
    out.method = QString::fromLatin1(parts[0]).trimmed().toUpper();
    out.target = QString::fromLatin1(parts[1]);
  }

  const QByteArray headerBlock = buf.mid(lineEnd + 2, end - (lineEnd + 2));
  for (const QByteArray& line : headerBlock.split('\n')) {
    QByteArray l = line;
    if (l.endsWith('\r')) l.chop(1);
    const int colon = l.indexOf(':');
    if (colon < 0) continue;
    const QString name = QString::fromLatin1(l.left(colon).trimmed()).toLower();
    const QString value = QString::fromLatin1(l.mid(colon + 1).trimmed());
    out.headers.insert(name, value);
  }

  if (out.headers.contains(QStringLiteral("transfer-encoding")) &&
      out.headers.value(QStringLiteral("transfer-encoding")).toLower().contains(
          QLatin1String("chunked"))) {
    out.chunked = true;
  }
  if (out.headers.contains(QStringLiteral("content-length"))) {
    bool ok = false;
    const int cl = out.headers.value(QStringLiteral("content-length")).toInt(&ok);
    out.contentLength = ok && cl >= 0 ? cl : -1;
  }
  return true;
}

struct ResponseHead {
  int statusCode = 0;
  QString reason = QStringLiteral("OK");
  QHash<QString, QString> headers;
  int contentLength = -1;
  bool chunked = false;
};

bool parseResponseHead(const QByteArray& buf, ResponseHead& out, int& headEnd) {
  const int end = buf.indexOf("\r\n\r\n");
  if (end < 0) return false;
  headEnd = end + 4;

  const int lineEnd = buf.indexOf("\r\n");
  if (lineEnd < 0 || lineEnd >= end) return true;

  const QList<QByteArray> parts = buf.left(lineEnd).split(' ');
  if (parts.size() >= 2) {
    out.statusCode = parts[1].toInt();
  }
  if (parts.size() >= 3) {
    out.reason = QString::fromLatin1(parts[2]);
  }

  const QByteArray headerBlock = buf.mid(lineEnd + 2, end - (lineEnd + 2));
  for (const QByteArray& line : headerBlock.split('\n')) {
    QByteArray l = line;
    if (l.endsWith('\r')) l.chop(1);
    const int colon = l.indexOf(':');
    if (colon < 0) continue;
    const QString name = QString::fromLatin1(l.left(colon).trimmed()).toLower();
    const QString value = QString::fromLatin1(l.mid(colon + 1).trimmed());
    out.headers.insert(name, value);
  }

  if (out.headers.contains(QStringLiteral("transfer-encoding")) &&
      out.headers.value(QStringLiteral("transfer-encoding")).toLower().contains(
          QLatin1String("chunked"))) {
    out.chunked = true;
  }
  if (out.headers.contains(QStringLiteral("content-length"))) {
    bool ok = false;
    const int cl = out.headers.value(QStringLiteral("content-length")).toInt(&ok);
    out.contentLength = ok && cl >= 0 ? cl : -1;
  }
  return true;
}

// ===== JSON <-> JS 桥接（字节经 Latin-1 往返，保证任意字节无损）=====

JSValue jsonToJs(JSContext* ctx, const json& value) {
  const std::string text = value.dump();
  return JS_ParseJSON(ctx, text.c_str(), text.size(), "<mitm>");
}

json jsToJson(JSContext* ctx, JSValueConst value) {
  if (JS_IsUndefined(value) || JS_IsNull(value)) return json();
  JSValuePtr serialized(ctx, JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED));
  if (serialized.isException()) return json();
  JSCStringPtr text(ctx, JS_ToCString(ctx, serialized.get()));
  json result;
  if (text.get()) {
    try {
      result = json::parse(text.get());
    } catch (...) {
      result = json();
    }
  }
  return result;   // text 先析构，serialized 后析构（JS_ToCString 借用内嵌缓冲也安全）
}

QString exceptionText(JSContext* ctx) {
  JSValuePtr exception(ctx, JS_GetException(ctx));
  JSCStringPtr text(ctx, JS_ToCString(ctx, exception.get()));
  return text.get() ? QString::fromUtf8(text.get()) : QStringLiteral("未知 JavaScript 异常");
}

int mitmInterruptHandler(JSRuntime*, void* opaque) {
  const auto* deadline = static_cast<const qint64*>(opaque);
  return QDateTime::currentMSecsSinceEpoch() > *deadline ? 1 : 0;
}

json requestToJson(const MITMRequest& r) {
  json j;
  j["method"] = r.method.toStdString();
  j["url"] = r.url.toStdString();
  json headers = json::object();
  for (auto it = r.headers.cbegin(); it != r.headers.cend(); ++it) {
    headers[it.key().toStdString()] = it.value().toStdString();
  }
  j["headers"] = std::move(headers);
  j["body"] = QString::fromLatin1(r.body).toStdString();  // 字节无损 Latin-1 往返
  return j;
}

void jsonToRequest(const json& j, MITMRequest& r) {
  if (j.contains("method") && j["method"].is_string()) {
    r.method = QString::fromStdString(j["method"].get<std::string>());
  }
  if (j.contains("url") && j["url"].is_string()) {
    r.url = QString::fromStdString(j["url"].get<std::string>());
  }
  if (j.contains("headers") && j["headers"].is_object()) {
    for (auto it = j["headers"].begin(); it != j["headers"].end(); ++it) {
      if (it.value().is_string()) {
        r.headers.insert(QString::fromStdString(it.key()),
                         QString::fromStdString(it.value().get<std::string>()));
      }
    }
  }
  if (j.contains("body") && j["body"].is_string()) {
    r.body = QString::fromStdString(j["body"].get<std::string>()).toLatin1();
  }
}

json responseToJson(const MITMResponse& r) {
  json j;
  j["statusCode"] = r.statusCode;
  j["statusText"] = r.statusText.toStdString();
  json headers = json::object();
  for (auto it = r.headers.cbegin(); it != r.headers.cend(); ++it) {
    headers[it.key().toStdString()] = it.value().toStdString();
  }
  j["headers"] = std::move(headers);
  j["body"] = QString::fromLatin1(r.body).toStdString();
  return j;
}

void jsonToResponse(const json& j, MITMResponse& r) {
  if (j.contains("statusCode") && j["statusCode"].is_number_integer()) {
    r.statusCode = j["statusCode"].get<int>();
  }
  if (j.contains("statusText") && j["statusText"].is_string()) {
    r.statusText = QString::fromStdString(j["statusText"].get<std::string>());
  }
  if (j.contains("headers") && j["headers"].is_object()) {
    for (auto it = j["headers"].begin(); it != j["headers"].end(); ++it) {
      if (it.value().is_string()) {
        r.headers.insert(QString::fromStdString(it.key()),
                         QString::fromStdString(it.value().get<std::string>()));
      }
    }
  }
  if (j.contains("body") && j["body"].is_string()) {
    r.body = QString::fromStdString(j["body"].get<std::string>()).toLatin1();
  }
}

QByteArray serializeHeaders(const QHash<QString, QString>& headers) {
  QByteArray out;
  for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
    out += it.key().toUtf8() + ": " + it.value().toUtf8() + "\r\n";
  }
  return out;
}

// 取函数属性；非函数/异常返回 JS_UNDEFINED（调用方拥有返回值的引用）。
JSValue takeFunction(JSContext* ctx, JSValueConst obj, const char* name) {
  JSAtom atom = JS_NewAtom(ctx, name);
  JSValuePtr v(ctx, JS_GetProperty(ctx, obj, atom));
  JS_FreeAtom(ctx, atom);
  if (!JS_IsFunction(ctx, v.get())) {
    return JS_UNDEFINED;   // v 析构自动释放非函数值
  }
  return v.release();       // 交还所有权给调用方
}

QString reasonPhrase(int code) {
  switch (code) {
    case 200: return QStringLiteral("OK");
    case 201: return QStringLiteral("Created");
    case 204: return QStringLiteral("No Content");
    case 301: return QStringLiteral("Moved Permanently");
    case 302: return QStringLiteral("Found");
    case 304: return QStringLiteral("Not Modified");
    case 400: return QStringLiteral("Bad Request");
    case 401: return QStringLiteral("Unauthorized");
    case 403: return QStringLiteral("Blocked");   // 审计测试约定：403 拦截的默认原因短语
    case 404: return QStringLiteral("Not Found");
    case 413: return QStringLiteral("Payload Too Large");
    case 429: return QStringLiteral("Too Many Requests");
    case 500: return QStringLiteral("Internal Server Error");
    case 501: return QStringLiteral("Not Implemented");
    case 502: return QStringLiteral("Bad Gateway");
    case 503: return QStringLiteral("Service Unavailable");
    default: return QStringLiteral("OK");
  }
}

}  // namespace

// ===== RuleScript：每个脚本一个独立 QuickJS Runtime/Context，含超时中断 =====

struct MITMManager::RuleScript {
  QString path;
  JSRuntime* rt = nullptr;
  JSContext* ctx = nullptr;
  JSValue onRequest = JS_UNDEFINED;   // JS_DupValue 持有
  JSValue onResponse = JS_UNDEFINED;
  qint64 deadlineMs = 0;

  ~RuleScript() {
    if (ctx) {
      if (!JS_IsUndefined(onRequest)) JS_FreeValue(ctx, onRequest);
      if (!JS_IsUndefined(onResponse)) JS_FreeValue(ctx, onResponse);
      onRequest = JS_UNDEFINED;
      onResponse = JS_UNDEFINED;
      JS_FreeContext(ctx);
      ctx = nullptr;
    }
    if (rt) {
      JS_FreeRuntime(rt);
      rt = nullptr;
    }
  }
};

// ===== ClientConnection：单个客户端连接的迷你前向代理状态机 =====

class MITMManager::ClientConnection final : public QObject {
 public:
  ClientConnection(MITMManager* manager, QTcpSocket* client)
      : QObject(manager), manager_(manager), client_(client) {
    // client 由 manager 传入；这里接管其信号并设为本对象的孩子，随本对象析构释放。
    client_->setParent(this);
  }

  void start() {
    connect(client_, &QTcpSocket::readyRead, this, [this] { onClientReadyRead(); });
    connect(client_, &QTcpSocket::disconnected, this, [this] { finish(); });
    connect(client_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
      finish();
    });
  }

  bool isFinished() const { return finished_; }

 private:
  enum class Mode {
    ReadingRequest,        // 读请求头
    ReadingRequestBody,    // 读 Content-Length 请求体
    ReadingResponseHead,   // 读上游响应头
    ReadingResponseBody,   // 缓冲 Content-Length 响应体
    Streaming,             // 响应过大/分块：透传转发
    Tunneling,             // CONNECT 隧道
  };

  void finish() {
    if (finished_) return;
    finished_ = true;
    // 延迟到下一轮事件循环再清理：绝不在 socket 信号栈内 delete 自身/兄弟 socket。
    if (manager_) QTimer::singleShot(0, manager_, &MITMManager::cleanupConnections);
  }

  void onClientReadyRead() {
    if (finished_) return;
    const QByteArray data = client_->readAll();
    if (mode_ == Mode::Tunneling) {
      // 隧道：客户端字节直转上游；上游尚未连上则暂存。
      if (upstream_ && upstream_->state() == QAbstractSocket::ConnectedState) {
        upstream_->write(data);
      } else {
        inbuf_ += data;
      }
      return;
    }
    inbuf_ += data;
    processInput();
  }

  void processInput() {
    switch (mode_) {
      case Mode::ReadingRequest:
        handleRequestHead();
        break;
      case Mode::ReadingRequestBody:
        handleRequestBody();
        break;
      case Mode::ReadingResponseHead:
        handleResponseHead();
        break;
      case Mode::ReadingResponseBody:
        handleResponseBody();
        break;
      case Mode::Streaming:
        break;  // 透传数据由 onUpstreamReadyRead 直接转发
      case Mode::Tunneling:
        break;  // 隧道数据由 onClientReadyRead 直接互转
      default:
        break;
    }
  }

  void handleRequestHead() {
    RequestHead head;
    int headEnd = 0;
    if (!parseRequestHead(inbuf_, head, headEnd)) return;

    const bool isConnect = (head.method == QLatin1String("CONNECT"));
    request_.method = head.method;
    request_.headers = head.headers;
    request_.url = head.target;

    if (isConnect) {
      // CONNECT host:port —— 建隧道不解密 TLS。
      parseConnectTarget(head.target, upstreamHost_, upstreamPort_);
      if (upstreamHost_.isEmpty()) {
        respondError(400, QStringLiteral("Bad Request"));
        return;
      }
      inbuf_.remove(0, headEnd);   // 剩余字节留给隧道
      connectUpstream();
      mode_ = Mode::Tunneling;
      return;
    }

    // 非 CONNECT：目标从绝对 URI 解析，并转成 origin-form 转发。
    const QUrl url(head.target);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
      respondError(400, QStringLiteral("Bad Request"));
      return;
    }
    upstreamHost_ = url.host();
    upstreamPort_ = url.port(80);
    upstreamPath_ = url.path().isEmpty() ? QStringLiteral("/") : url.path();
    if (url.hasQuery()) upstreamPath_ += QLatin1Char('?') + url.query();
    request_.url = head.target;

    if (head.chunked || (head.contentLength > kMaxRequestBody)) {
      respondError(413, QStringLiteral("Request body too large / unsupported chunked"));
      return;
    }

    if (head.contentLength > 0) {
      mode_ = Mode::ReadingRequestBody;
      expectBodyBytes_ = head.contentLength;
      inbuf_.remove(0, headEnd);
      handleRequestBody();
      return;
    }

    // 无请求体。
    inbuf_.remove(0, headEnd);
    dispatch();
  }

  void handleRequestBody() {
    const qint64 need = std::min<qint64>(expectBodyBytes_ - bodyBuf_.size(), inbuf_.size());
    if (need > 0) {
      bodyBuf_ += inbuf_.left(static_cast<int>(need));
      inbuf_.remove(0, static_cast<int>(need));
    }
    if (bodyBuf_.size() < expectBodyBytes_) return;
    request_.body = bodyBuf_;
    bodyBuf_.clear();
    dispatch();
  }

  void dispatch() {
    // 1) 请求规则链。
    bool aborted = false;
    std::optional<MITMResponse> synthetic;
    QString error;
    if (manager_ && !manager_->applyRequestRules(request_, synthetic, aborted, error)) {
      respondError(500, QStringLiteral("Rule script error"));
      return;
    }
    if (aborted) {
      client_->disconnectFromHost();  // 丢弃请求：直接关闭
      return;
    }
    if (synthetic.has_value()) {
      emit manager_->requestIntercepted(request_);
      sendSynthetic(*synthetic);
      return;
    }

    if (manager_) emit manager_->requestIntercepted(request_);

    // 2) 连上游并转发改写后的请求。
    connectUpstream();
  }

  void connectUpstream() {
    upstream_ = new QTcpSocket(this);
    connect(upstream_, &QTcpSocket::connected, this, [this] { onUpstreamConnected(); });
    connect(upstream_, &QTcpSocket::readyRead, this, [this] { onUpstreamReadyRead(); });
    connect(upstream_, &QTcpSocket::disconnected, this, [this] {
      // 上游关闭：把客户端待写数据冲掉后优雅关闭，再由 client disconnected 驱动清理。
      if (client_ && client_->state() == QAbstractSocket::ConnectedState) {
        client_->disconnectFromHost();
      } else {
        finish();
      }
    });
    connect(upstream_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
      if (finished_) return;
      if (!upstreamConnected_) {
        respondError(502, QStringLiteral("Bad Gateway"));  // 内部 disconnectFromHost → disconnected → finish
      } else {
        finish();
      }
    });
    upstream_->connectToHost(upstreamHost_, upstreamPort_);
  }

  void onUpstreamConnected() {
    if (finished_) return;
    upstreamConnected_ = true;
    if (mode_ == Mode::Tunneling) {
      // CONNECT 成功 → 200 后开始双向转发。
      client_->write("HTTP/1.1 200 Connection Established\r\n\r\n");
      // 把请求头解析后遗留的字节（客户端抢先发的 TLS 数据）转发给上游。
      if (!inbuf_.isEmpty()) {
        upstream_->write(inbuf_);
        inbuf_.clear();
      }
      return;
    }
    // 普通请求：用 origin-form 重写后发送。
    QByteArray out = request_.method.toUtf8() + ' ' + upstreamPath_.toUtf8() +
                     " HTTP/1.1\r\n";
    // 确保 Host / Content-Length 与最终 body 一致。
    if (!request_.headers.contains(QStringLiteral("host"))) {
      request_.headers.insert(QStringLiteral("host"), upstreamHost_);
    }
    request_.headers.remove(QStringLiteral("transfer-encoding"));
    request_.headers.insert(QStringLiteral("content-length"),
                            QString::number(request_.body.size()));
    out += serializeHeaders(request_.headers);
    out += "\r\n";
    out += request_.body;
    upstream_->write(out);
    mode_ = Mode::ReadingResponseHead;
  }

  void onUpstreamReadyRead() {
    if (finished_) return;
    const QByteArray chunk = upstream_->readAll();
    if (mode_ == Mode::Tunneling) {
      client_->write(chunk);
      return;
    }
    if (mode_ == Mode::Streaming) {
      client_->write(chunk);
      return;
    }
    upstreamBuf_ += chunk;
    if (mode_ == Mode::ReadingResponseHead) handleResponseHead();
    else if (mode_ == Mode::ReadingResponseBody) handleResponseBody();
  }

  void handleResponseHead() {
    ResponseHead head;
    int headEnd = 0;
    if (!parseResponseHead(upstreamBuf_, head, headEnd)) return;

    response_.statusCode = head.statusCode;
    response_.statusText = head.reason;
    response_.headers = head.headers;

    if (head.chunked || (head.contentLength < 0 && head.statusCode != 204 && head.statusCode != 304) ||
        head.contentLength > kMaxResponseBody) {
      // 大响应/分块：只对头部做规则改写，body 透传（不载入内存）。
      QString error;
      if (manager_) manager_->applyResponseRules(response_, error);
      if (manager_) emit manager_->responseIntercepted(response_);
      QByteArray out = buildStreamingHeadBytes();
      client_->write(out);
      const QByteArray rest = upstreamBuf_.mid(headEnd);
      if (!rest.isEmpty()) client_->write(rest);
      mode_ = Mode::Streaming;
      return;
    }

    mode_ = Mode::ReadingResponseBody;
    expectBodyBytes_ = (head.contentLength > 0) ? head.contentLength : 0;
    bodyBuf_ = upstreamBuf_.mid(headEnd);
    upstreamBuf_.clear();
    handleResponseBody();
  }

  void handleResponseBody() {
    if (bodyBuf_.size() < expectBodyBytes_) return;  // 等更多数据
    response_.body = bodyBuf_.left(static_cast<int>(expectBodyBytes_));
    bodyBuf_.clear();

    QString error;
    if (manager_) manager_->applyResponseRules(response_, error);
    if (manager_) emit manager_->responseIntercepted(response_);

    QByteArray out = buildResponseBytes();
    client_->write(out);
    client_->disconnectFromHost();  // 冲掉待写数据后优雅关闭，disconnected 再驱动清理
  }

  QByteArray buildResponseBytes() const {
    QHash<QString, QString> headers = response_.headers;
    headers.remove(QStringLiteral("transfer-encoding"));
    headers.insert(QStringLiteral("content-length"), QString::number(response_.body.size()));
    headers.insert(QStringLiteral("connection"), QStringLiteral("close"));
    QByteArray out = "HTTP/1.1 " + QByteArray::number(response_.statusCode > 0 ? response_.statusCode : 200) +
                     ' ' + (response_.statusText.isEmpty() ? reasonPhrase(response_.statusCode > 0 ? response_.statusCode : 200).toUtf8() : response_.statusText.toUtf8()) +
                     "\r\n";
    out += serializeHeaders(headers);
    out += "\r\n";
    out += response_.body;
    return out;
  }

  // 透传（大响应/分块）响应头：保留原始 Content-Length / Transfer-Encoding，
  // 只施加规则对头字段的改写，并强制 connection: close 让客户端按连接关闭结束。
  QByteArray buildStreamingHeadBytes() const {
    QHash<QString, QString> headers = response_.headers;
    headers.insert(QStringLiteral("connection"), QStringLiteral("close"));
    QByteArray out = "HTTP/1.1 " + QByteArray::number(response_.statusCode > 0 ? response_.statusCode : 200) +
                     ' ' + (response_.statusText.isEmpty() ? reasonPhrase(response_.statusCode > 0 ? response_.statusCode : 200).toUtf8() : response_.statusText.toUtf8()) +
                     "\r\n";
    out += serializeHeaders(headers);
    out += "\r\n";
    return out;
  }

  void respondError(int code, const QString& text) {
    const QByteArray body = (text + "\r\n").toUtf8();
    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + ' ' + text.toUtf8() +
                     "\r\nContent-Type: text/plain\r\nContent-Length: " +
                     QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    client_->write(out);
    client_->disconnectFromHost();
  }

  void sendSynthetic(const MITMResponse& response) {
    MITMResponse r = response;
    if (r.statusCode <= 0) r.statusCode = 200;
    if (r.statusText.isEmpty()) r.statusText = reasonPhrase(r.statusCode);
    response_ = r;
    response_.headers.remove(QStringLiteral("transfer-encoding"));
    response_.headers.insert(QStringLiteral("content-length"), QString::number(r.body.size()));
    response_.headers.insert(QStringLiteral("connection"), QStringLiteral("close"));
    QByteArray out = "HTTP/1.1 " + QByteArray::number(r.statusCode) + ' ' + r.statusText.toUtf8() + "\r\n";
    out += serializeHeaders(response_.headers);
    out += "\r\n";
    out += r.body;
    client_->write(out);
    client_->disconnectFromHost();
  }

  static void parseConnectTarget(const QString& target, QString& host, quint16& port) {
    host.clear();
    port = 443;
    const int colon = target.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) {
      host = target.left(colon);
      bool ok = false;
      const int p = target.mid(colon + 1).toInt(&ok);
      if (ok && p > 0 && p <= 65535) port = static_cast<quint16>(p);
    } else if (!target.trimmed().isEmpty()) {
      host = target.trimmed();
    }
    // IPv6 字面量 [::1]:443 的更完整解析留待后续；此处按主机名/端口处理。
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
      host = host.mid(1, host.size() - 2);
    }
  }

  QPointer<MITMManager> manager_;
  QTcpSocket* client_ = nullptr;
  QTcpSocket* upstream_ = nullptr;
  bool upstreamConnected_ = false;

  QByteArray inbuf_;
  QByteArray upstreamBuf_;
  QByteArray bodyBuf_;
  qint64 expectBodyBytes_ = 0;

  Mode mode_ = Mode::ReadingRequest;

  MITMRequest request_;
  MITMResponse response_;
  QString upstreamHost_;
  quint16 upstreamPort_ = 80;
  QString upstreamPath_;
  bool finished_ = false;
};

// ===== MITMManager =====

MITMManager::MITMManager(ConfigManager* config, LogManager* log, QObject* parent)
    : QObject(parent), config_(config), log_(log) {
  qRegisterMetaType<MITMRequest>();
  qRegisterMetaType<MITMResponse>();

  connect(&server_, &QTcpServer::newConnection, this, &MITMManager::onNewConnection);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
          &MITMManager::onDirectoryChanged);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &MITMManager::onFileChanged);

  reloadTimer_.setSingleShot(true);
  reloadTimer_.setInterval(500);
  connect(&reloadTimer_, &QTimer::timeout, this, [this] { reloadScripts(); });
}

MITMManager::~MITMManager() { stop(); }

void MITMManager::setScriptDir(const QString& directory) {
  scriptDir_ = QDir::cleanPath(directory);
  reloadScripts();
}

QString MITMManager::scriptDir() const {
  if (!scriptDir_.isEmpty()) return scriptDir_;
  if (config_) return config_->mitmScriptDir();
  return QDir::tempPath() + QStringLiteral("/sparkle-mitm");
}

bool MITMManager::start(quint16 port) {
  if (running_) return false;
  reloadScripts();
  if (!server_.listen(QHostAddress::LocalHost, port)) {
    reportError(QStringLiteral("MITM 监听失败：%1").arg(server_.errorString()));
    return false;
  }
  port_ = server_.serverPort();
  running_ = true;
  emit started(port_);
  return true;
}

void MITMManager::stop() {
  if (!running_) return;
  running_ = false;
  server_.close();
  // connections_ 里的每根连接会随析构释放其 QTcpSocket（客户端/上游），
  // 避免 Socket 句柄泄漏。
  connections_.clear();
  port_ = 0;
  emit stopped();
}

bool MITMManager::isRunning() const { return running_; }
quint16 MITMManager::port() const { return port_; }

void MITMManager::onNewConnection() {
  while (QTcpSocket* socket = server_.nextPendingConnection()) {
    auto connection = std::make_unique<ClientConnection>(this, socket);
    connection->start();
    connections_.push_back(std::move(connection));
  }
}

void MITMManager::cleanupConnections() {
  connections_.erase(
      std::remove_if(connections_.begin(), connections_.end(),
                     [](const std::unique_ptr<ClientConnection>& c) {
                       return !c || c->isFinished();
                     }),
      connections_.end());
}

void MITMManager::onDirectoryChanged(const QString&) { scheduleReload(); }
void MITMManager::onFileChanged(const QString&) { scheduleReload(); }

void MITMManager::scheduleReload() { reloadTimer_.start(); }

void MITMManager::reportError(const QString& message) {
  if (log_) log_->appendAppLog(QStringLiteral("[MITM] %1\n").arg(message));
  emit errorOccurred(message);
}

bool MITMManager::reloadScripts() {
  scripts_.clear();
  watcher_.removePaths(watcher_.directories());
  watcher_.removePaths(watcher_.files());

  const QDir dir(scriptDir());
  watcher_.addPath(dir.absolutePath());

  const QStringList files = dir.entryList(QStringList{QStringLiteral("*.js")}, QDir::Files);
  for (const QString& name : files) {
    const QString path = dir.filePath(name);
    std::unique_ptr<RuleScript> script;
    if (loadScriptFile(path, script) && script) {
      scripts_.push_back(std::move(script));
      watcher_.addPath(path);
    }
  }
  return true;
}

bool MITMManager::loadScriptFile(const QString& path, std::unique_ptr<RuleScript>& output) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    reportError(QStringLiteral("无法读取规则脚本：%1").arg(path));
    return false;
  }
  const QByteArray source = file.readAll();
  if (source.size() > 1024 * 1024) {
    reportError(QStringLiteral("规则脚本超过 1 MiB：%1").arg(path));
    return false;
  }

  auto script = std::make_unique<RuleScript>();
  script->path = path;
  script->rt = JS_NewRuntime();
  if (!script->rt) {
    reportError(QStringLiteral("无法创建 QuickJS Runtime：%1").arg(path));
    return false;
  }
  JS_SetMemoryLimit(script->rt, 32ull * 1024 * 1024);
  JS_SetMaxStackSize(script->rt, 256 * 1024);
  script->ctx = JS_NewContext(script->rt);
  if (!script->ctx) {
    reportError(QStringLiteral("无法创建 QuickJS Context：%1").arg(path));
    return false;
  }
  JS_SetInterruptHandler(script->rt, &mitmInterruptHandler, &script->deadlineMs);
  script->deadlineMs = QDateTime::currentMSecsSinceEpoch() + kJsTimeoutMs;

  const QByteArray wrapped = QByteArrayLiteral(
                                 "var module={exports:{}};var exports=module.exports;\n") +
                             source + QByteArrayLiteral("\n;module.exports;");
  JSValuePtr exported(
      script->ctx,
      JS_Eval(script->ctx, wrapped.constData(), wrapped.size(), path.toUtf8().constData(),
              JS_EVAL_TYPE_GLOBAL));
  if (exported.isException()) {
    const QString err = exceptionText(script->ctx);
    reportError(QStringLiteral("规则脚本执行失败 %1：%2").arg(path, err));
    return false;
  }

  // 规则脚本两种约定：
  //   1) module.exports = { onRequest, onResponse }（挂在 exports 对象上）
  //   2) 顶层函数 function onRequest/onResponse（审计测试约定，挂在全局对象上）
  JSValuePtr requestFn(script->ctx, JS_UNDEFINED);
  JSValuePtr responseFn(script->ctx, JS_UNDEFINED);
  if (JS_IsObject(exported.get())) {
    requestFn = JSValuePtr(script->ctx, takeFunction(script->ctx, exported.get(), "onRequest"));
    responseFn = JSValuePtr(script->ctx, takeFunction(script->ctx, exported.get(), "onResponse"));
  }
  JSValuePtr global(script->ctx, JS_GetGlobalObject(script->ctx));
  if (JS_IsUndefined(requestFn.get())) {
    requestFn = JSValuePtr(script->ctx, takeFunction(script->ctx, global.get(), "onRequest"));
  }
  if (JS_IsUndefined(responseFn.get())) {
    responseFn = JSValuePtr(script->ctx, takeFunction(script->ctx, global.get(), "onResponse"));
  }

  if (!JS_IsUndefined(requestFn.get())) {
    script->onRequest = JS_DupValue(script->ctx, requestFn.get());
  }
  if (!JS_IsUndefined(responseFn.get())) {
    script->onResponse = JS_DupValue(script->ctx, responseFn.get());
  }
  // exported / global / requestFn / responseFn 由 JSValuePtr 按逆序自动释放。

  output = std::move(script);
  return true;
}

bool MITMManager::applyRequestRules(MITMRequest& request, std::optional<MITMResponse>& synthetic,
                                    bool& aborted, QString& error) {
  synthetic.reset();
  aborted = false;
  for (auto& script : scripts_) {
    if (!script || !script->ctx || JS_IsUndefined(script->onRequest)) continue;

    script->deadlineMs = QDateTime::currentMSecsSinceEpoch() + kJsTimeoutMs;
    JSValuePtr arg(script->ctx, jsonToJs(script->ctx, requestToJson(request)));
    JSValue argRaw = arg.get();
    JSValuePtr result(script->ctx, JS_Call(script->ctx, script->onRequest, JS_UNDEFINED, 1, &argRaw));
    arg.reset();   // 请求对象不再需要，尽早释放

    if (result.isException()) {
      error = exceptionText(script->ctx);
      return false;
    }
    if (JS_IsUndefined(result.get()) || JS_IsNull(result.get())) {
      continue;  // 未修改
    }
    if (!JS_IsObject(result.get())) {
      const int truthy = JS_ToBool(script->ctx, result.get());
      if (!truthy) {
        aborted = true;  // return false → 丢弃请求
      }
      continue;
    }

    const json out = jsToJson(script->ctx, result.get());
    if (!out.is_object()) continue;

    // 合成响应：直接返回 {statusCode,...}，或 {response:{...}}。
    if ((out.contains("statusCode") && out["statusCode"].is_number()) ||
        (out.contains("response") && out["response"].is_object())) {
      const json& resp = out.contains("response") ? out["response"] : out;
      jsonToResponse(resp, synthetic.emplace());
      return true;
    }
    // 丢弃请求：{abort:true}
    if (out.contains("abort") && out["abort"].is_boolean() && out["abort"].get<bool>()) {
      aborted = true;
      return true;
    }
    // 改写请求：return req（修改后的请求对象）或 {request:{...}}。
    const json& req = out.contains("request") && out["request"].is_object() ? out["request"] : out;
    jsonToRequest(req, request);
  }
  return true;
}

bool MITMManager::applyResponseRules(MITMResponse& response, QString& error) {
  for (auto& script : scripts_) {
    if (!script || !script->ctx || JS_IsUndefined(script->onResponse)) continue;

    script->deadlineMs = QDateTime::currentMSecsSinceEpoch() + kJsTimeoutMs;
    JSValuePtr arg(script->ctx, jsonToJs(script->ctx, responseToJson(response)));
    JSValue argRaw = arg.get();
    JSValuePtr result(script->ctx, JS_Call(script->ctx, script->onResponse, JS_UNDEFINED, 1, &argRaw));
    arg.reset();

    if (result.isException()) {
      error = exceptionText(script->ctx);
      return false;
    }
    if (JS_IsUndefined(result.get()) || JS_IsNull(result.get())) {
      continue;
    }
    if (JS_IsObject(result.get())) {
      const json out = jsToJson(script->ctx, result.get());
      if (out.is_object()) {
        const json& resp =
            out.contains("response") && out["response"].is_object() ? out["response"] : out;
        jsonToResponse(resp, response);
      }
      continue;
    }
    // 非对象返回值（bool/number 等）直接忽略，result 由 JSValuePtr 在迭代结束时释放。
  }
  return true;
}

}  // namespace sparkle::core