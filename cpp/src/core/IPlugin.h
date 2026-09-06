#pragma once

#include <QString>

namespace sparkle::core {

// 所有 Sparkle 插件必须实现的最小生命周期接口。
class IPlugin {
public:
  virtual ~IPlugin() = default;

  virtual QString id() const = 0;
  virtual QString filePath() const = 0;
  virtual bool load() = 0;
  virtual void unload() = 0;
  virtual void onProxyStart() = 0;
  virtual void onProxyStop() = 0;
};

}  // namespace sparkle::core
