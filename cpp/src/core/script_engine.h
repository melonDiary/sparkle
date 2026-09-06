#pragma once

#include <QObject>
#include <QMetaObject>
#include <QString>

#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <functional>
#include <type_traits>
#include <vector>

#include "models.h"

// 本项目没有引入 quickjspp/quickjscpp；QuickJS 由 CMake FetchContent/vcpkg 提供。
// 这里用一层小型 C++ RAII 门面封装原生 C API，避免额外依赖和隐藏的所有权。
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

namespace sparkle::core {

class CoreManager;
class LogManager;

// JS 执行失败时统一转换成 C++ 异常，调用方可以按普通 C++ 异常处理。
class ScriptError final : public std::runtime_error {
public:
  explicit ScriptError(const std::string& message) : std::runtime_error(message) {}
};

// QuickJS 运行时与上下文的 RAII 封装。
//
// evaluate() 返回 JS 值的 JSON 文本：undefined 返回空字符串，字符串返回 JSON 字符串
//（包含引号），对象/数组返回 JSON 对象/数组文本。脚本异常会抛出 ScriptError。
class ScriptEngine final : public QObject {
  Q_OBJECT
public:
  explicit ScriptEngine(LogManager* log = nullptr, QObject* parent = nullptr);
  ~ScriptEngine() override;

  // 创建 JSRuntime/JSContext。重复初始化是幂等的；创建失败返回 false 并记录日志。
  bool initialize();
  bool isInitialized() const;

  // 执行一段全局 JavaScript。语法错误、运行时异常、JSON 序列化异常均转换为 ScriptError。
  std::string evaluate(const std::string& code);

  // 注册一个全局 C 函数。func 的生命周期由调用方保证（通常是静态函数指针）。
  void registerFunction(const std::string& name, JSCFunction* func);

  // 注册一个可通过 `new Name()` 创建的默认构造 C++ 类。
  // 类实例以 opaque 指针挂在 JS 对象上，并在 JS 垃圾回收时 delete，遵循 RAII。
  template <typename T>
  void registerClass(const std::string& name) {
    static_assert(std::is_default_constructible_v<T>,
                  "ScriptEngine::registerClass<T> requires a default constructor");
    ensureInitialized();

    JSClassID& id = classId<T>();
    if (id == JS_INVALID_CLASS_ID) JS_NewClassID(&id);

    // 每个 ScriptEngine 都有独立的 JSRuntime；同一个 T 在不同 runtime 中各注册一次。
    const std::string className = name;
    classNames_.push_back(className);  // deque 保证已有字符串地址不会因后续插入而失效。
    const char* stableName = classNames_.back().c_str();
    JSClassDef def{};
    def.class_name = stableName;
    def.finalizer = &ScriptEngine::classFinalizer<T>;
    if (!JS_IsRegisteredClass(runtime_.get(), id) &&
        JS_NewClass(runtime_.get(), id, &def) < 0) {
      throwScriptError(QStringLiteral("注册 QuickJS 类失败：") + QString::fromStdString(name));
    }

    JSValue proto = JS_NewObject(ctx_.get());
    if (JS_IsException(proto)) {
      throwScriptError(QStringLiteral("创建 QuickJS 类原型失败：") + QString::fromStdString(name));
    }
    JS_SetClassProto(ctx_.get(), id, proto);

    JSValue ctor = JS_NewCFunction2(ctx_.get(), &ScriptEngine::classConstructor<T>, stableName,
                                   0, JS_CFUNC_constructor, 0);
    if (JS_IsException(ctor)) {
      throwScriptError(QStringLiteral("创建 QuickJS 构造函数失败：") +
                       QString::fromStdString(name));
    }
    JS_SetConstructor(ctx_.get(), ctor, proto);

    JSValue global = JS_GetGlobalObject(ctx_.get());
    if (JS_SetPropertyStr(ctx_.get(), global, stableName, ctor) < 0) {
      JS_FreeValue(ctx_.get(), global);
      throwScriptError(QStringLiteral("导出 QuickJS 类失败：") + QString::fromStdString(name));
    }
    JS_FreeValue(ctx_.get(), global);
  }

  // 把 CoreManager 暴露为全局 `core` 对象，并订阅其生命周期/日志信号。
  // JS API：core.start([configPath])、core.stop()、core.restart()、core.isRunning()、
  // core.state()、core.on("log"|"crash"|"started"|"stopped"|"state", callback)。
  void bindCoreManager(CoreManager* core);

  // 从文件读取并执行全局脚本；文件读取失败或 JS 执行失败会抛出 ScriptError。
  std::string loadScript(const std::string& path);

  // 调用脚本导出的全局函数。用于 QML 按钮把行为委托给用户脚本。
  std::string invokeFunction(const std::string& name);

  // 注入一个受限 UI 回调；脚本只能通过 uiStatus(message) 更新展示状态。
  void setUiStatusHandler(std::function<void(const QString&)> handler);

  void setLogManager(LogManager* log);

signals:
  void scriptError(const QString& message);

private:
  struct RuntimeDeleter {
    void operator()(JSRuntime* runtime) const noexcept {
      if (runtime) JS_FreeRuntime(runtime);
    }
  };
  struct ContextDeleter {
    void operator()(JSContext* context) const noexcept {
      if (context) JS_FreeContext(context);
    }
  };
  using RuntimePtr = std::unique_ptr<JSRuntime, RuntimeDeleter>;
  using ContextPtr = std::unique_ptr<JSContext, ContextDeleter>;

  struct JsCallback {
    QString event;
    JSValue function = JS_UNDEFINED;  // 由 JS_DupValue 持有，析构前在 context 中释放。
  };

  template <typename T>
  static JSClassID& classId() {
    static JSClassID id = JS_INVALID_CLASS_ID;
    return id;
  }

  template <typename T>
  static JSValue classConstructor(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    try {
      JSValue object = JS_NewObjectClass(ctx, classId<T>());
      if (JS_IsException(object)) return object;
      JS_SetOpaque(object, new T());
      return object;
    } catch (const std::exception& error) {
      return JS_ThrowInternalError(ctx, "创建 C++ 类实例失败：%s", error.what());
    } catch (...) {
      return JS_ThrowInternalError(ctx, "创建 C++ 类实例失败");
    }
  }

  template <typename T>
  static void classFinalizer(JSRuntime*, JSValue value) {
    // finalizer 没有 JSContext，只能使用 class id 取出 opaque 指针并释放 C++ 对象。
    delete static_cast<T*>(JS_GetOpaque(value, classId<T>()));
  }

  static ScriptEngine* fromContext(JSContext* ctx);
  static JSValue jsConsoleLog(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                               int magic);
  static JSValue jsCoreStart(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsCoreStop(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsCoreRestart(JSContext* ctx, JSValueConst thisVal, int argc,
                               JSValueConst* argv);
  static JSValue jsCoreIsRunning(JSContext* ctx, JSValueConst thisVal, int argc,
                                 JSValueConst* argv);
  static JSValue jsCoreState(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsCoreOn(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsUiStatus(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);

  void ensureInitialized() const;
  void throwScriptError(const QString& message);
  void recordError(const QString& message);
  void installConsoleObject();
  void installCoreObject();
  void installUiObject();
  void disconnectCoreSignals();
  void clearCallbacks();
  void invokeCallbacks(const QString& event, const std::vector<JSValue>& args);

  void onCoreLog(const QString& line);
  void onCoreCrash(int exitCode);
  void onCoreStarted();
  void onCoreStopped();
  void onCoreStateChanged(CoreState state);

  // 类名字符串必须比 QuickJS Runtime 活得更久：QuickJS 的 class definition
  // 可能在 runtime 销毁期间仍访问 class_name。
  std::deque<std::string> classNames_;
  RuntimePtr runtime_;
  ContextPtr ctx_;
  LogManager* log_ = nullptr;       // 非拥有；由 AppController 持有
  CoreManager* core_ = nullptr;     // 非拥有；由 AppController 持有
  std::function<void(const QString&)> uiStatusHandler_;
  std::vector<QMetaObject::Connection> coreConnections_;
  std::vector<JsCallback> callbacks_;
};

}  // namespace sparkle::core