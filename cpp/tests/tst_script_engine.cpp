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
#include "quickjs_raii.h"
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

// 在 JS 执行期间再次进入同一个引擎：重入守卫应抛出 ScriptError 而非破坏 Runtime。
JSValue nativeReenter(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  auto* engine = static_cast<ScriptEngine*>(JS_GetContextOpaque(ctx));
  bool rejected = false;
  try {
    engine->invokeFunction("doesNotMatter");
  } catch (const ScriptError&) {
    rejected = true;
  }
  return JS_NewBool(ctx, rejected);
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

  void reentrantExecutionIsRejected() {
    ScriptEngine engine;
    QVERIFY(engine.initialize());
    engine.registerFunction("nativeReenter", &nativeReenter);
    // 外层 evaluate 进入 JS 后，nativeReenter 再次 invokeFunction 应被重入守卫拒绝。
    QCOMPARE(engine.evaluate("nativeReenter()"), std::string("true"));
  }

  void jsValueRaiiSmoke() {
    JSRuntime* rt = JS_NewRuntime();
    QVERIFY(rt != nullptr);
    JSContext* ctx = JS_NewContext(rt);
    QVERIFY(ctx != nullptr);

    {
      JSValuePtr object(ctx, JS_NewObject(ctx));
      QVERIFY(JS_IsObject(object.get()));
      // move 语义：搬空源对象，避免二次释放。
      JSValuePtr moved = std::move(object);
      QVERIFY(JS_IsObject(moved.get()));
      QVERIFY(JS_IsUndefined(object.get()));
    }

    {
      JSValuePtr owned(ctx, JS_NewStringLen(ctx, "abc", 3));
      JSValue released = owned.release();
      QVERIFY(JS_IsUndefined(owned.get()));  // release 后不再持有
      QVERIFY(JS_IsString(released));
      JS_FreeValue(ctx, released);           // 交还后手动释放一次，验证无双重释放
    }

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
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