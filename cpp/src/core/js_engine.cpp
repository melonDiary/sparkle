#include "js_engine.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <string>

// QuickJS 头文件使用 C99 复合字面量（JS_UNDEFINED 等宏）且内联函数含未用参数，
// 在 C++ -Wextra 下触发 -Wc99-extensions / -Wunused-parameter，此处第三方头文件告警统一静默。
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc99-extensions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
extern "C" {
#include "quickjs.h"
}

#include "json_yaml.h"

namespace sparkle::core {
namespace {

// ============ console 输出缓冲（单线程、线程局部） ============
thread_local QByteArray g_consoleLog;

QString jsValueToQString(JSContext* ctx, JSValueConst v);

// ============ 中断（超时）保护 ============
struct InterruptState {
  qint64 deadlineMs = 0;
};

int jsInterruptHandler(JSRuntime*, void* opaque) {
  const auto* state = static_cast<const InterruptState*>(opaque);
  return QDateTime::currentMSecsSinceEpoch() > state->deadlineMs ? 1 : 0;
}

// ============ Promise 兑现/拒绝捕获 ============
thread_local bool g_settled = false;
thread_local bool g_rejected = false;
thread_local JSValue g_captured = JS_UNDEFINED;   // JS_DupValue 持有；调用方负责释放

JSValue cbFulfilled(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  g_settled = true;
  g_rejected = false;
  if (!JS_IsUndefined(g_captured)) JS_FreeValue(ctx, g_captured);
  g_captured = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
  return JS_UNDEFINED;
}

JSValue cbRejected(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  g_settled = true;
  g_rejected = true;
  if (!JS_IsUndefined(g_captured)) JS_FreeValue(ctx, g_captured);
  g_captured = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
  return JS_UNDEFINED;
}

// 将 JS 值转成 QString（供 console / 错误消息；对象走 JSON 序列化）。
QString jsValueToQString(JSContext* ctx, JSValueConst v) {
  if (JS_IsString(v)) {
    const char* p = JS_ToCString(ctx, v);
    if (p) {
      const QString s = QString::fromUtf8(p);
      JS_FreeCString(ctx, p);
      return s;
    }
    return QString();
  }
  if (JS_IsNull(v)) return QStringLiteral("null");
  if (JS_IsUndefined(v)) return QStringLiteral("undefined");
  if (JS_IsBool(v)) return JS_ToBool(ctx, v) ? QStringLiteral("true") : QStringLiteral("false");
  if (JS_IsNumber(v)) {
    double d = 0;
    JS_ToFloat64(ctx, &d, v);
    return QString::number(d);
  }
  if (JS_IsObject(v)) {
    JSValue j = JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(j)) {
      const char* p = JS_ToCString(ctx, j);
      if (p) {
        const QString s = QString::fromUtf8(p);
        JS_FreeCString(ctx, p);
        JS_FreeValue(ctx, j);
        return s;
      }
    }
    JS_FreeValue(ctx, j);
    return QStringLiteral("[object]");
  }
  if (JS_IsException(v)) {
    JSValue ex = JS_GetException(ctx);
    const QString s = jsValueToQString(ctx, ex);
    JS_FreeValue(ctx, ex);
    return s;
  }
  return QString();
}

// 取最近异常的文本描述（Error 对象取 message + stack，原始值回退到值本身）。
QString takeExceptionString(JSContext* ctx) {
  JSValue ex = JS_GetException(ctx);
  QString s;
  if (JS_IsObject(ex)) {
    JSValue msg = JS_GetPropertyStr(ctx, ex, "message");
    if (!JS_IsUndefined(msg) && !JS_IsException(msg)) {
      s = jsValueToQString(ctx, msg);
    }
    JS_FreeValue(ctx, msg);
    JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsException(stack)) {
      const QString st = jsValueToQString(ctx, stack);
      if (!st.isEmpty()) s += QStringLiteral("\n") + st;
    }
    JS_FreeValue(ctx, stack);
  }
  if (s.isEmpty() || s == QStringLiteral("undefined")) {
    s = jsValueToQString(ctx, ex);
  }
  JS_FreeValue(ctx, ex);
  return s.isEmpty() ? QStringLiteral("未知错误") : s;
}

// ============ 内置全局：b64e / b64d ============
JSValue jsB64e(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  if (argc < 1) return JS_ThrowTypeError(ctx, "b64e 需要 1 个参数");
  const QByteArray in = jsValueToQString(ctx, argv[0]).toUtf8();
  const QByteArray out = in.toBase64();
  return JS_NewStringLen(ctx, out.constData(), out.size());
}

JSValue jsB64d(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  if (argc < 1) return JS_ThrowTypeError(ctx, "b64d 需要 1 个参数");
  const QByteArray out = QByteArray::fromBase64(jsValueToQString(ctx, argv[0]).toUtf8());
  return JS_NewStringLen(ctx, out.constData(), out.size());
}

// ============ 内置全局：yaml.parse / yaml.stringify ============
JSValue jsYamlParse(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  if (argc < 1) return JS_ThrowTypeError(ctx, "yaml.parse 需要 1 个参数");
  const std::string yaml = jsValueToQString(ctx, argv[0]).toStdString();
  const nlohmann::json j = parseYamlStr(yaml);
  const std::string jsonStr = j.dump();
  return JS_ParseJSON(ctx, jsonStr.c_str(), jsonStr.size(), "<yaml.parse>");
}

JSValue jsYamlStringify(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  if (argc < 1) return JS_ThrowTypeError(ctx, "yaml.stringify 需要 1 个参数");
  JSValue jsonVal = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);
  if (JS_IsException(jsonVal)) return jsonVal;
  nlohmann::json j = nlohmann::json::object();
  const char* p = JS_ToCString(ctx, jsonVal);
  if (p) {
    try {
      j = nlohmann::json::parse(p);
    } catch (...) {
      j = nlohmann::json::object();
    }
    JS_FreeCString(ctx, p);
  }
  JS_FreeValue(ctx, jsonVal);
  const std::string yaml = toYamlString(j);
  return JS_NewStringLen(ctx, yaml.c_str(), yaml.size());
}

// ============ 内置全局：console ============
JSValue jsConsoleLog(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv, int magic) {
  Q_UNUSED(thisVal);
  static const char* const kLevels[] = {"log", "info", "debug", "error"};
  const char* level = (magic >= 0 && magic < 4) ? kLevels[magic] : "log";
  QByteArray line = "[";
  line += level;
  line += "] ";
  for (int i = 0; i < argc; ++i) {
    if (i) line += ' ';
    line += jsValueToQString(ctx, argv[i]).toUtf8();
  }
  g_consoleLog += line;
  g_consoleLog += '\n';
  return JS_UNDEFINED;
}

// ============ 内置全局：fetch（真网络） ============

// 同步 HTTP 请求结果（阻塞当前线程）。
struct SyncHttpResult {
  bool ok = false;
  int status = 0;
  QString statusText;
  QString error;
  QByteArray body;
  QHash<QString, QString> headers;   // 小写头名 → 值
};

// 同步执行一次 HTTP(S) 请求：QNetworkAccessManager + 嵌套 QEventLoop 等待。
// 说明：覆写脚本在 generate() 期间同步执行，此处用同步语义匹配这一时序；
//       若未来把 generate() 移到工作线程，可改为真正异步回调后再 settle Promise。
SyncHttpResult syncHttpRequest(const QString& url, const QString& method, const QByteArray& body,
                               const QHash<QString, QString>& headers, int timeoutMs = 30000) {
  SyncHttpResult out;
  const QUrl u(url);
  if (!u.isValid() || (u.scheme() != QLatin1String("http") &&
                       u.scheme() != QLatin1String("https"))) {
    out.error = QStringLiteral("仅支持 http/https: ") + url;
    return out;
  }

  QNetworkAccessManager nam;
  QNetworkRequest req(u);
  req.setRawHeader("Accept", "*/*");
  for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
    if (!it.key().isEmpty()) req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
  }

  QNetworkReply* reply = nam.sendCustomRequest(req, method.toUtf8().toUpper(), body);

  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
  timer.start(timeoutMs);
  loop.exec();
  timer.stop();

  out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  out.statusText = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
  // 与 Web fetch 语义一致：收到 HTTP 状态码即为合法响应（4xx/5xx 仍返回 Response）；
  // 仅 status=0（无 HTTP 响应 = DNS/连接/超时等网络故障）才设 error。
  if (out.status > 0) {
    out.ok = true;
  } else {
    out.error = reply->errorString();
  }
  const auto pairs = reply->rawHeaderPairs();
  for (const auto& p : pairs) {
    out.headers.insert(QString::fromLatin1(p.first.toLower()), QString::fromLatin1(p.second));
  }
  out.body = reply->readAll();
  reply->deleteLater();
  return out;
}

// __nativeFetch(url, method, headersJson, bodyText) → {status,statusText,body,headers,error?}
// request 报酬：仅暴露纯数据，Response 语义（.json()/.text()/ok）由下方 JS 垫片补全。
JSValue jsNativeFetch(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv) {
  Q_UNUSED(thisVal);
  const QString url = argc > 0 ? jsValueToQString(ctx, argv[0]) : QString();
  const QString method = (argc > 1 ? jsValueToQString(ctx, argv[1]) : QStringLiteral("GET")).toUpper();
  QHash<QString, QString> headers;
  if (argc > 2) {
    const std::string h = jsValueToQString(ctx, argv[2]).toStdString();
    try {
      const nlohmann::json j = nlohmann::json::parse(h);
      if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
          if (it.value().is_string()) {
            headers.insert(QString::fromStdString(it.key()).toLower(),
                           QString::fromStdString(it.value().get<std::string>()));
          }
        }
      }
    } catch (...) {
      // header 解析失败按空处理
    }
  }
  const QByteArray body = argc > 3 ? jsValueToQString(ctx, argv[3]).toUtf8() : QByteArray();

  const SyncHttpResult r = syncHttpRequest(url, method, body, headers);

  JSValue result = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, result, "status", JS_NewInt32(ctx, r.status));
  JS_SetPropertyStr(ctx, result, "statusText",
                    JS_NewStringLen(ctx, r.statusText.toUtf8().constData(), r.statusText.toUtf8().size()));
  JS_SetPropertyStr(ctx, result, "body", JS_NewStringLen(ctx, r.body.constData(), r.body.size()));
  JSValue headersObj = JS_NewObject(ctx);
  for (auto it = r.headers.cbegin(); it != r.headers.cend(); ++it) {
    const QByteArray v = it.value().toUtf8();
    JS_SetPropertyStr(ctx, headersObj, it.key().toUtf8().constData(), JS_NewStringLen(ctx, v.constData(), v.size()));
  }
  JS_SetPropertyStr(ctx, result, "headers", headersObj);
  if (!r.ok) {
    const QByteArray e = r.error.toUtf8();
    JS_SetPropertyStr(ctx, result, "error", JS_NewStringLen(ctx, e.constData(), e.size()));
  }
  return result;
}

// 注册全局（b64e/b64d/console/yaml/fetch），并注入 Buffer / fetch 兼容垫片。
bool defineGlobals(JSContext* ctx) {
  JSValue global = JS_GetGlobalObject(ctx);

  JS_SetPropertyStr(ctx, global, "b64e", JS_NewCFunction(ctx, jsB64e, "b64e", 1));
  JS_SetPropertyStr(ctx, global, "b64d", JS_NewCFunction(ctx, jsB64d, "b64d", 1));
  JS_SetPropertyStr(ctx, global, "__nativeFetch", JS_NewCFunction(ctx, jsNativeFetch, "__nativeFetch", 4));

  JSValue yamlObj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, yamlObj, "parse", JS_NewCFunction(ctx, jsYamlParse, "parse", 1));
  JS_SetPropertyStr(ctx, yamlObj, "stringify", JS_NewCFunction(ctx, jsYamlStringify, "stringify", 1));
  JS_SetPropertyStr(ctx, global, "yaml", yamlObj);

  JSValue consoleObj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, consoleObj, "log",
                    JS_NewCFunctionMagic(ctx, jsConsoleLog, "log", 1, JS_CFUNC_generic_magic, 0));
  JS_SetPropertyStr(ctx, consoleObj, "info",
                    JS_NewCFunctionMagic(ctx, jsConsoleLog, "info", 1, JS_CFUNC_generic_magic, 1));
  JS_SetPropertyStr(ctx, consoleObj, "debug",
                    JS_NewCFunctionMagic(ctx, jsConsoleLog, "debug", 1, JS_CFUNC_generic_magic, 2));
  JS_SetPropertyStr(ctx, consoleObj, "error",
                    JS_NewCFunctionMagic(ctx, jsConsoleLog, "error", 1, JS_CFUNC_generic_magic, 3));
  JS_SetPropertyStr(ctx, global, "console", consoleObj);
  JS_FreeValue(ctx, global);

  // Buffer 垫片 + fetch（真网络，基于 __nativeFetch，返回与 Web fetch 兼容的 Response）。
  static const char kShim[] =
      "(function(){\n"
      "  var Buffer = {\n"
      "    from: function(data, enc) {\n"
      "      var s = (enc === 'base64' || enc === 'base64url') ? b64d(String(data)) : String(data);\n"
      "      return { toString: function(enc2) {\n"
      "        return (enc2 === 'base64' || enc2 === 'base64url') ? b64e(s) : s;\n"
      "      } };\n"
      "    },\n"
      "    isBuffer: function() { return false; }\n"
      "  };\n"
      "  var fetch = function(url, options) {\n"
      "    try {\n"
      "      options = options || {};\n"
      "      var method = String(options.method || 'GET').toUpperCase();\n"
      "      var headers = {};\n"
      "      if (options.headers) {\n"
      "        if (typeof options.headers.forEach === 'function') {\n"
      "          options.headers.forEach(function(v, k) { headers[String(k).toLowerCase()] = String(v); });\n"
      "        } else {\n"
      "          Object.keys(options.headers).forEach(function(k) { headers[String(k).toLowerCase()] = String(options.headers[k]); });\n"
      "        }\n"
      "      }\n"
      "      var bodyStr = '';\n"
      "      if (options.body !== undefined && options.body !== null) {\n"
      "        if (typeof options.body === 'string') { bodyStr = options.body; }\n"
      "        else { bodyStr = JSON.stringify(options.body); if (!headers['content-type']) headers['content-type'] = 'application/json'; }\n"
      "      }\n"
      "      var raw = __nativeFetch(String(url), method, JSON.stringify(headers), bodyStr);\n"
      "      if (raw.error) throw new Error(raw.error);\n"
      "      var status = raw.status || 0;\n"
      "      var h = raw.headers || {};\n"
      "      var body = raw.body || '';\n"
      "      var response = {\n"
      "        ok: status >= 200 && status < 300,\n"
      "        status: status,\n"
      "        statusText: raw.statusText || '',\n"
      "        url: String(url),\n"
      "        headers: {\n"
      "          get: function(n) { var v = h[String(n).toLowerCase()]; return v === undefined ? null : v; },\n"
      "          has: function(n) { return Object.prototype.hasOwnProperty.call(h, String(n).toLowerCase()); }\n"
      "        },\n"
      "        text: function() { return Promise.resolve(body); },\n"
      "        json: function() { return Promise.resolve().then(function() { return JSON.parse(body); }); }\n"
      "      };\n"
      "      return Promise.resolve(response);\n"
      "    } catch(e) {\n"
      "      return Promise.reject(e);\n"
      "    }\n"
      "  };\n"
      "  var g = (typeof globalThis !== 'undefined') ? globalThis : this;\n"
      "  g.Buffer = Buffer; g.fetch = fetch;\n"
      "})();\n";
  JSValue shimRes = JS_Eval(ctx, kShim, sizeof(kShim) - 1, "<sparkle-shim>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(shimRes)) {
    JS_FreeValue(ctx, shimRes);
    return false;
  }
  JS_FreeValue(ctx, shimRes);
  return true;
}

// 等待 thenable（Promise）兑现：挂接 .then 并排空任务队列，返回兑现值或抛出的异常。
JSValue awaitJsValue(JSContext* ctx, JSRuntime* rt, JSValue value) {
  while (true) {
    JSValue then = JS_GetPropertyStr(ctx, value, "then");
    if (JS_IsException(then)) {
      JS_FreeValue(ctx, value);
      return then;
    }
    if (!JS_IsFunction(ctx, then)) {
      JS_FreeValue(ctx, then);
      return value;   // 非 thenable：直接作为最终值
    }

    JSValue resolve = JS_NewCFunction(ctx, cbFulfilled, "then_ok", 1);
    JSValue reject = JS_NewCFunction(ctx, cbRejected, "then_err", 1);
    JSValueConst args[2] = {resolve, reject};
    g_settled = false;
    g_rejected = false;
    JSValue callRet = JS_Call(ctx, then, value, 2, args);
    JS_FreeValue(ctx, then);
    JS_FreeValue(ctx, resolve);
    JS_FreeValue(ctx, reject);
    if (JS_IsException(callRet)) {
      JS_FreeValue(ctx, value);
      return callRet;
    }
    JS_FreeValue(ctx, callRet);
    JS_FreeValue(ctx, value);

    // 排空任务队列，直到 then 回调被调度命中。
    // 同时做时间兜底：防止无限自旋（例如 resolved 回调未被调度时）。
    const qint64 loopDeadline = QDateTime::currentMSecsSinceEpoch() + 10000;  // 10s
    int guard = 0;
    while (!g_settled && guard++ < 1000000) {
      JSContext* jobCtx = ctx;
      if (JS_ExecutePendingJob(rt, &jobCtx) == -1) break;   // 无任务可执行
      if (QDateTime::currentMSecsSinceEpoch() > loopDeadline) break;
    }
    if (!g_settled) {
      return JS_ThrowInternalError(ctx, "覆写脚本的 Promise 未兑现（可能等待了不支持的异步操作）");
    }
    if (g_rejected) {
      JSValue reason = g_captured;
      g_captured = JS_UNDEFINED;
      return JS_Throw(ctx, reason);
    }
    value = g_captured;
    g_captured = JS_UNDEFINED;
  }
}

}  // namespace

JsOverrideResult runOverrideScript(const QString& script, const nlohmann::json& profile,
                                   const QString& logPath) {
  JsOverrideResult out;
  g_consoleLog.clear();

  const QString header = QStringLiteral("[info] 开始执行脚本\n");

  JSRuntime* rt = JS_NewRuntime();
  if (!rt) {
    out.error = QStringLiteral("无法创建 JS 运行时");
    return out;
  }
  JS_SetMemoryLimit(rt, 256ull * 1024 * 1024);   // 256 MB
  JS_SetMaxStackSize(rt, 512 * 1024);

  InterruptState interrupt;
  interrupt.deadlineMs = QDateTime::currentMSecsSinceEpoch() + 10000;   // 10s 超时
  JS_SetInterruptHandler(rt, jsInterruptHandler, &interrupt);

  JSContext* ctx = JS_NewContext(rt);
  if (!ctx) {
    JS_FreeRuntime(rt);
    out.error = QStringLiteral("无法创建 JS 上下文");
    return out;
  }

  auto finish = [&]() {
    // 写日志（覆盖写）：头部 + console 输出 + 结果尾注。
    QByteArray body = header.toUtf8() + g_consoleLog;
    if (out.ok) {
      body += "[info] 脚本执行成功\n";
    } else {
      body += "[exception] 脚本执行失败：";
      body += out.error.toUtf8();
      body += "\n";
    }
    out.log = QString::fromUtf8(g_consoleLog);
    if (!logPath.isEmpty()) {
      QDir().mkpath(QFileInfo(logPath).absolutePath());
      QFile f(logPath);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(body);
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
  };

  if (!defineGlobals(ctx)) {
    out.error = QStringLiteral("内置全局注入失败");
    finish();
    return out;
  }

  // 1) 执行脚本（定义 main）。
  const QByteArray scriptUtf8 = script.toUtf8();
  JSValue evalRes =
      JS_Eval(ctx, scriptUtf8.constData(), scriptUtf8.size(), "<override>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(evalRes)) {
    out.error = takeExceptionString(ctx);
    JS_FreeValue(ctx, evalRes);
    finish();
    return out;
  }
  JS_FreeValue(ctx, evalRes);

  // 2) 取 main（全局词法绑定，函数声明/var 均覆盖）。
  JSValue mainVal = JS_Eval(ctx, "main", 4, "<main>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(mainVal)) {
    out.error = takeExceptionString(ctx);
    JS_FreeValue(ctx, mainVal);
    finish();
    return out;
  }
  if (!JS_IsFunction(ctx, mainVal)) {
    JS_FreeValue(ctx, mainVal);
    out.error = QStringLiteral("脚本必须导出 main 函数");
    finish();
    return out;
  }

  // 3) profile → JSValue，调用 main(profile)。
  const std::string profileJson = profile.dump();
  JSValue profileVal = JS_ParseJSON(ctx, profileJson.c_str(), profileJson.size(), "<profile>");
  if (JS_IsException(profileVal)) {
    out.error = takeExceptionString(ctx);
    JS_FreeValue(ctx, mainVal);
    JS_FreeValue(ctx, profileVal);
    finish();
    return out;
  }
  JSValue result = JS_Call(ctx, mainVal, JS_UNDEFINED, 1, &profileVal);
  JS_FreeValue(ctx, profileVal);
  JS_FreeValue(ctx, mainVal);
  if (JS_IsException(result)) {
    out.error = takeExceptionString(ctx);
    JS_FreeValue(ctx, result);
    finish();
    return out;
  }

  // 4) 等待 Promise（若有）。
  result = awaitJsValue(ctx, rt, result);
  if (JS_IsException(result)) {
    out.error = takeExceptionString(ctx);
    JS_FreeValue(ctx, result);
    finish();
    return out;
  }

  // 5) 结果 → JSON，并校验是对象。
  JSValue jsonVal = JS_JSONStringify(ctx, result, JS_UNDEFINED, JS_UNDEFINED);
  JS_FreeValue(ctx, result);
  if (JS_IsException(jsonVal)) {
    out.error = takeExceptionString(ctx);
    JS_FreeValue(ctx, jsonVal);
    finish();
    return out;
  }
  const char* jsonStr = JS_ToCString(ctx, jsonVal);
  if (!jsonStr) {
    out.error = QStringLiteral("JSON 序列化失败");
    JS_FreeValue(ctx, jsonVal);
    finish();
    return out;
  }
  try {
    out.profile = nlohmann::json::parse(jsonStr);
    out.ok = true;
  } catch (...) {
    out.ok = false;
    out.error = QStringLiteral("main 返回值不是有效 JSON");
  }
  JS_FreeCString(ctx, jsonStr);
  JS_FreeValue(ctx, jsonVal);

  if (out.ok && !out.profile.is_object()) {
    out.ok = false;
    out.profile = nlohmann::json::object();
    out.error = QStringLiteral("脚本返回值必须是对象");
  }

  finish();
  return out;
}

}  // namespace sparkle::core