#include "MITMManager.h"

#include "config_manager.h"
#include "log_manager.h"
#include "paths.h"

#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <nlohmann/json.hpp>

class TstMitm : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QVERIFY(temp_.isValid());
    QCoreApplication::setApplicationName(QStringLiteral("sparkle-mitm-test"));
    sparkle::core::Paths::initialize(temp_.filePath(QStringLiteral("sparkle-test")), true);
  }

  void forwardsHttpAndAppliesRequestResponseRules() {
    QTcpServer upstream;
    QVERIFY(upstream.listen(QHostAddress::LocalHost, 0));
    QByteArray upstreamRequest;
    QObject::connect(&upstream, &QTcpServer::newConnection, &upstream,
                     [&upstream, &upstreamRequest] {
                       auto* socket = upstream.nextPendingConnection();
                       socket->setParent(&upstream);
                       QObject::connect(socket, &QTcpSocket::readyRead, socket,
                                        [socket, &upstreamRequest] {
                                          upstreamRequest += socket->readAll();
                                          if (!upstreamRequest.contains("\r\n\r\n")) return;
                                          const QByteArray body = QByteArrayLiteral("hello");
                                          const QByteArray response =
                                              QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\nConnection: close\r\n\r\n") + body;
                                          socket->write(response);
                                          socket->disconnectFromHost();
                                        });
                     });

    const QString scriptDir = temp_.filePath(QStringLiteral("rules"));
    QVERIFY(QDir().mkpath(scriptDir));
    QFile script(QDir(scriptDir).filePath(QStringLiteral("01-test.js")));
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("function onRequest(req) { req.headers['X-Test'] = 'yes'; return req; }\n"
                 "function onResponse(res) { res.body = res.body.toUpperCase(); return res; }\n");
    script.close();

    sparkle::core::ConfigManager config;
    sparkle::core::LogManager log;
    sparkle::core::MITMManager mitm(&config, &log);
    mitm.setScriptDir(scriptDir);
    QVERIFY(mitm.start(0));
    QVERIFY(mitm.isRunning());
    QVERIFY(mitm.port() > 0);

    QTcpSocket client;
    QByteArray response;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&client, &QTcpSocket::readyRead, &loop, [&] { response += client.readAll(); });
    QObject::connect(&client, &QTcpSocket::connected, &loop, [&] {
      const QByteArray request =
          QByteArrayLiteral("GET http://127.0.0.1:") + QByteArray::number(upstream.serverPort()) +
          QByteArrayLiteral("/hello HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
      client.write(request);
    });
    QObject::connect(&client, &QTcpSocket::disconnected, &loop, [&] {
      response += client.readAll();
      loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    client.connectToHost(QHostAddress::LocalHost, mitm.port());
    timeout.start(5000);
    loop.exec();
    response += client.readAll();
    QVERIFY(!response.isEmpty());
    QVERIFY(upstreamRequest.contains("X-Test: yes"));
    QVERIFY(response.contains("200 OK"));
    QVERIFY(response.endsWith("HELLO"));
    mitm.stop();
  }

  void blocksRequestWithSyntheticResponse() {
    const QString scriptDir = temp_.filePath(QStringLiteral("block"));
    QVERIFY(QDir().mkpath(scriptDir));
    QFile script(QDir(scriptDir).filePath(QStringLiteral("block.js")));
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("function onRequest(req) { return { statusCode: 403, body: 'denied' }; }\n");
    script.close();

    sparkle::core::ConfigManager config;
    sparkle::core::LogManager log;
    sparkle::core::MITMManager mitm(&config, &log);
    mitm.setScriptDir(scriptDir);
    QVERIFY(mitm.start(0));

    QTcpSocket client;
    QByteArray response;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&client, &QTcpSocket::readyRead, &loop, [&] { response += client.readAll(); });
    QObject::connect(&client, &QTcpSocket::connected, &loop, [&] {
      client.write(QByteArrayLiteral("GET http://example.com/ HTTP/1.1\r\nHost: example.com\r\n\r\n"));
    });
    QObject::connect(&client, &QTcpSocket::disconnected, &loop, [&] {
      response += client.readAll();
      loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    client.connectToHost(QHostAddress::LocalHost, mitm.port());
    timeout.start(3000);
    loop.exec();
    response += client.readAll();
    QVERIFY(!response.isEmpty());
    QVERIFY(response.contains("403 Blocked"));
    QVERIFY(response.endsWith("denied"));
    mitm.stop();
  }

  void configProvidesMitmDefaultsAndSignal() {
    sparkle::core::ConfigManager config;
    QCOMPARE(config.mitmEnabled(), false);
    QCOMPARE(config.mitmPort(), quint16(8080));
    QVERIFY(config.mitmScriptDir().endsWith(QStringLiteral("/mitm")) ||
            config.mitmScriptDir().endsWith(QStringLiteral("\\mitm")));

    QSignalSpy changed(&config, &sparkle::core::ConfigManager::mitmConfigChanged);
    nlohmann::json patch;
    patch["mitm_enabled"] = true;
    patch["mitm_port"] = 18080;
    config.patchAppConfig(patch);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(config.mitmEnabled(), true);
    QCOMPARE(config.mitmPort(), quint16(18080));
  }

  void reloadsRulesExplicitly() {
    const QString scriptDir = temp_.filePath(QStringLiteral("reload"));
    QVERIFY(QDir().mkpath(scriptDir));
    const QString path = QDir(scriptDir).filePath(QStringLiteral("rule.js"));
    QFile script(path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("function onRequest(req) { return null; }\n");
    script.close();

    sparkle::core::ConfigManager config;
    sparkle::core::LogManager log;
    sparkle::core::MITMManager mitm(&config, &log);
    mitm.setScriptDir(scriptDir);
    QVERIFY(mitm.reloadScripts());
    script.close();
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("function onRequest(req) { return req; }\n");
    script.close();
    QVERIFY(mitm.reloadScripts());
  }

private:
  QTemporaryDir temp_;
};

QTEST_GUILESS_MAIN(TstMitm)
#include "tst_mitm.moc"
