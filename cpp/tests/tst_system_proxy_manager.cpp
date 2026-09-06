#include "SystemProxyManager.h"

#include "config_manager.h"
#include "system_proxy.h"
#include "paths.h"

#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace sparkle::core;

namespace {

class FakeSystemProxy final : public sparkle::platform::ISystemProxy {
public:
  void setManualProxy(const QString& host, unsigned short port,
                      const QStringList& bypass) override {
    ++manualCalls;
    lastHost = host;
    lastPort = port;
    lastBypass = bypass;
    currentStatus = sparkle::platform::ProxyStatus::Manual;
  }

  void setAutoProxy(const QUrl& pacUrl) override {
    ++autoCalls;
    lastPacUrl = pacUrl;
    currentStatus = sparkle::platform::ProxyStatus::Auto;
  }

  void clearProxy() override {
    ++clearCalls;
    currentStatus = sparkle::platform::ProxyStatus::Disabled;
  }

  sparkle::platform::ProxyStatus status() override { return currentStatus; }
  void setGuardEnabled(bool, bool) override {}

  int manualCalls = 0;
  int autoCalls = 0;
  int clearCalls = 0;
  QString lastHost;
  unsigned short lastPort = 0;
  QStringList lastBypass;
  QUrl lastPacUrl;
  sparkle::platform::ProxyStatus currentStatus = sparkle::platform::ProxyStatus::Disabled;
};

}  // namespace

class TstSystemProxyManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QVERIFY(temp_.isValid());
    QCoreApplication::setApplicationName(QStringLiteral("sparkle-system-proxy-test"));
    Paths::initialize(temp_.filePath(QStringLiteral("sparkle-test")), true);
  }

  void appliesManualProxyAndClearsIt() {
    ConfigManager config;
    config.replaceControlledMihomoConfig(nlohmann::json{{"mixed-port", 7890}});
    SysProxyConfig proxyConfig = config.sysProxyConfig();
    proxyConfig.host = QStringLiteral("127.0.0.2");
    proxyConfig.mode = SysProxyMode::Manual;
    proxyConfig.bypass = {QStringLiteral("localhost"), QStringLiteral("internal.test")};
    config.setSysProxyConfig(proxyConfig);

    auto backend = std::make_unique<FakeSystemProxy>();
    FakeSystemProxy* backendPtr = backend.get();
    SystemProxyManager manager(&config, std::move(backend));
    QSignalSpy stateSpy(&manager, &SystemProxyManager::proxyStateChanged);

    manager.setProxy(true);
    QCOMPARE(backendPtr->manualCalls, 1);
    QCOMPARE(backendPtr->lastHost, QStringLiteral("127.0.0.2"));
    QCOMPARE(backendPtr->lastPort, static_cast<unsigned short>(7890));
    QCOMPARE(backendPtr->lastBypass, proxyConfig.bypass);
    QVERIFY(manager.isProxyEnabled());
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(0).toBool(), true);

    manager.clearProxy();
    QCOMPARE(backendPtr->clearCalls, 1);
    QVERIFY(!manager.isProxyEnabled());
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(stateSpy.at(1).at(0).toBool(), false);
  }

  void appliesPacProxyWhenConfiguredForAutoMode() {
    ConfigManager config;
    config.replaceControlledMihomoConfig(nlohmann::json{{"mixed-port", 7891}});
    SysProxyConfig proxyConfig = config.sysProxyConfig();
    proxyConfig.mode = SysProxyMode::Auto;
    config.setSysProxyConfig(proxyConfig);

    auto backend = std::make_unique<FakeSystemProxy>();
    FakeSystemProxy* backendPtr = backend.get();
    SystemProxyManager manager(&config, std::move(backend));

    manager.setProxy(true);
    QCOMPARE(backendPtr->autoCalls, 1);
    QVERIFY(backendPtr->lastPacUrl.isValid());
    QCOMPARE(backendPtr->lastPacUrl.scheme(), QStringLiteral("http"));
    QCOMPARE(backendPtr->lastPacUrl.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(backendPtr->lastPacUrl.port() > 0);
    QVERIFY(backendPtr->lastPacUrl.path() == QStringLiteral("/pac"));
    QVERIFY(manager.isEnabled());

    manager.disable();
    QCOMPARE(backendPtr->clearCalls, 1);
    QVERIFY(!manager.isEnabled());
  }

  void changingDesiredStateInvalidatesRetry() {
    ConfigManager config;
    config.replaceControlledMihomoConfig(nlohmann::json::object());
    auto backend = std::make_unique<FakeSystemProxy>();
    FakeSystemProxy* backendPtr = backend.get();
    SystemProxyManager manager(&config, std::move(backend));

    manager.setProxy(true);
    manager.setProxy(false);
    QTest::qWait(5100);

    // The delayed retry from the first request must not re-enable the proxy.
    QCOMPARE(backendPtr->autoCalls, 1);
    QCOMPARE(backendPtr->clearCalls, 1);
    QVERIFY(!manager.isProxyEnabled());
  }

private:
  QTemporaryDir temp_;
};

QTEST_GUILESS_MAIN(TstSystemProxyManager)
#include "tst_system_proxy_manager.moc"
