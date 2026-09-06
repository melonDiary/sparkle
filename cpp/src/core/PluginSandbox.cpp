#include "plugin_sandbox.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QDebug>

#include <nlohmann/json.hpp>

#include "config_manager.h"
#include "log_manager.h"

namespace sparkle::core {
namespace {
using json = nlohmann::json;

QString jsToString(JSContext* ctx, JSValueConst value) {
  const char* text = JS_ToCString(ctx, value);
  if (!text) return {};
  const QString result = QString::fromUtf8(text);
  JS_FreeCString(ctx, text);
  return result;
}

JSValue jsonToJs(JSContext* ctx, const json& value) {
  const std::string text = value.dump();
  return JS_ParseJSON(ctx, text.c_str(), text.size(), "<plugin-json>");
}

json jsToJson(JSContext* ctx, JSValueConst value) {
  JSValue serialized = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
  if (JS_IsException(serialized)) return nullptr;
  const char* text = JS_ToCString(ctx, serialized);
  if (!text) {
    JS_FreeValue(ctx, serialized);
    return nullptr;
  }
  json result;
  try {
    result = json::parse(text);
  } catch (...) {
    result = json(json::value_t::discarded);
  }
  JS_FreeCString(ctx, text);
  JS_FreeValue(ctx, serialized);
  return result;
}

int interruptHandler(JSRuntime*, void* opaque) {
  const auto* deadline = static_cast<const qint64*>(opaque);
  return QDateTime::currentMSecsSinceEpoch() > *deadline ? 1 : 0;
}

json getPath(json value, const QString& path) {
  for (const QString& part : path.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
    if (!value.is_object() || !value.contains(part.toStdString())) return nullptr;
    value = value[part.toStdString()];
  }
  return value;
}

void setPath(json& value, const QString& path, const json& replacement) {
  const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
  if (parts.isEmpty()) return;
  json* current = &value;
  for (int i = 0; i < parts.size() - 1; ++i) {
    (*current)[parts[i].toStdString()] = current->value(parts[i].toStdString(), json::object());
    current = &(*current)[parts[i].toStdString()];
  }
  (*current)[parts.back().toStdString()] = replacement;
}

}  // namespace

void PluginSandbox::RuntimeDeleter::operator()(JSRuntime* runtime) const noexcept {
  if (runtime) JS_FreeRuntime(runtime);
}

void PluginSandbox::ContextDeleter::operator()(JSContext* context) const noexcept {
  if (context) JS_FreeContext(context);
}

PluginSandbox::PluginSandbox(QString pluginPath, LogManager* log, ConfigManager* config,
                               NotificationHandler notificationHandler)
    : path_(QFileInfo(pluginPath).absoluteFilePath()),
      id_(QFileInfo(path_).completeBaseName()),
      log_(log),
      config_(config),
      notificationHandler_(std::move(notificationHandler)) {}

PluginSandbox::~PluginSandbox() { unload(); }

QString PluginSandbox::id() const { return id_; }
QString PluginSandbox::filePath() const { return path_; }
QString PluginSandbox::lastError() const { return error_; }
bool PluginSandbox::isLoaded() const { return loaded_; }

void PluginSandbox::setNotificationHandler(NotificationHandler handler) {
  notificationHandler_ = std::move(handler);
}

void PluginSandbox::setError(const QString& message) {
  error_ = message;
  if (log_) log_->appendAppLog(QStringLiteral("[Plugin:%1] %2\n").arg(id_, message));
}

PluginSandbox* PluginSandbox::fromContext(JSContext* ctx) {
  return static_cast<PluginSandbox*>(JS_GetContextOpaque(ctx));
}

JSValue PluginSandbox::jsLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto* plugin = fromContext(ctx);
  QStringList values;
  for (int i = 0; i < argc; ++i) values << jsToString(ctx, argv[i]);
  if (plugin && plugin->log_) {
    plugin->log_->appendAppLog(QStringLiteral("[plugin:%1] %2\n")
                                   .arg(plugin->id_, values.join(QLatin1Char(' '))));
  }
  return JS_UNDEFINED;
}

JSValue PluginSandbox::jsConfigGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto* plugin = fromContext(ctx);
  if (!plugin || !plugin->config_ || argc < 1) return JS_UNDEFINED;
  const QString key = jsToString(ctx, argv[0]);
  const json result = getPath(plugin->config_->appConfig(), key);
  return jsonToJs(ctx, result);
}

JSValue PluginSandbox::jsConfigSet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  auto* plugin = fromContext(ctx);
  if (!plugin || !plugin->config_ || argc < 2) {
    return JS_ThrowTypeError(ctx, "sparkle.config.set(key, value) requires two arguments");
  }
  const QString key = jsToString(ctx, argv[0]);
  const json replacement = jsToJson(ctx, argv[1]);
  if (key.isEmpty() || replacement.is_discarded()) {
    return JS_ThrowTypeError(ctx, "invalid config value");
  }
  json config = plugin->config_->appConfig();
  setPath(config, key, replacement);
  plugin->config_->patchAppConfig(config);
  return JS_UNDEFINED;
}

JSValue PluginSandbox::jsHttpGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
  if (argc < 1) return JS_ThrowTypeError(ctx, "sparkle.http.get(url) requires a URL");
  const QUrl url(jsToString(ctx, argv[0]));
  if (!url.isValid() || (url.scheme() != QLatin1String("http") &&
                         url.scheme() != QLatin1String("https"))) {
    return JS_ThrowTypeError(ctx, "only http and https URLs are supported");
  }

  QNetworkAccessManager manager;
  QNetworkReply* reply = manager.get(QNetworkRequest(url));
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
  timer.start(10000);
  loop.exec();

  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const QByteArray body = reply->readAll();
  const QString error = reply->errorString();
  reply->deleteLater();

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "ok", JS_NewBool(ctx, status >= 200 && status < 300));
  JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, status));
  JS_SetPropertyStr(ctx, result, "body", JS_NewStringLen(ctx, body.constData(), body.size()));
  if (status == 0) {
    const QByteArray message = error.toUtf8();
    JS_SetPropertyStr(ctx, result, "error",
                      JS_NewStringLen(ctx, message.constData(), message.size()));
  }
  return result;
}

JSValue PluginSandbox::jsShowNotification(JSContext* ctx, JSValueConst, int argc,
                                          JSValueConst* argv) {
  auto* plugin = fromContext(ctx);
  if (plugin && argc > 0) {
    const QString message = jsToString(ctx, argv[0]);
    if (plugin->notificationHandler_) {
      plugin->notificationHandler_(message);
    } else {
      // 无 GUI/测试环境仍保留可观测的日志降级路径。
      qInfo().noquote() << QStringLiteral("[Plugin notification:%1] %2")
                               .arg(plugin->id_, message);
    }
  }
  return JS_UNDEFINED;
}

bool PluginSandbox::installApi() {
  JSValue global = JS_GetGlobalObject(context_.get());
  JSValue sparkle = JS_NewObject(context_.get());
  JSValue config = JS_NewObject(context_.get());
  JSValue http = JS_NewObject(context_.get());
  JSValue ui = JS_NewObject(context_.get());

  JS_SetPropertyStr(context_.get(), sparkle, "log",
                    JS_NewCFunction(context_.get(), &PluginSandbox::jsLog, "log", 1));
  JS_SetPropertyStr(context_.get(), config, "get",
                    JS_NewCFunction(context_.get(), &PluginSandbox::jsConfigGet, "get", 1));
  JS_SetPropertyStr(context_.get(), config, "set",
                    JS_NewCFunction(context_.get(), &PluginSandbox::jsConfigSet, "set", 2));
  JS_SetPropertyStr(context_.get(), http, "get",
                    JS_NewCFunction(context_.get(), &PluginSandbox::jsHttpGet, "get", 1));
  JS_SetPropertyStr(context_.get(), ui, "showNotification",
                    JS_NewCFunction(context_.get(), &PluginSandbox::jsShowNotification,
                                    "showNotification", 1));
  JS_SetPropertyStr(context_.get(), sparkle, "config", config);
  JS_SetPropertyStr(context_.get(), sparkle, "http", http);
  JS_SetPropertyStr(context_.get(), sparkle, "ui", ui);
  const int result = JS_SetPropertyStr(context_.get(), global, "sparkle", sparkle);
  JS_FreeValue(context_.get(), global);
  return result >= 0;
}

QString PluginSandbox::exceptionText() {
  JSValue exception = JS_GetException(context_.get());
  QString message = jsToString(context_.get(), exception);
  JS_FreeValue(context_.get(), exception);
  return message.isEmpty() ? QStringLiteral("unknown JavaScript error") : message;
}

bool PluginSandbox::callFunction(JSValue function) {
  const qint64 previousDeadline = deadlineMs_;
  deadlineMs_ = QDateTime::currentMSecsSinceEpoch() + 5000;
  JSValue result = JS_Call(context_.get(), function, JS_UNDEFINED, 0, nullptr);
  deadlineMs_ = previousDeadline;
  if (JS_IsException(result)) {
    setError(exceptionText());
    JS_FreeValue(context_.get(), result);
    return false;
  }
  JS_FreeValue(context_.get(), result);
  return true;
}

bool PluginSandbox::callLifecycle(const char* functionName) {
  if (!context_) return true;
  JSValue function = JS_GetPropertyStr(context_.get(), module_, functionName);
  if (JS_IsException(function)) {
    setError(exceptionText());
    JS_FreeValue(context_.get(), function);
    return false;
  }
  if (!JS_IsFunction(context_.get(), function)) {
    JS_FreeValue(context_.get(), function);
    return true;
  }
  const bool ok = callFunction(function);
  JS_FreeValue(context_.get(), function);
  return ok;
}

bool PluginSandbox::load() {
  unload();
  error_.clear();

  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    setError(QStringLiteral("cannot read plugin file"));
    return false;
  }

  runtime_.reset(JS_NewRuntime());
  if (!runtime_) {
    setError(QStringLiteral("cannot create QuickJS runtime"));
    return false;
  }
  JS_SetMemoryLimit(runtime_.get(), 32ull * 1024 * 1024);
  JS_SetMaxStackSize(runtime_.get(), 256 * 1024);
  JS_SetInterruptHandler(runtime_.get(), interruptHandler, &deadlineMs_);
  context_.reset(JS_NewContext(runtime_.get()));
  if (!context_) {
    setError(QStringLiteral("cannot create QuickJS context"));
    runtime_.reset();
    return false;
  }
  JS_SetContextOpaque(context_.get(), this);
  if (!installApi()) {
    setError(QStringLiteral("cannot install plugin API"));
    unload();
    return false;
  }

  // CommonJS 导出对象作为生命周期容器；不提供 require/fs 等能力。
  deadlineMs_ = QDateTime::currentMSecsSinceEpoch() + 5000;
  const QByteArray source = file.readAll();
  if (source.size() > 8 * 1024 * 1024) {
    setError(QStringLiteral("plugin source exceeds 8 MiB limit"));
    unload();
    return false;
  }
  const QByteArray wrapped = QByteArrayLiteral(
      "var module = { exports: {} }; var exports = module.exports;\n") + source +
                             QByteArrayLiteral("\n;module.exports;");
  JSValue exported = JS_Eval(context_.get(), wrapped.constData(), wrapped.size(),
                             path_.toUtf8().constData(), JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(exported)) {
    setError(exceptionText());
    JS_FreeValue(context_.get(), exported);
    unload();
    return false;
  }
  if (!JS_IsObject(exported)) {
    setError(QStringLiteral("plugin must export an object"));
    JS_FreeValue(context_.get(), exported);
    unload();
    return false;
  }
  module_ = JS_DupValue(context_.get(), exported);
  JS_FreeValue(context_.get(), exported);

  JSValue exportedId = JS_GetPropertyStr(context_.get(), module_, "id");
  if (!JS_IsException(exportedId) && JS_IsString(exportedId)) id_ = jsToString(context_.get(), exportedId);
  JS_FreeValue(context_.get(), exportedId);
  loaded_ = callLifecycle("onLoad");
  if (!loaded_) unload();
  return loaded_;
}

void PluginSandbox::unload() {
  if (!context_) {
    runtime_.reset();
    return;
  }
  if (loaded_ && JS_IsObject(module_)) callLifecycle("onUnload");
  if (!JS_IsUndefined(module_)) JS_FreeValue(context_.get(), module_);
  module_ = JS_UNDEFINED;
  loaded_ = false;
  context_.reset();
  runtime_.reset();
}

void PluginSandbox::onProxyStart() { callLifecycle("onProxyStart"); }
void PluginSandbox::onProxyStop() { callLifecycle("onProxyStop"); }

}  // namespace sparkle::core
