#include "script_engine.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <QByteArray>

#include <utility>

#include "core_manager.h"
#include "log_manager.h"

// QuickJS 是第三方 C 头文件；关闭其在 C++ 警告选项下产生的兼容性告警。
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
namespace {

QString exceptionText(JSContext* ctx) {
  JSValue exception = JS_GetException(ctx);
  QString message;

  if (JS_IsObject(exception)) {
    JSValue value = JS_GetPropertyStr(ctx, exception, "message");
    if (!JS_IsException(value) && !JS_IsUndefined(value)) {
      const char* text = JS_ToCString(ctx, value);
      if (text) {
        message = QString::fromUtf8(text);
        JS_FreeCString(ctx, text);
      }
    }
    JS_FreeValue(ctx, value);

    value = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsException(value) && !JS_IsUndefined(value)) {
      const char* text = JS_ToCString(ctx, value);
      if (text) {
        const QString stack = QString::fromUtf8(text);
        if (!stack.isEmpty() && stack != message) {
          message += message.isEmpty() ? QString() : QStringLiteral("\n");
          message += stack;
        }
        JS_FreeCString(ctx, text);
      }
    }
    JS_FreeValue(ctx, value);
  }

  if (message.isEmpty()) {
    const char* text = JS_ToCString(ctx, exception);
    if (text) {
      message = QString::fromUtf8(text);
      JS_FreeCString(ctx, text);
    }
  }
  JS_FreeValue(ctx, exception);
  return message.isEmpty() ? QStringLiteral("未知的 JavaScript 异常") : message;
}

QString jsString(JSContext* ctx, JSValueConst value) {
  const char* text = JS_ToCString(ctx, value);
  if (!text) return QString();
  const QString result = QString::fromUtf8(text);
  JS_FreeCString(ctx, text);
  return result;
}

QString eventNameFromValue(JSContext* ctx, JSValueConst value) {
  if (!JS_IsString(value)) return QString();
  return jsString(ctx, value).trimmed().toLower();
}

}  // namespace

ScriptEngine::ScriptEngine(LogManager* log, QObject* parent)
    : QObject(parent), log_(log) {}

ScriptEngine::~ScriptEngine() {
  // 先断开 Qt 信号，再释放仍被 C++ 回调持有的 JS 函数引用。
  disconnectCoreSignals();
  clearCallbacks();
}

bool ScriptEngine::initialize() {
  if (isInitialized()) return true;

  runtime_.reset(JS_NewRuntime());
  if (!runtime_) {
    recordError(QStringLiteral("无法创建 QuickJS Runtime"));
    return false;
  }

  // 限制脚本资源，避免用户脚本无限消耗内存或递归栈。
  JS_SetMemoryLimit(runtime_.get(), 256ull * 1024 * 1024);
  JS_SetMaxStackSize(runtime_.get(), 512 * 1024);

  ctx_.reset(JS_NewContext(runtime_.get()));
  if (!ctx_) {
    runtime_.reset();
    recordError(QStringLiteral("无法创建 QuickJS Context"));
    return false;
  }

  JS_SetContextOpaque(ctx_.get(), this);
  try {
    installConsoleObject();
    installUiObject();
  } catch (const ScriptError&) {
    ctx_.reset();
    runtime_.reset();
    return false;
  }
  return true;
}

bool ScriptEngine::isInitialized() const {
  return runtime_ != nullptr && ctx_ != nullptr;
}

void ScriptEngine::ensureInitialized() const {
  if (!isInitialized()) {
    throw ScriptError("QuickJS 尚未初始化，请先调用 initialize()");
  }
}

void ScriptEngine::throwScriptError(const QString& message) {
  recordError(message);
  throw ScriptError(message.toStdString());
}

void ScriptEngine::recordError(const QString& message) {
  if (log_) {
    log_->appendAppLog(QStringLiteral("[ScriptEngine] %1\n").arg(message));
  }
  emit scriptError(message);
}

std::string ScriptEngine::evaluate(const std::string& code) {
  ensureInitialized();

  JSValue result = JS_Eval(ctx_.get(), code.c_str(), code.size(), "<evaluate>",
                           JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) {
    const QString message = exceptionText(ctx_.get());
    JS_FreeValue(ctx_.get(), result);
    throwScriptError(message);
  }

  if (JS_IsUndefined(result)) {
    JS_FreeValue(ctx_.get(), result);
    return {};
  }

  // 使用 JSON 作为 C++/JS 边界格式：对象、数组、数字、布尔值都能无损返回。
  JSValue json = JS_JSONStringify(ctx_.get(), result, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(ctx_.get(), result);
  if (JS_IsException(json)) {
    const QString message = exceptionText(ctx_.get());
    JS_FreeValue(ctx_.get(), json);
    throwScriptError(message);
  }

  const char* text = JS_ToCString(ctx_.get(), json);
  if (!text) {
    JS_FreeValue(ctx_.get(), json);
    throwScriptError(QStringLiteral("JavaScript 结果无法转换为字符串"));
  }
  const std::string output(text);
  JS_FreeCString(ctx_.get(), text);
  JS_FreeValue(ctx_.get(), json);
  return output;
}

void ScriptEngine::registerFunction(const std::string& name, JSCFunction* func) {
  ensureInitialized();
  if (name.empty() || func == nullptr) {
    throwScriptError(QStringLiteral("注册 JavaScript 函数失败：名称或函数指针为空"));
  }

  JSValue function = JS_NewCFunction(ctx_.get(), func, name.c_str(), 0);
  if (JS_IsException(function)) {
    throwScriptError(QStringLiteral("创建 JavaScript 函数失败：") +
                     QString::fromStdString(name));
  }

  JSValue global = JS_GetGlobalObject(ctx_.get());
  // JS_SetPropertyStr 会接管 function 的所有权；失败时不能再次 free，避免双重释放。
  const int result = JS_SetPropertyStr(ctx_.get(), global, name.c_str(), function);
  JS_FreeValue(ctx_.get(), global);
  if (result < 0) {
    throwScriptError(QStringLiteral("导出 JavaScript 函数失败：") +
                     QString::fromStdString(name));
  }
}

std::string ScriptEngine::loadScript(const std::string& path) {
  ensureInitialized();

  QFile file(QString::fromStdString(path));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    const QString message = QStringLiteral("无法读取 JavaScript 文件：%1").arg(
        QString::fromStdString(path));
    throwScriptError(message);
  }

  const QByteArray source = file.readAll();
  return evaluate(source.toStdString());
}

std::string ScriptEngine::invokeFunction(const std::string& name) {
  ensureInitialized();
  if (name.empty()) {
    throwScriptError(QStringLiteral("调用 JavaScript 函数失败：名称为空"));
  }

  JSValue global = JS_GetGlobalObject(ctx_.get());
  JSValue function = JS_GetPropertyStr(ctx_.get(), global, name.c_str());
  JS_FreeValue(ctx_.get(), global);
  if (JS_IsException(function)) {
    const QString message = exceptionText(ctx_.get());
    JS_FreeValue(ctx_.get(), function);
    throwScriptError(message);
  }
  if (!JS_IsFunction(ctx_.get(), function)) {
    JS_FreeValue(ctx_.get(), function);
    throwScriptError(QStringLiteral("JavaScript 函数不存在：") + QString::fromStdString(name));
  }

  JSValue result = JS_Call(ctx_.get(), function, JS_UNDEFINED, 0, nullptr);
  JS_FreeValue(ctx_.get(), function);
  if (JS_IsException(result)) {
    const QString message = exceptionText(ctx_.get());
    JS_FreeValue(ctx_.get(), result);
    throwScriptError(message);
  }
  if (JS_IsUndefined(result)) {
    JS_FreeValue(ctx_.get(), result);
    return {};
  }

  JSValue json = JS_JSONStringify(ctx_.get(), result, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(ctx_.get(), result);
  if (JS_IsException(json)) {
    const QString message = exceptionText(ctx_.get());
    JS_FreeValue(ctx_.get(), json);
    throwScriptError(message);
  }
  const char* text = JS_ToCString(ctx_.get(), json);
  if (!text) {
    JS_FreeValue(ctx_.get(), json);
    throwScriptError(QStringLiteral("JavaScript 函数返回值无法转换为字符串"));
  }
  const std::string output(text);
  JS_FreeCString(ctx_.get(), text);
  JS_FreeValue(ctx_.get(), json);
  return output;
}

void ScriptEngine::setUiStatusHandler(std::function<void(const QString&)> handler) {
  uiStatusHandler_ = std::move(handler);
}

void ScriptEngine::setLogManager(LogManager* log) { log_ = log; }

void ScriptEngine::bindCoreManager(CoreManager* core) {
  ensureInitialized();
  if (!core) {
    throwScriptError(QStringLiteral("绑定 CoreManager 失败：实例为空"));
  }

  disconnectCoreSignals();
  clearCallbacks();
  core_ = core;
  installCoreObject();

  coreConnections_.push_back(connect(core_, &CoreManager::logReceived, this,
                                     &ScriptEngine::onCoreLog));
  coreConnections_.push_back(connect(core_, &CoreManager::coreCrashed, this,
                                     &ScriptEngine::onCoreCrash));
  coreConnections_.push_back(connect(core_, &CoreManager::coreStarted, this,
                                     &ScriptEngine::onCoreStarted));
  coreConnections_.push_back(connect(core_, &CoreManager::coreStopped, this,
                                     &ScriptEngine::onCoreStopped));
  coreConnections_.push_back(connect(core_, &CoreManager::stateChanged, this,
                                     &ScriptEngine::onCoreStateChanged));
}

JSValue ScriptEngine::jsConsoleLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv,
                                    int magic) {
  auto* engine = fromContext(ctx);
  QStringList values;
  values.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    const char* text = JS_ToCString(ctx, argv[i]);
    if (!text) return JS_EXCEPTION;
    values << QString::fromUtf8(text);
    JS_FreeCString(ctx, text);
  }

  const QString level = magic == 1 ? QStringLiteral("info")
                                   : magic == 2 ? QStringLiteral("debug")
                                                : magic == 3 ? QStringLiteral("error")
                                                             : QStringLiteral("log");
  if (engine && engine->log_) {
    engine->log_->appendAppLog(QStringLiteral("[script:%1] %2\\n").arg(level, values.join(' ')));
  }
  return JS_UNDEFINED;
}

void ScriptEngine::installConsoleObject() {
  JSValue console = JS_NewObject(ctx_.get());
  if (JS_IsException(console)) {
    throwScriptError(QStringLiteral("创建 JavaScript console 对象失败"));
  }
  const auto addConsoleFunction = [this, &console](const char* name, int magic) {
    JSValue value = JS_NewCFunctionMagic(ctx_.get(), &ScriptEngine::jsConsoleLog, name, 1,
                                         JS_CFUNC_generic_magic, magic);
    if (JS_IsException(value)) {
      throwScriptError(QStringLiteral("创建 console.%1 函数失败").arg(QString::fromLatin1(name)));
    }
    // JS_SetPropertyStr 会接管 value 的所有权；失败时不能再次释放它。
    if (JS_SetPropertyStr(ctx_.get(), console, name, value) < 0) {
      throwScriptError(QStringLiteral("绑定 console.%1 失败").arg(QString::fromLatin1(name)));
    }
  };
  addConsoleFunction("log", 0);
  addConsoleFunction("info", 1);
  addConsoleFunction("debug", 2);
  addConsoleFunction("error", 3);
  JSValue global = JS_GetGlobalObject(ctx_.get());
  if (JS_SetPropertyStr(ctx_.get(), global, "console", console) < 0) {
    JS_FreeValue(ctx_.get(), global);
    throwScriptError(QStringLiteral("导出 JavaScript console 对象失败"));
  }
  JS_FreeValue(ctx_.get(), global);
}

void ScriptEngine::installUiObject() {
  JSValue uiObject = JS_NewObject(ctx_.get());
  if (JS_IsException(uiObject)) {
    throwScriptError(QStringLiteral("创建 JavaScript ui 对象失败"));
  }
  JSValue status = JS_NewCFunction(ctx_.get(), &ScriptEngine::jsUiStatus, "status", 1);
  if (JS_IsException(status) || JS_SetPropertyStr(ctx_.get(), uiObject, "status", status) < 0) {
    throwScriptError(QStringLiteral("绑定 ui.status 失败"));
  }
  JSValue global = JS_GetGlobalObject(ctx_.get());
  if (JS_SetPropertyStr(ctx_.get(), global, "ui", uiObject) < 0) {
    JS_FreeValue(ctx_.get(), global);
    throwScriptError(QStringLiteral("导出全局 ui 对象失败"));
  }
  JS_FreeValue(ctx_.get(), global);
}

void ScriptEngine::installCoreObject() {
  JSValue coreObject = JS_NewObject(ctx_.get());
  if (JS_IsException(coreObject)) {
    throwScriptError(QStringLiteral("创建 JavaScript core 对象失败"));
  }

  const auto addFunction = [this, &coreObject](const char* name, JSCFunction* function,
                                                int length) {
    JSValue value = JS_NewCFunction(ctx_.get(), function, name, length);
    if (JS_IsException(value)) {
      throwScriptError(QStringLiteral("创建 core.%1 函数失败").arg(QString::fromLatin1(name)));
    }
    // JS_SetPropertyStr 会接管 value 的所有权；失败时不能再次释放它。
    if (JS_SetPropertyStr(ctx_.get(), coreObject, name, value) < 0) {
      throwScriptError(QStringLiteral("绑定 core.%1 失败").arg(QString::fromLatin1(name)));
    }
  };

  addFunction("start", &ScriptEngine::jsCoreStart, 1);
  addFunction("stop", &ScriptEngine::jsCoreStop, 0);
  addFunction("restart", &ScriptEngine::jsCoreRestart, 0);
  addFunction("isRunning", &ScriptEngine::jsCoreIsRunning, 0);
  addFunction("state", &ScriptEngine::jsCoreState, 0);
  addFunction("on", &ScriptEngine::jsCoreOn, 2);

  JSValue global = JS_GetGlobalObject(ctx_.get());
  if (JS_SetPropertyStr(ctx_.get(), global, "core", coreObject) < 0) {
    JS_FreeValue(ctx_.get(), global);
    throwScriptError(QStringLiteral("导出全局 core 对象失败"));
  }
  JS_FreeValue(ctx_.get(), global);
}

ScriptEngine* ScriptEngine::fromContext(JSContext* ctx) {
  return static_cast<ScriptEngine*>(JS_GetContextOpaque(ctx));
}

JSValue ScriptEngine::jsUiStatus(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->uiStatusHandler_ || argc < 1) return JS_UNDEFINED;
  try {
    engine->uiStatusHandler_(jsString(ctx, argv[0]));
    return JS_UNDEFINED;
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "ui.status 调用失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "ui.status 调用失败");
  }
}

JSValue ScriptEngine::jsCoreStart(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->core_) return JS_ThrowInternalError(ctx, "CoreManager 尚未绑定");
  try {
    bool started = false;
    if (argc > 0 && JS_IsString(argv[0])) {
      const QString path = jsString(ctx, argv[0]);
      started = engine->core_->startCore(path);
    } else {
      engine->core_->startup();
      started = engine->core_->state() == CoreState::Starting ||
                engine->core_->state() == CoreState::Running;
    }
    return JS_NewBool(ctx, started);
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "core.start 调用失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "core.start 调用失败");
  }
}

JSValue ScriptEngine::jsCoreStop(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->core_) return JS_ThrowInternalError(ctx, "CoreManager 尚未绑定");
  try {
    return JS_NewBool(ctx, engine->core_->stopCore());
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "core.stop 调用失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "core.stop 调用失败");
  }
}

JSValue ScriptEngine::jsCoreRestart(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->core_) return JS_ThrowInternalError(ctx, "CoreManager 尚未绑定");
  try {
    engine->core_->restart();
    return JS_UNDEFINED;
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "core.restart 调用失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "core.restart 调用失败");
  }
}

JSValue ScriptEngine::jsCoreIsRunning(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->core_) return JS_ThrowInternalError(ctx, "CoreManager 尚未绑定");
  try {
    return JS_NewBool(ctx, engine->core_->isRunning());
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "core.isRunning 调用失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "core.isRunning 调用失败");
  }
}

JSValue ScriptEngine::jsCoreState(JSContext* ctx, JSValueConst, int, JSValueConst*) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->core_) return JS_ThrowInternalError(ctx, "CoreManager 尚未绑定");
  try {
    const QByteArray state = toString(engine->core_->state()).toUtf8();
    return JS_NewStringLen(ctx, state.constData(), state.size());
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "core.state 调用失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "core.state 调用失败");
  }
}

JSValue ScriptEngine::jsCoreOn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto* engine = fromContext(ctx);
  if (!engine || !engine->core_) return JS_ThrowInternalError(ctx, "CoreManager 尚未绑定");
  if (argc < 2 || !JS_IsString(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
    return JS_ThrowTypeError(ctx, "core.on(event, callback) 参数无效");
  }

  const QString event = eventNameFromValue(ctx, argv[0]);
  if (event.isEmpty()) return JS_ThrowTypeError(ctx, "core.on 的事件名不能为空");
  if (event != QLatin1String("log") && event != QLatin1String("crash") &&
      event != QLatin1String("started") && event != QLatin1String("stopped") &&
      event != QLatin1String("state")) {
    return JS_ThrowRangeError(ctx, "不支持的 core 事件：%s", event.toUtf8().constData());
  }

  try {
    ScriptEngine::JsCallback callback;
    callback.event = event;
    callback.function = JS_DupValue(ctx, argv[1]);
    engine->callbacks_.push_back(callback);
    return JS_UNDEFINED;
  } catch (const std::exception& error) {
    return JS_ThrowInternalError(ctx, "注册 core 回调失败：%s", error.what());
  } catch (...) {
    return JS_ThrowInternalError(ctx, "注册 core 回调失败");
  }
}

void ScriptEngine::disconnectCoreSignals() {
  for (const auto& connection : coreConnections_) QObject::disconnect(connection);
  coreConnections_.clear();
  core_ = nullptr;
}

void ScriptEngine::clearCallbacks() {
  if (!ctx_) {
    callbacks_.clear();
    return;
  }
  for (auto& callback : callbacks_) {
    if (!JS_IsUndefined(callback.function)) JS_FreeValue(ctx_.get(), callback.function);
    callback.function = JS_UNDEFINED;
  }
  callbacks_.clear();
}

void ScriptEngine::invokeCallbacks(const QString& event, const std::vector<JSValue>& args) {
  if (!ctx_) return;

  // 先复制引用，避免回调执行过程中再次 core.on() 导致 vector 扩容而使迭代器失效。
  std::vector<JSValue> functions;
  for (const auto& callback : callbacks_) {
    if (callback.event == event && !JS_IsUndefined(callback.function)) {
      functions.push_back(JS_DupValue(ctx_.get(), callback.function));
    }
  }

  for (const JSValue function : functions) {
    std::vector<JSValue> callArgs(args.begin(), args.end());
    JSValue result = JS_Call(ctx_.get(), function, JS_UNDEFINED,
                             static_cast<int>(callArgs.size()),
                             callArgs.empty() ? nullptr : callArgs.data());
    JS_FreeValue(ctx_.get(), function);
    if (JS_IsException(result)) {
      const QString message = QStringLiteral("core.%1 回调执行失败：%2")
                                  .arg(event, exceptionText(ctx_.get()));
      JS_FreeValue(ctx_.get(), result);
      recordError(message);
      continue;
    }
    JS_FreeValue(ctx_.get(), result);
  }
}

void ScriptEngine::onCoreLog(const QString& line) {
  const QByteArray value = line.toUtf8();
  const JSValue argument = JS_NewStringLen(ctx_.get(), value.constData(), value.size());
  invokeCallbacks(QStringLiteral("log"), {argument});
  JS_FreeValue(ctx_.get(), argument);
}

void ScriptEngine::onCoreCrash(int exitCode) {
  const JSValue argument = JS_NewInt32(ctx_.get(), exitCode);
  invokeCallbacks(QStringLiteral("crash"), {argument});
  JS_FreeValue(ctx_.get(), argument);
}

void ScriptEngine::onCoreStarted() { invokeCallbacks(QStringLiteral("started"), {}); }

void ScriptEngine::onCoreStopped() { invokeCallbacks(QStringLiteral("stopped"), {}); }

void ScriptEngine::onCoreStateChanged(CoreState state) {
  const QByteArray value = toString(state).toUtf8();
  const JSValue argument = JS_NewStringLen(ctx_.get(), value.constData(), value.size());
  invokeCallbacks(QStringLiteral("state"), {argument});
  JS_FreeValue(ctx_.get(), argument);
}

}  // namespace sparkle::core