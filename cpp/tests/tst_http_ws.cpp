#include "http_client.h"
#include "mihomo_api_client.h"
#include "ws_client.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

#include <memory>
#include <algorithm>

#include <nlohmann/json.hpp>

using namespace sparkle::core;

// 验证传输层纯函数：HTTP/1.1 响应解析 + RFC6455 WebSocket 帧编解码。
class TstHttpWs : public QObject {
  Q_OBJECT

private slots:
  void httpContentLength() {
    HttpResult r;
    const QByteArray resp =
        QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");
    QCOMPARE(parseHttpResponse(resp, r), HttpParseStatus::Done);
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, QByteArray("hello"));

    HttpResult partial;
    QCOMPARE(parseHttpResponse(
                 QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhel"), partial),
             HttpParseStatus::NeedMore);
  }

  void httpChunked() {
    HttpResult r;
    const QByteArray resp = QByteArrayLiteral(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
    QCOMPARE(parseHttpResponse(resp, r), HttpParseStatus::Done);
    QCOMPARE(r.status, 200);
    QCOMPARE(r.body, QByteArray("hello world"));
  }

  void proxyResponseParsing() {
    const nlohmann::json response = {
        {"proxies", {{"DIRECT", {{"name", "DIRECT"}, {"type", "Direct"}, {"alive", true}}},
                     {"Node A", {{"name", "Node A"},
                                  {"type", "Shadowsocks"},
                                  {"alive", true},
                                  {"provider-name", "Provider"},
                                  {"history", {{{"time", "2025-01-01T00:00:00.000Z"}, {"delay", 123}}}}}},
                     {"Auto", {{"name", "Auto"},
                                {"type", "Selector"},
                                {"alive", true},
                                {"now", "Node A"},
                                {"all", {"Node A", "DIRECT"}},
                                {"hidden", false},
                                {"fixed", "Node A"}}}}}};

    const auto nodes = proxyNodesFromControllerJson(response);
    QCOMPARE(nodes.size(), std::size_t(2));
    const auto nodeIt = std::find_if(nodes.begin(), nodes.end(), [](const ProxyNode& node) {
      return node.name == QStringLiteral("Node A");
    });
    QVERIFY(nodeIt != nodes.end());
    QCOMPARE(nodeIt->providerName, QStringLiteral("Provider"));
    QCOMPARE(nodeIt->delay, 123);
    QVERIFY(nodeIt->history.size() == 1);

    const auto groups = proxyGroupsFromControllerJson(response);
    QCOMPARE(groups.size(), std::size_t(1));
    QCOMPARE(groups[0].name, QStringLiteral("Auto"));
    QCOMPARE(groups[0].now, QStringLiteral("Node A"));
    QVERIFY(groups[0].fixed);
    QCOMPARE(groups[0].all, QStringList({QStringLiteral("Node A"), QStringLiteral("DIRECT")}));

    QVERIFY(proxyNodesFromControllerJson(nlohmann::json::object()).empty());
    QVERIFY(proxyGroupsFromControllerJson(nlohmann::json::object()).empty());
  }

  void rulesResponseParsing() {
    const nlohmann::json response = {
        {"rules", nlohmann::json::array({
                     nlohmann::json{{"index", 7},
                                    {"type", "DOMAIN-SUFFIX"},
                                    {"payload", "example.com"},
                                    {"proxy", "Proxy"},
                                    {"size", 2},
                                    {"extra", {{"disabled", true}, {"hitCount", 11},
                                                {"missCount", 3}}}},
                     nlohmann::json{{"index", 8},
                                    {"type", "MATCH"},
                                    {"payload", ""},
                                    {"proxy", "DIRECT"}}})}};

    const auto rules = rulesFromControllerJson(response);
    QCOMPARE(rules.size(), std::size_t(2));
    QCOMPARE(rules[0].index, std::size_t(7));
    QCOMPARE(rules[0].type, QStringLiteral("DOMAIN-SUFFIX"));
    QCOMPARE(rules[0].payload, QStringLiteral("example.com"));
    QCOMPARE(rules[0].proxy, QStringLiteral("Proxy"));
    QCOMPARE(rules[0].size, std::size_t(2));
    QVERIFY(rules[0].disabled);
    QCOMPARE(rules[0].hitCount, std::uint64_t(11));
    QCOMPARE(rules[0].missCount, std::uint64_t(3));
    QCOMPARE(rules[1].index, std::size_t(8));
    QVERIFY(!rules[1].disabled);
    QVERIFY(rulesFromControllerJson(nlohmann::json::object()).empty());
  }

  void wsAcceptKnownVector() {
    // RFC6455 §1.3 示例向量。
    QCOMPARE(computeWebSocketAccept(QByteArrayLiteral("dGhlIHNhbXBsZSBub25jZQ==")),
             QByteArrayLiteral("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
  }

  void wsFrameRoundTrip() {
    const QByteArray payload = QByteArrayLiteral("{\"up\":1,\"down\":2}");
    const QByteArray frame = encodeWebSocketFrame(0x1, payload);

    bool fin = false;
    int opcode = 0;
    QByteArray decoded;
    qint64 consumed = 0;
    QVERIFY(decodeWebSocketFrame(frame, fin, opcode, decoded, consumed));
    QVERIFY(fin);
    QCOMPARE(opcode, 0x1);
    QCOMPARE(decoded, payload);
    QCOMPARE(consumed, qint64(frame.size()));
  }

  void wsDecodeUnmaskedServerFrame() {
    QByteArray frame;
    frame.append(static_cast<char>(0x81));   // FIN + text
    frame.append(static_cast<char>(0x05));   // 无掩码，长度 5
    frame += "hello";

    bool fin = false;
    int opcode = 0;
    QByteArray payload;
    qint64 consumed = 0;
    QVERIFY(decodeWebSocketFrame(frame, fin, opcode, payload, consumed));
    QVERIFY(fin);
    QCOMPARE(opcode, 0x1);
    QCOMPARE(payload, QByteArray("hello"));
  }

  void wsDecodeExtendedLength() {
    const QByteArray payload(300, 'x');
    QByteArray frame;
    frame.append(static_cast<char>(0x81));
    frame.append(static_cast<char>(126));        // 16 位扩展长度
    frame.append(static_cast<char>(0x01));       // 0x012c = 300
    frame.append(static_cast<char>(0x2c));
    frame += payload;

    bool fin = false;
    int opcode = 0;
    QByteArray decoded;
    qint64 consumed = 0;
    QVERIFY(decodeWebSocketFrame(frame, fin, opcode, decoded, consumed));
    QCOMPARE(int(decoded.size()), 300);
    QCOMPARE(decoded, payload);
  }

  void wsIncompleteFrame() {
    QByteArray frame;
    frame.append(static_cast<char>(0x81));
    frame.append(static_cast<char>(0x05));
    frame += "he";   // 只有 2/5 字节

    bool fin = false;
    int opcode = 0;
    QByteArray payload;
    qint64 consumed = 0;
    QVERIFY(!decodeWebSocketFrame(frame, fin, opcode, payload, consumed));
  }

  void httpServiceBearer() {
    // 本地最小 HTTP 服务器：校验 Authorization: Bearer 头，回 200 + JSON。
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    bool sawAuth = false;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &sawAuth] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      auto reqBuf = std::make_shared<QByteArray>();
      auto responded = std::make_shared<bool>(false);
      QObject::connect(s, &QTcpSocket::readyRead, s, [s, reqBuf, responded, &sawAuth] {
        *reqBuf += s->readAll();
        if (*responded || !reqBuf->contains("\r\n\r\n")) return;
        *responded = true;
        if (reqBuf->toUpper().contains("AUTHORIZATION: BEARER SECRET123")) sawAuth = true;
        const QByteArray body = QByteArrayLiteral("{\"ok\":true}");
        const QByteArray resp =
            QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
            QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        s->write(resp);
        s->disconnectFromHost();
      });
    });

    HttpClient client;
    client.setEndpoint(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()), true);
    client.setSecret(QStringLiteral("secret123"));

    HttpResult result;
    bool done = false;
    QEventLoop loop;
    client.get(QStringLiteral("/version"), [&](const HttpResult& r) {
      result = r;
      done = true;
      loop.quit();
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(done);
    QVERIFY(result.ok);
    QCOMPARE(result.status, 200);
    QVERIFY(sawAuth);
  }

  // 边界：连接被拒（目标端口未监听）→ 快速返回 error。
  void httpPatchSendsRuleDisablePayload() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QByteArray request;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &request] {
      QTcpSocket* socket = server.nextPendingConnection();
      if (!socket) return;
      socket->setParent(&server);
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &request] {
        request += socket->readAll();
        const int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const QByteArray headers = request.left(headerEnd);
        const int contentLengthPos = headers.toLower().indexOf("content-length:");
        int contentLength = 0;
        if (contentLengthPos >= 0) {
          const int valueStart = contentLengthPos + QByteArrayLiteral("content-length:").size();
          contentLength = headers.mid(valueStart).trimmed().split('\r').first().toInt();
        }
        if (request.size() < headerEnd + 4 + contentLength) return;
        const QByteArray response = QByteArrayLiteral(
            "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->write(response);
        socket->disconnectFromHost();
      });
    });

    HttpClient client;
    client.setEndpoint(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()), true);

    HttpResult result;
    bool done = false;
    QEventLoop loop;
    client.request(QStringLiteral("PATCH"), QStringLiteral("/rules/disable"),
                   QByteArrayLiteral("{\"7\":true}"), [&](const HttpResult& response) {
                     result = response;
                     done = true;
                     loop.quit();
                   });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(done);
    QVERIFY(result.ok);
    QCOMPARE(result.status, 204);
    QVERIFY(request.startsWith("PATCH /rules/disable HTTP/1.1\r\n"));
    QVERIFY(request.endsWith(QByteArrayLiteral("{\"7\":true}")));
  }

  void httpConnectionRefused() {
    HttpClient client;
    // 随机高端口，不太可能有人监听。
    client.setEndpoint(QStringLiteral("127.0.0.1:19999"), true);

    HttpResult result;
    bool done = false;
    QEventLoop loop;
    client.get(QStringLiteral("/version"), [&](const HttpResult& r) {
      result = r;
      done = true;
      loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(done);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
  }

  // 边界：服务器返回 4xx/5xx，HttpClient 正确解析状态码。
  void httpServerError() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      QObject::connect(s, &QTcpSocket::readyRead, s, [s] {
        s->readAll();
        const QByteArray body = QByteArrayLiteral("{\"error\":\"forbidden\"}");
        const QByteArray resp =
            QByteArrayLiteral("HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n"
                              "Content-Length: ") +
            QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        s->write(resp);
        s->disconnectFromHost();
      });
    });

    HttpClient client;
    client.setEndpoint(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()), true);

    HttpResult result;
    bool done = false;
    QEventLoop loop;
    client.get(QStringLiteral("/test"), [&](const HttpResult& r) {
      result = r;
      done = true;
      loop.quit();
    });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(done);
    QCOMPARE(result.status, 403);
    QVERIFY(!result.ok);
    QVERIFY(result.body.contains("forbidden"));
  }

  // 边界：服务器无响应（挂起），HttpClient 超时后回调。
  void httpTimeout() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      // 不回复，让客户端超时。
    });

    HttpClient client;
    client.setEndpoint(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()), true);

    HttpResult result;
    bool done = false;
    QEventLoop loop;
    client.get(QStringLiteral("/hang"), [&](const HttpResult& r) {
      result = r;
      done = true;
      loop.quit();
    });
    // 等待足够长：service 模式 15s 超时 + 余量。
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(done);
    QVERIFY(!result.ok);
    QVERIFY(result.error.contains("timeout") || result.error.contains("Timeout"));
  }

  // 边界：多次串行请求在同一 HttpClient 上执行。
  void httpSerialRequests() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    int requestCount = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, &requestCount] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      QObject::connect(s, &QTcpSocket::readyRead, s, [s, &requestCount] {
        s->readAll();
        ++requestCount;
        const QByteArray body = QByteArrayLiteral("{\"n\":1}");
        const QByteArray resp =
            QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
            QByteArray::number(body.size()) +
            QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        s->write(resp);
        s->disconnectFromHost();
      });
    });

    HttpClient client;
    client.setEndpoint(QStringLiteral("127.0.0.1:%1").arg(server.serverPort()), true);

    QList<HttpResult> results;
    int completed = 0;
    QEventLoop loop;
    for (int i = 0; i < 3; ++i) {
      client.get(QStringLiteral("/req%1").arg(i), [&](const HttpResult& r) {
        results.append(r);
        ++completed;
        if (completed >= 3) loop.quit();
      });
    }
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(completed, 3);
    for (const auto& r : results) {
      QCOMPARE(r.status, 200);
      QVERIFY(r.ok);
    }
  }
};

QTEST_GUILESS_MAIN(TstHttpWs)
#include "tst_http_ws.moc"