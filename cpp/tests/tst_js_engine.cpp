#include "js_engine.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace sparkle::core;

// 验证 QuickJS 覆写引擎：同步/异步 main、内置 b64/yaml/console、fetch 真网络、异常与非法返回。
class TstJsEngine : public QObject {
  Q_OBJECT

private slots:
  void syncMain() {
    const char* script =
        "function main(config) { config.mode = 'global'; config.tags = ['a','b']; return config; }";
    JsOverrideResult r = runOverrideScript(QString::fromUtf8(script),
                                           nlohmann::json{{"a", 1}}, QString());
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.profile["a"].get<int>(), 1);
    QCOMPARE(QString::fromStdString(r.profile.value("mode", std::string())), QString("global"));
    QCOMPARE(int(r.profile["tags"].size()), 2);
  }

  void asyncMain() {
    const char* script =
        "function main(c){ return Promise.resolve(Object.assign({}, c, {done:true})); }";
    JsOverrideResult r = runOverrideScript(QString::fromUtf8(script), nlohmann::json::object(),
                                           QString());
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.profile.value("done", false), true);
  }

  void helpers() {
    const char* script =
        "function main(c){"
        "  console.log('hello', 42);"
        "  var s = b64e('abc');"
        "  var d = b64d(s);"
        "  c.enc = d + '/' + yaml.stringify({k:'v'}).trim().split(':')[0];"
        "  return c;"
        "}";
    JsOverrideResult r = runOverrideScript(QString::fromUtf8(script),
                                           nlohmann::json{{"x", 0}}, QString());
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.log.contains("hello 42"));
    QCOMPARE(QString::fromStdString(r.profile.value("enc", std::string())), QString("abc/k"));
  }

  void throws() {
    const char* script = "function main(c){ throw new Error('boom'); }";
    JsOverrideResult r = runOverrideScript(QString::fromUtf8(script), nlohmann::json::object(),
                                           QString());
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains("boom"));
  }

  void nonObject() {
    const char* script = "function main(c){ return 123; }";
    JsOverrideResult r = runOverrideScript(QString::fromUtf8(script), nlohmann::json::object(),
                                           QString());
    QVERIFY(!r.ok);
  }

  void fetch() {
    // 本地最小 HTTP 服务器：回显 POST 体并返回 {"hello":"world"}。
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      QObject::connect(s, &QTcpSocket::readyRead, s, [s] {
        s->readAll();   // 消费请求（不需要精确解析，仅验证链路的请求/响应往返）
        const QByteArray body = QByteArrayLiteral("{\"hello\":\"world\"}");
        const QByteArray resp =
            QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ") +
            QByteArray::number(body.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        s->write(resp);
        s->disconnectFromHost();
      });
    });

    const QString script =
        "function main(c){"
        "  return fetch('http://127.0.0.1:%1/hello', { method: 'POST', body: JSON.stringify({a:1}) })"
        "    .then(function(r){ c.status = r.status; return r.json(); })"
        "    .then(function(data){ c.fetched = data.hello; return c; });"
        "}";
    JsOverrideResult r =
        runOverrideScript(script.arg(port), nlohmann::json::object(), QString());
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.profile.value("status", 0), 200);
    QCOMPARE(QString::fromStdString(r.profile.value("fetched", std::string())),
             QString("world"));
  }

  // 边界：fetch 连接被拒 → 脚本应抛异常，不崩溃。
  void fetchConnectionRefused() {
    const QString script =
        "function main(c){"
        "  return fetch('http://127.0.0.1:19999/nope')"
        "    .then(function(r){ c.status = r.status; return c; })"
        "    .catch(function(e){ c.error = String(e); return c; });"
        "}";
    JsOverrideResult r =
        runOverrideScript(script, nlohmann::json::object(), QString());
    // 连接被拒：fetch 抛异常 → catch 捕获 → 返回对象。
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.profile.contains("error"));
  }

  // 边界：服务器返回非 200，r.ok 应为 false。
  void fetchNon200() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      QObject::connect(s, &QTcpSocket::readyRead, s, [s] {
        s->readAll();
        const QByteArray body = QByteArrayLiteral("{\"err\":\"denied\"}");
        const QByteArray resp =
            QByteArrayLiteral("HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n"
                              "Content-Length: ") +
            QByteArray::number(body.size()) +
            QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        s->write(resp);
        s->disconnectFromHost();
      });
    });

    const QString script =
        "function main(c){"
        "  return fetch('http://127.0.0.1:%1/secret')"
        "    .then(function(r){ c.ok = r.ok; c.status = r.status; return r.json(); })"
        "    .then(function(data){ c.err = data.err; return c; });"
        "}";
    JsOverrideResult r =
        runOverrideScript(script.arg(port), nlohmann::json::object(), QString());
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.profile.value("ok", true), false);
    QCOMPARE(r.profile.value("status", 0), 403);
    QCOMPARE(QString::fromStdString(r.profile.value("err", std::string())),
             QString("denied"));
  }

  // 边界：fetch 头部获取（headers.get() 可用）。
  void fetchHeaders() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const quint16 port = server.serverPort();
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
      QTcpSocket* s = server.nextPendingConnection();
      if (!s) return;
      s->setParent(&server);
      QObject::connect(s, &QTcpSocket::readyRead, s, [s] {
        s->readAll();
        const QByteArray body = QByteArrayLiteral("ok");
        const QByteArray resp =
            QByteArrayLiteral("HTTP/1.1 200 OK\r\nX-Custom: sparkle\r\n"
                              "Content-Type: text/plain\r\nContent-Length: ") +
            QByteArray::number(body.size()) +
            QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body;
        s->write(resp);
        s->disconnectFromHost();
      });
    });

    const QString script =
        "function main(c){"
        "  return fetch('http://127.0.0.1:%1/h')"
        "    .then(function(r){ c.ct = r.headers.get('content-type'); c.custom = r.headers.get('x-custom'); return c; });"
        "}";
    JsOverrideResult r =
        runOverrideScript(script.arg(port), nlohmann::json::object(), QString());
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(QString::fromStdString(r.profile.value("ct", std::string())),
             QString("text/plain"));
    QCOMPARE(QString::fromStdString(r.profile.value("custom", std::string())),
             QString("sparkle"));
  }
};

QTEST_GUILESS_MAIN(TstJsEngine)
#include "tst_js_engine.moc"