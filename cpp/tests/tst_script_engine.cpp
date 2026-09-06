#include "script_engine.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

#include "config_manager.h"
#include "core_manager.h"
#include "log_manager.h"
#include "mihomo_api_client.h"
#include "runtime_config_factory.h"

#include <nlohmann/json.hpp>

using namespace sparkle::core;

namespace {

JSValue nativeAdd(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 2) return JS_ThrowTypeError(ctx, "nativeAdd 需要两个参数");
  int32_t left = 0;
  int32_t right = 0;
  if (JS_ToInt32(ctx, &left, argv[0]) < 0 || JS_ToInt32(ctx, &right, argv[1]) < 0) {
    return JS_EXCEPTION;
  }
  return JS_NewInt32(ctx, left + right);
}

struct ExampleClass {
  ExampleClass() = default;
};

}  // namespace

class TstScriptEngine : public QObject {
  Q_OBJECT

private slots:
  void initializeIsIdempotent() {
    ScriptEngine engine;
    QVERIFY(!engine.isInitialized());
    QVERIFY(engine.initialize());
    QVERIFY(engine.isInitialized());
    QVERIFY(engine.initialize());
  }

  void evaluateAndRegisterFunction() {
    ScriptEngine engine;
    QVERIFY(engine.initialize());
    engine.registerFunction("nativeAdd", &nativeAdd);

    QCOMPARE(engine.evaluate("nativeAdd(2, 3)"), std::string("5"));
    QCOMPARE(engine.evaluate("var answer = {ok: true, value: nativeAdd(4, 5)}; answer"),
             std::string("{\"ok\":true,\"value\":9}"));
    QCOMPARE(engine.evaluate("void 0"), std::string());

    // console.* 是绑定层提供的最小日志 API。
    QCOMPARE(engine.evaluate("console.info('hello', 42)"), std::string());
  }

  void registerClass() {
    ScriptEngine engine;
    QVERIFY(engine.initialize());
    engine.registerClass<ExampleClass>("ExampleClass");

    QCOMPARE(engine.evaluate("typeof ExampleClass"), std::string("\"function\""));
    QCOMPARE(engine.evaluate("new ExampleClass() instanceof ExampleClass"), std::string("true"));
  }

  void exceptionsAreConvertedAndReported() {
    ScriptEngine engine;
    QVERIFY(engine.initialize());
    QSignalSpy errorSpy(&engine, &ScriptEngine::scriptError);

    try {
      engine.evaluate("throw new Error('script boom')");
      QFAIL("evaluate() should throw ScriptError");
    } catch (const ScriptError& error) {
      QVERIFY(QString::fromUtf8(error.what()).contains("script boom"));
    }
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(errorSpy.at(0).at(0).toString().contains("script boom"));
  }

  void loadScriptFromFile() {
    QTemporaryFile file;
    QVERIFY(file.open());
    QVERIFY(file.write("({loaded: true, value: 7})") > 0);
    file.flush();

    ScriptEngine engine;
    QVERIFY(engine.initialize());
    QCOMPARE(engine.loadScript(file.fileName().toStdString()),
             std::string("{\"loaded\":true,\"value\":7}"));
  }

  void coreManagerBindingAndCallback() {
    ConfigManager config;
    LogManager log;
    RuntimeConfigFactory factory(&config);
    MihomoApiClient api(&config, &log);
    CoreManager core(&config, &factory, &api, &log);

    ScriptEngine engine(&log);
    QVERIFY(engine.initialize());
    engine.bindCoreManager(&core);

    QCOMPARE(engine.evaluate("core.state()"), std::string("\"stopped\""));
    QCOMPARE(engine.evaluate("core.isRunning()"), std::string("false"));
    QCOMPARE(engine.evaluate(
                 "var observed = null; core.on('crash', function(code) { observed = code; });"),
             std::string());

    // 没有配置内核二进制时，CoreManager 会同步发出 coreCrashed(-1)，
    // 用这个确定性的失败路径验证 C++ 信号能够调用 JS 回调。
    QVERIFY(!core.startCore(QStringLiteral("/tmp/nonexistent-sparkle-config.yaml")));
    QCOMPARE(engine.evaluate("observed"), std::string("-1"));
  }
};

QTEST_GUILESS_MAIN(TstScriptEngine)
#include "tst_script_engine.moc"