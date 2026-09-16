#include "subscription_manager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

#include <cctype>

#include <nlohmann/json.hpp>

#include "log_manager.h"

namespace sparkle::core {
namespace {

using nlohmann::json;

constexpr int kMaxSubscriptionBytes = 20 * 1024 * 1024;  // 20 MiB
constexpr const char* kUserAgent = "sparkle/1.0";

// ===== 字节/文本清理 =====

QByteArray stripBom(const QByteArray& raw) {
  if (raw.startsWith("\xEF\xBB\xBF")) return raw.mid(3);             // UTF-8 BOM
  if (raw.startsWith("\xFF\xFE") || raw.startsWith("\xFE\xFF")) {    // UTF-16 BOM
    const QByteArray b = raw.mid(2);
    const QString s = QString::fromUtf16(reinterpret_cast<const char16_t*>(b.constData()),
                                         b.size() / 2);
    return s.toUtf8();
  }
  return raw;
}

QByteArray stripWhitespace(const QByteArray& in) {
  QByteArray out;
  out.reserve(in.size());
  for (char c : in) {
    if (!std::isspace(static_cast<unsigned char>(c))) out += c;
  }
  return out;
}

bool looksBase64(const QByteArray& in) {
  if (in.size() < 8) return false;
  for (char c : in) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) continue;
    if (c == '+' || c == '/' || c == '=' || c == '-' || c == '_') continue;
    return false;
  }
  return true;
}

QByteArray decodeBase64Flexible(const QByteArray& in) {
  QByteArray padded = stripWhitespace(in);
  while (padded.size() % 4 != 0) padded += '=';
  QByteArray d = QByteArray::fromBase64(padded, QByteArray::AbortOnBase64DecodingErrors);
  if (!d.isEmpty()) return d;
  return QByteArray::fromBase64(padded, QByteArray::Base64UrlEncoding |
                                            QByteArray::AbortOnBase64DecodingErrors);
}

bool looksLikeHtml(const QString& content) {
  const QString t = content.left(1024).trimmed().toLower();
  if (t.isEmpty()) return false;
  if (t.startsWith(QLatin1Char('<'))) return true;
  return t.contains(QLatin1String("<html")) || t.contains(QLatin1String("<!doctype")) ||
         t.contains(QLatin1String("<body")) || t.contains(QLatin1String("<head")) ||
         t.contains(QLatin1String("<?xml")) || t.contains(QLatin1String("<title"));
}

// ===== vmess JSON 辅助 =====

int jsonInt(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key)) return 0;
  const json& v = j[key];
  if (v.is_number_integer()) return v.get<int>();
  if (v.is_number_unsigned()) return static_cast<int>(v.get<unsigned>());
  if (v.is_string()) {
    bool ok = false;
    const int p = QString::fromStdString(v.get<std::string>()).toInt(&ok);
    return ok ? p : 0;
  }
  return 0;
}

QString jsonString(const json& j, const char* key) {
  if (!j.is_object() || !j.contains(key)) return QString();
  const json& v = j[key];
  if (v.is_string()) return QString::fromStdString(v.get<std::string>());
  if (v.is_number_integer()) return QString::number(v.get<int>());
  return QString();
}

// ===== host:port 解析（含 IPv6 [::]）=====

void splitHostPort(const QString& s, QString& host, int& port) {
  host.clear();
  port = 0;
  const QString t = s.trimmed();
  if (t.isEmpty()) return;
  if (t.startsWith(QLatin1Char('['))) {
    const int close = t.indexOf(QLatin1Char(']'));
    if (close < 0) {
      host = t;
      return;
    }
    host = t.mid(1, close - 1);
    if (close + 1 < t.size() && t.at(close + 1) == QLatin1Char(':')) {
      port = t.mid(close + 2).toInt();
    }
    return;
  }
  const int colon = t.lastIndexOf(QLatin1Char(':'));
  if (colon > 0 && t.indexOf(QLatin1Char(':')) == colon) {
    host = t.left(colon);
    port = t.mid(colon + 1).toInt();
  } else {
    host = t;  // 无端口或纯 IPv6（无括号）
  }
}

int defaultPortForScheme(const QString& scheme) {
  if (scheme == QLatin1String("http")) return 80;
  if (scheme == QLatin1String("https") || scheme == QLatin1String("trojan") ||
      scheme == QLatin1String("vless") || scheme == QLatin1String("vmess")) {
    return 443;
  }
  if (scheme == QLatin1String("socks5") || scheme == QLatin1String("socks")) return 1080;
  return 0;
}

// ===== 单行节点解析 =====

ProxyNode parseNodeLine(const QString& line) {
  ProxyNode node;
  const QString trimmed = line.trimmed();
  const int schemeEnd = trimmed.indexOf(QLatin1String("://"));
  if (schemeEnd <= 0) return node;  // 非节点行，返回 name/server 均为空

  const QString scheme = trimmed.left(schemeEnd).toLower();
  node.typeName = scheme;
  node.type = proxyTypeFromString(scheme);

  const int hashPos = trimmed.indexOf(QLatin1Char('#'), schemeEnd + 3);
  QString fragment;
  if (hashPos >= 0) {
    fragment = QUrl::fromPercentEncoding(trimmed.mid(hashPos + 1).toUtf8());
  }
  const int bodyStart = schemeEnd + 3;
  const int bodyLen = (hashPos >= 0 ? hashPos : trimmed.size()) - bodyStart;
  const QString body = trimmed.mid(bodyStart, bodyLen);

  auto finish = [&](const QString& host, int port, QString name) {
    node.server = host;
    node.port = port;
    if (name.isEmpty()) name = host;
    node.name = name;
    return node;
  };

  if (scheme == QLatin1String("vmess")) {
    QByteArray vmessSrc = decodeBase64Flexible(body.toLatin1());
    if (vmessSrc.isEmpty()) vmessSrc = QByteArrayLiteral("{}");
    const json j =
        json::parse(vmessSrc.constData(), vmessSrc.constData() + vmessSrc.size(), nullptr, false);
    const QString host = jsonString(j, "add");
    const int port = jsonInt(j, "port");
    const QString ps = jsonString(j, "ps");
    if (!host.isEmpty()) return finish(host, port, fragment.isEmpty() ? ps : fragment);
  } else if (scheme == QLatin1String("ss") || scheme == QLatin1String("ssr")) {
    QString hostPort = body;
    if (!body.contains(QLatin1Char('@'))) {
      hostPort = QString::fromUtf8(decodeBase64Flexible(body.toLatin1()));
    }
    const int at = hostPort.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) hostPort = hostPort.mid(at + 1);
    QString host;
    int port = 0;
    splitHostPort(hostPort, host, port);
    if (!host.isEmpty()) return finish(host, port, fragment);
  }

  // 其余协议：用 QUrl 解析（userinfo/host/port/query）。
  const QUrl u(trimmed);
  QString host = u.host();
  int port = u.port(0);
  if (port == 0) port = defaultPortForScheme(scheme);
  if (host.isEmpty()) {
    // QUrl 解析失败时兜底：body 中若有 @，尝试 host:port 段。
    const int at = body.lastIndexOf(QLatin1Char('@'));
    QString hp = (at >= 0) ? body.mid(at + 1) : body;
    hp = hp.section(QLatin1Char('/'), 0, 0);  // 去掉可能的 /path
    hp = hp.section(QLatin1Char('?'), 0, 0);  // 去掉 query
    splitHostPort(hp, host, port);
    if (port == 0) port = defaultPortForScheme(scheme);
  }
  if (host.isEmpty()) return node;  // 无法解析 → 跳过
  return finish(host, port, fragment);
}

}  // namespace

SubscriptionManager::SubscriptionManager(ConfigManager* config, LogManager* log, QObject* parent)
    : QObject(parent), log_(log) {
  Q_UNUSED(config);
  nam_ = new QNetworkAccessManager(this);
}

SubscriptionManager::~SubscriptionManager() {
  if (reply_) {
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
  }
}

void SubscriptionManager::fetch(
    const QString& url, const std::function<void(const std::vector<ProxyNode>&)>& onDone,
    const std::function<void(const QString&)>& onError) {
  if (reply_) {
    QObject::disconnect(reply_, nullptr, this, nullptr);
    reply_->abort();
    reply_->deleteLater();
    reply_ = nullptr;
  }

  const QUrl target(url);
  if (!target.isValid() || target.isEmpty()) {
    const QString m = QStringLiteral("无效的订阅地址：%1").arg(url);
    emit fetchFailed(m);
    if (onError) onError(m);
    return;
  }

  QNetworkRequest req(target);
  req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral(kUserAgent));
  reply_ = nam_->get(req);

  QObject::connect(reply_, &QNetworkReply::finished, this, [this, onDone, onError] {
    QNetworkReply* r = qobject_cast<QNetworkReply*>(sender());
    if (!r || r != reply_) return;  // 已被更新的请求替换
    reply_ = nullptr;

    // 重定向：跟随（相对地址相对当前 URL 解析）。
    const QVariant redirect = r->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirect.isValid()) {
      QUrl dest = redirect.toUrl();
      if (dest.isRelative()) dest = r->url().resolved(dest);
      r->deleteLater();
      if (dest.isValid() && !dest.isEmpty()) {
        fetch(dest.toString(), onDone, onError);
      } else {
        const QString m = QStringLiteral("订阅返回了无效的重定向地址");
        emit fetchFailed(m);
        if (onError) onError(m);
      }
      return;
    }

    if (r->error() != QNetworkReply::NoError) {
      const QString m = r->errorString();
      r->deleteLater();
      emit fetchFailed(m);
      if (onError) onError(m);
      return;
    }

    const QByteArray data = r->readAll();
    r->deleteLater();

    if (data.size() > kMaxSubscriptionBytes) {
      const QString m = QStringLiteral("订阅内容过大（%1 字节），已拒绝").arg(data.size());
      emit fetchFailed(m);
      if (onError) onError(m);
      return;
    }

    QString error;
    const auto nodes = parseSubscription(data, &error);
    if (nodes.empty() && !error.isEmpty()) {
      if (log_) log_->appendAppLog(QStringLiteral("[订阅] %1\n").arg(error));
      emit fetchFailed(error);
      if (onError) onError(error);
      return;
    }
    if (log_) {
      log_->appendAppLog(QStringLiteral("[订阅] 解析到 %1 个节点\n").arg(nodes.size()));
    }
    emit fetched(nodes);
    if (onDone) onDone(nodes);
  });
}

std::vector<ProxyNode> SubscriptionManager::parseSubscription(const QByteArray& raw,
                                                              QString* errorMessage) {
  if (errorMessage) errorMessage->clear();

  const QByteArray text = stripBom(raw);
  const QByteArray compact = stripWhitespace(text);
  const QString rawText = QString::fromUtf8(text).trimmed();

  QByteArray decoded;
  if (looksBase64(compact)) decoded = decodeBase64Flexible(compact);

  QString content;
  if (!decoded.isEmpty()) {
    const QString dec = QString::fromUtf8(decoded);
    // 解码后若是节点列表（含 scheme）则用之；否则退回原文（明文节点列表）。
    content = dec.contains(QLatin1String("://")) ? dec : rawText;
    if (!dec.contains(QLatin1String("://")) && rawText.contains(QLatin1String("://"))) {
      content = rawText;
    }
  } else {
    content = rawText;
  }

  std::vector<ProxyNode> nodes;
  if (content.trimmed().isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("订阅内容为空");
    return nodes;
  }
  if (looksLikeHtml(content)) {
    if (errorMessage)
      *errorMessage = QStringLiteral("订阅返回的是 HTML（错误页/登录页），请检查订阅地址或 UA");
    return nodes;
  }

  const QStringList lines =
      content.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
  for (const QString& line : lines) {
    ProxyNode n = parseNodeLine(line);
    if (n.server.isEmpty()) continue;  // 无法解析成节点
    if (n.name.isEmpty()) n.name = n.server;
    n.providerName = QStringLiteral("subscription");
    nodes.push_back(n);
  }

  if (nodes.empty() && errorMessage)
    *errorMessage = QStringLiteral("订阅内容中没有可识别的节点（支持 vmess/ss/ssr/trojan/vless 等）");
  return nodes;
}

}  // namespace sparkle::core