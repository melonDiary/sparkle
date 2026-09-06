#include "config_manager.h"
#include "log_manager.h"
#include "paths.h"
#include "plugin_manager.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <nlohmann/json.hpp>

using namespace sparkle::core;

class TstConfigPlugin : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QVERIFY(temp_.isValid());
    QCoreApplication::setApplicationName(QStringLiteral("sparkle-test"));
    Paths::initialize(temp_.filePath(QStringLiteral("sparkle-test")), true);
  }

  void loadsYamlAndReturnsProxyNodes() {
    const QString path = temp_.filePath(QStringLiteral("config.yaml"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("mixed-port: 7890\nproxies:\n"
               "  - name: yaml-node\n"
               "    type: socks5\n"
               "    server: 127.0.0.1\n"
               "    port: 1080\n");
    file.close();

    ConfigManager manager;
    QVERIFY(manager.loadConfig(path.toStdString()));
    const auto nodes = manager.getProxyNodes();
    QCOMPARE(nodes.size(), std::size_t(1));
    QCOMPARE(nodes[0].name, QStringLiteral("yaml-node"));
    QCOMPARE(nodes[0].server, QStringLiteral("127.0.0.1"));
    QCOMPARE(nodes[0].port, 1080);
  }

  void loadsJavaScriptConfigAndEmitsChange() {
    const QString path = temp_.filePath(QStringLiteral("config.js"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("const names = ['one', 'two'];\n"
               "module.exports = { proxies: names.map(function(name, i) { return {"
               "name: name, type: 'socks5', server: 'example.com', port: 1000 + i"
               "}; }) };\n");
    file.close();

    ConfigManager manager;
    QSignalSpy changed(&manager, &ConfigManager::configChanged);
    QSignalSpy reload(&manager, &ConfigManager::reloadRequested);
    QVERIFY(manager.loadConfig(path.toStdString()));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(reload.count(), 1);

    const auto nodes = manager.getProxyNodes();
    QCOMPARE(nodes.size(), std::size_t(2));
    QCOMPARE(nodes[0].name, QStringLiteral("one"));
    QCOMPARE(nodes[1].port, 1001);
  }

  void discoversAndRunsPluginLifecycle() {
    const QString directory = temp_.filePath(QStringLiteral("plugins"));
    QVERIFY(QDir().mkpath(directory));
    const QString path = QDir(directory).filePath(QStringLiteral("lifecycle.js"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("module.exports = { id: 'lifecycle',\n"
               "onLoad: function() { sparkle.config.set('pluginState', 'loaded'); sparkle.ui.showNotification('ready'); },\n"
               "onProxyStart: function() { sparkle.config.set('pluginState', 'started'); },\n"
               "onProxyStop: function() { sparkle.config.set('pluginState', 'stopped'); },\n"
               "onUnload: function() { sparkle.config.set('pluginState', 'unloaded'); } };\n");
    file.close();

    ConfigManager config;
    LogManager log;
    PluginManager manager(directory, &log, &config);
    QString notification;
    manager.setNotificationHandler([&notification](const QString& message) {
      notification = message;
    });
    manager.discover();
    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.loadedPluginIds(), QStringList({QStringLiteral("lifecycle")}));
    QCOMPARE(config.appConfig().value("pluginState", std::string()), std::string("loaded"));
    QCOMPARE(notification, QStringLiteral("ready"));

    manager.proxyStarted();
    QCOMPARE(config.appConfig().value("pluginState", std::string()), std::string("started"));
    manager.proxyStopped();
    QCOMPARE(config.appConfig().value("pluginState", std::string()), std::string("stopped"));
    manager.unloadAll();
    QCOMPARE(config.appConfig().value("pluginState", std::string()), std::string("unloaded"));
  }

  void sandboxDoesNotExposeEscapeApis() {
    const QString directory = temp_.filePath(QStringLiteral("isolated"));
    QVERIFY(QDir().mkpath(directory));
    const QString path = QDir(directory).filePath(QStringLiteral("isolation.js"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("module.exports = { id: 'isolation', onLoad: function() {"
               "sparkle.config.set('api.require', typeof require);"
               "sparkle.config.set('api.process', typeof process);"
               "sparkle.config.set('api.fs', typeof fs);"
               "sparkle.config.set('api.sparkle', typeof sparkle);"
               "} };\n");
    file.close();

    ConfigManager config;
    LogManager log;
    PluginManager manager(directory, &log, &config);
    manager.discover();
    QCOMPARE(manager.loadAll(), 1);
    const nlohmann::json app = config.appConfig();
    QVERIFY(app.contains("api"));
    QVERIFY(app["api"].is_object());
    QCOMPARE(app["api"].value("require", std::string()), std::string("undefined"));
    QCOMPARE(app["api"].value("process", std::string()), std::string("undefined"));
    QCOMPARE(app["api"].value("fs", std::string()), std::string("undefined"));
    QCOMPARE(app["api"].value("sparkle", std::string()), std::string("object"));
  }

private:
  QTemporaryDir temp_;
};

QTEST_GUILESS_MAIN(TstConfigPlugin)
#include "tst_config_plugin.moc"
