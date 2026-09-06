#pragma once

#include <QString>
#include <QtGlobal>
#include <functional>
#include <memory>
#include <string>

#include "IPlugin.h"

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
class ConfigManager;
class LogManager;

// 每个插件拥有独立 Runtime/Context。沙箱只注入 Sparkle API，不提供文件系统 API。
class PluginSandbox final : public IPlugin {
public:
  using NotificationHandler = std::function<void(const QString& message)>;

  PluginSandbox(QString pluginPath, LogManager* log, ConfigManager* config,
                NotificationHandler notificationHandler = {});
  ~PluginSandbox();

  bool load() override;
  void unload() override;
  bool isLoaded() const;
  void onProxyStart() override;
  void onProxyStop() override;
  QString id() const override;
  QString filePath() const override;
  QString lastError() const;
  void setNotificationHandler(NotificationHandler handler);

private:
  struct RuntimeDeleter {
    void operator()(JSRuntime* runtime) const noexcept;
  };
  struct ContextDeleter {
    void operator()(JSContext* context) const noexcept;
  };
  using RuntimePtr = std::unique_ptr<JSRuntime, RuntimeDeleter>;
  using ContextPtr = std::unique_ptr<JSContext, ContextDeleter>;

  static PluginSandbox* fromContext(JSContext* ctx);
  static JSValue jsLog(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsConfigGet(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsConfigSet(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsHttpGet(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
  static JSValue jsShowNotification(JSContext* ctx, JSValueConst thisVal, int argc,
                                    JSValueConst* argv);

  bool installApi();
  bool callLifecycle(const char* functionName);
  bool callFunction(JSValue function);
  QString exceptionText();
  void setError(const QString& message);

  QString path_;
  QString id_;
  QString error_;
  LogManager* log_ = nullptr;
  ConfigManager* config_ = nullptr;
  NotificationHandler notificationHandler_;
  RuntimePtr runtime_;
  ContextPtr context_;
  JSValue module_ = JS_UNDEFINED;
  qint64 deadlineMs_ = 0;
  bool loaded_ = false;
};

}  // namespace sparkle::core
