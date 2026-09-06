#pragma once

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

namespace sparkle::core {

class ConfigManager;

// 运行时配置生成（对应原 core/factory.ts::generateProfile）。
class RuntimeConfigFactory final : public QObject {
  Q_OBJECT
public:
  explicit RuntimeConfigFactory(ConfigManager* config, QObject* parent = nullptr);

  void generate();                     // profile + override + controlled → work/config.yaml
  QString generatedPath() const;       // 最近一次 generate() 写入的文件路径

  nlohmann::json runtimeConfig() const;
  QString runtimeConfigStr() const;    // 序列化后内容

signals:
  void generated();

private:
  ConfigManager* config_;
  nlohmann::json runtime_;
  QString generatedPath_;
};

}  // namespace sparkle::core