#pragma once

#include <QObject>
#include <QString>
#include <string>

#include <nlohmann/json.hpp>
#include <memory>
#include <vector>

#include "models.h"
#include "yaml_store.h"

namespace sparkle::core {

// service 模式控制器接入点：host/port = mihomo 原生 TCP external-controller；
// secret = 该控制器的访问凭据（HTTP/WS 均需 `Authorization: Bearer <secret>`）。
struct ServiceControllerEndpoint {
  QString host = QStringLiteral("127.0.0.1");
  quint16 port = 0;                     // 0 = 未配置
  QString secret;                       // 空 = 未生成
};

// 配置管理（对应原 config/*.ts）：4 个 YAML 存储 + 节点解析 + 变更信号。
// 骨架阶段配置以 nlohmann::json 承载（完整类型化 AppConfig 结构体后继补齐）。
class ConfigManager final : public QObject {
  Q_OBJECT
public:
  explicit ConfigManager(QObject* parent = nullptr);
  ~ConfigManager() override;

  // ---- service 模式（detached + TCP 控制器 + Bearer secret）----
  bool serviceMode();                          // appConfig.corePermissionMode == "service"
  ServiceControllerEndpoint serviceControllerEndpoint();   // 缺失/无效时自动生成并持久化

  // ---- MITM 配置（存储在 config.yaml 的 mitm_* 键）----
  bool mitmEnabled() const;
  quint16 mitmPort() const;
  QString mitmScriptDir() const;

  // ---- App 配置（config.yaml）----
  nlohmann::json appConfig(bool force = false);
  void patchAppConfig(const nlohmann::json& patch);

  // ---- 受控 Mihomo 配置（mihomo.yaml）----
  nlohmann::json controlledMihomoConfig(bool force = false);
  void patchControlledMihomoConfig(const nlohmann::json& patch);
  // 整体替换受控配置并落盘（不触发 reloadRequested；供运行时配置回写同步 tun/dns/sniffer 用）。
  void replaceControlledMihomoConfig(const nlohmann::json& config);

  // ---- 订阅/配置（profile.yaml）----
  nlohmann::json profileConfig(bool force = false);
  void setProfileConfig(const nlohmann::json& config);

  // ---- 覆写（override.yaml）----
  nlohmann::json overrideConfig(bool force = false);
  void setOverrideConfig(const nlohmann::json& config);

  // ---- 订阅正文（原文串保真）----
  QString profileRawText(const QString& id);
  void setProfileRawText(const QString& id, const QString& content);

  // ---- 配置文件加载（支持 YAML 与 QuickJS config.js）----
  bool loadConfig(const std::string& path);
  std::vector<ProxyNode> getProxyNodes() const;

  // ---- 节点/组/规则解析（从当前订阅的 YAML 提取）----
  std::vector<ProxyNode> parseProxyNodes();
  std::vector<ProxyGroup> parseProxyGroups();
  std::vector<RuleItem> parseRules();

  // ---- 系统代理配置快捷读写（落在 appConfig.sysProxy 内）----
  SysProxyConfig sysProxyConfig();
  void setSysProxyConfig(const SysProxyConfig& config);

signals:
  void appConfigChanged();
  void controlledMihomoConfigChanged();
  void profileConfigChanged();
  void overrideConfigChanged();
  void reloadRequested();            // 订阅/受控配置变更 → 重新生成运行配置 + 重启内核
  void profileWriteFailed(const QString& id);  // 订阅正文落盘失败（重试后仍失败）
  void configChanged();                // loadConfig 成功后通知上层重载
  void mitmConfigChanged();

private:
  struct Impl;
  std::unique_ptr<Impl> d_;
  nlohmann::json loadedConfig_;
  QString loadedConfigPath_;
};

}  // namespace sparkle::core