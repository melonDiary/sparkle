#include "yaml_store.h"

#include <QTemporaryDir>
#include <QtTest>

#include <nlohmann/json.hpp>

// 测试通用 YAML 存储的行为：缺省初始化、写后缓存、clear 后重读。
class TstYamlStore : public QObject {
  Q_OBJECT

private slots:
  void getMissingUsesDefault() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cfg.yaml"));

    sparkle::core::YamlStore<nlohmann::json> store(
        path, [] { return nlohmann::json{{"a", 1}}; },
        [](nlohmann::json j) { return j.is_object() ? j : nlohmann::json::object(); }, true);

    const nlohmann::json got = store.get();
    QCOMPARE(got.value("a", 0), 1);
  }

  void setWritesAndCaches() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cfg.yaml"));

    sparkle::core::YamlStore<nlohmann::json> store(
        path, [] { return nlohmann::json::object(); },
        [](nlohmann::json j) { return j; }, true);

    store.set(nlohmann::json{{"key", "value"}});
    QVERIFY(QFile::exists(path));
    QCOMPARE(store.get().value("key", std::string()), std::string("value"));
  }

  void clearReloadsFromFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("cfg.yaml"));

    sparkle::core::YamlStore<nlohmann::json> store(
        path, [] { return nlohmann::json::object(); },
        [](nlohmann::json j) { return j; }, true);

    store.set(nlohmann::json{{"k", 7}});
    store.clear();
    QCOMPARE(store.get().value("k", 0), 7);
  }
};

QTEST_GUILESS_MAIN(TstYamlStore)
#include "tst_yaml_store.moc"