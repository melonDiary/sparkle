#pragma once

#include <QString>

#include <nlohmann/json.hpp>

namespace sparkle::core {

// JS 覆写脚本执行结果（对应原 factory.ts::runOverrideScript）。
struct JsOverrideResult {
  bool ok = false;
  QString error;              // 异常信息（ok=false 时）
  QString log;                // 脚本 console 输出
  nlohmann::json profile;     // main() 返回的新配置对象（ok=true 时）
};

// 用 QuickJS 执行 override 脚本的 main(profile)（对应原 vm.runInContext 的 async 包装：
// 支持 Promise 返回），返回新的配置对象。日志写入 logPath（覆盖写）。
JsOverrideResult runOverrideScript(const QString& script, const nlohmann::json& profile,
                                   const QString& logPath);

}  // namespace sparkle::core