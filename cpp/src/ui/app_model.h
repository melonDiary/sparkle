#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QTimer>
#include <map>
#include <vector>

#include "models.h"

#include <map>

#include <nlohmann/json.hpp>

namespace sparkle::core {
class CoreManager;
class MihomoApiClient;
class LogManager;
class SystemProxyManager;
class ConfigManager;
class SubscriptionManager;
}

namespace sparkle::ui {

// QML 数据模型：将核心状态和代理数据转换成 Q_PROPERTY/QVariant，避免 QML 依赖
// C++ 业务对象的内部实现。实际实例由 main.cpp 注入 QML 上下文。
class AppModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString coreState READ coreState NOTIFY coreStateChanged)
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(bool systemProxyEnabled READ systemProxyEnabled NOTIFY systemProxyEnabledChanged)
  Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY groupNamesChanged)
  Q_PROPERTY(QString selectedGroup READ selectedGroup WRITE setSelectedGroup NOTIFY selectedGroupChanged)
  Q_PROPERTY(QVariantList proxies READ proxies NOTIFY proxiesChanged)
  Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
  Q_PROPERTY(QVariantList logs READ logs NOTIFY logsChanged)
  Q_PROPERTY(QVariantList connections READ connections NOTIFY connectionsChanged)
  Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
  Q_PROPERTY(QString currentProfileId READ currentProfileId NOTIFY profilesChanged)
  Q_PROPERTY(QVariantMap traffic READ traffic NOTIFY trafficChanged)
  Q_PROPERTY(QVariantMap memory READ memory NOTIFY memoryChanged)
  Q_PROPERTY(int connectionCount READ connectionCount NOTIFY connectionCountChanged)
  Q_PROPERTY(bool mitmEnabled READ mitmEnabled NOTIFY mitmEnabledChanged)
  Q_PROPERTY(bool autostartEnabled READ autostartEnabled NOTIFY autostartEnabledChanged)
  Q_PROPERTY(QString outboundMode READ outboundMode WRITE setOutboundMode NOTIFY outboundModeChanged)
  Q_PROPERTY(int siderWidth READ siderWidth WRITE setSiderWidth NOTIFY siderWidthChanged)
  // 受控 mihomo 配置中的 tun/dns/sniffer 对象（TUN/DNS/嗅探设置表单的数据源）。
  Q_PROPERTY(QVariantMap tunConfig READ tunConfig NOTIFY controlledConfigChanged)
  Q_PROPERTY(QVariantMap dnsConfig READ dnsConfig NOTIFY controlledConfigChanged)
  Q_PROPERTY(QVariantMap snifferConfig READ snifferConfig NOTIFY controlledConfigChanged)
  // 侧栏卡片顺序（appConfig.siderOrder，拖拽重排后持久化，对齐原版 siderOrder）。
  Q_PROPERTY(QStringList siderOrder READ siderOrder WRITE setSiderOrder NOTIFY siderOrderChanged)
  Q_PROPERTY(QString controllerVersion READ controllerVersion NOTIFY controllerVersionChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
public:
  explicit AppModel(QObject* parent = nullptr);
  ~AppModel() override;

  void setCoreManager(sparkle::core::CoreManager* core);
  void setApiClient(sparkle::core::MihomoApiClient* api);
  void setLogManager(sparkle::core::LogManager* log);
  void setSystemProxyManager(sparkle::core::SystemProxyManager* manager);
  void setConfigManager(sparkle::core::ConfigManager* config);
  void setSubscriptionManager(sparkle::core::SubscriptionManager* subscription);

  QString coreState() const;
  bool running() const;
  bool systemProxyEnabled() const;
  QStringList groupNames() const;
  QString selectedGroup() const;
  QVariantList proxies() const;
  QVariantList rules() const;
  QVariantList logs() const;
  QVariantList connections() const;
  QVariantList profiles() const;
  QString currentProfileId() const;
  QVariantMap traffic() const;
  QVariantMap memory() const;
  int connectionCount() const;
  bool mitmEnabled() const;
  bool autostartEnabled() const;
  QString outboundMode() const;
  void setOutboundMode(const QString& mode);
  int siderWidth() const;
  void setSiderWidth(int width);
  QVariantMap tunConfig() const;
  QVariantMap dnsConfig() const;
  QVariantMap snifferConfig() const;
  QStringList siderOrder() const;
  void setSiderOrder(const QStringList& order);
  QString controllerVersion() const;
  QString statusMessage() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void setRuleDisabled(qulonglong index, bool disabled);
  Q_INVOKABLE void setStatusMessage(const QString& message);
  Q_INVOKABLE void setSelectedGroup(const QString& group);
  Q_INVOKABLE void setSystemProxyEnabled(bool enabled);
  // 把选中分组的当前节点切换为 name；在"全部"视图下自动定位到包含该节点的第一个分组。
  Q_INVOKABLE void activateNode(const QString& name);
  // 触发一次节点延迟测试，结果回写 proxies 的 delay 字段。
  Q_INVOKABLE void testNodeDelay(const QString& name);
  // 开关 MITM（写入配置，AppController 会据此启动/停止 MITMManager）。
  Q_INVOKABLE void setMitmEnabled(bool enabled);
  // 开机自启（调用平台级登录项注册）。
  Q_INVOKABLE void setAutostartEnabled(bool enabled);
  // ---- 连接页 ----
  // 关闭单条连接 / 全部连接（走 controller REST，连接流自动反映变化）。
  Q_INVOKABLE void closeConnection(const QString& id);
  Q_INVOKABLE void closeAllConnections();
  // ---- 订阅页 ----
  // 导入订阅：拉取解析 -> 正文落盘 + profile.yaml 增条目。
  Q_INVOKABLE void importProfile(const QString& url, const QString& name);
  // 更新远程订阅（重新拉取并覆盖正文）。
  Q_INVOKABLE void updateProfile(const QString& id);
  // 切换当前订阅（触发运行配置重新生成 + 内核重启）。
  Q_INVOKABLE void setCurrentProfile(const QString& id);
  // 删除订阅（正文文件 + profile.yaml 条目；删当前订阅时先切到剩余首个）。
  Q_INVOKABLE void deleteProfile(const QString& id);
  // 订阅自动更新（对齐原版 profileUpdater）：按 interval（分钟）为远程订阅设定时器。
  Q_INVOKABLE void rescheduleProfileUpdaters();
  // 开关单个订阅的自动更新（appConfig 自动更新总开关语义见 ProfileItem.autoUpdate）。
  Q_INVOKABLE void setProfileAutoUpdate(const QString& id, bool enabled);
  // 切换出站模式（rule/global/direct）：写入受控配置并触发内核重载，对齐原
  // OutboundModeSwitcher（patchControledMihomoConfig + patchMihomoConfig）。
  Q_INVOKABLE void requestOutboundMode(const QString& mode);
  // 通用受控配置补丁（TUN/DNS/嗅探表单保存入口）：QVariantMap 深合并进受控配置，
  // 持久化 + reloadRequested → 内核重启（与原版 patchControledMihomoConfig + restartCore 一致）。
  Q_INVOKABLE void patchControlledConfig(const QVariantMap& patch);
  // 持久化侧栏宽度（拖拽松手时调用，边界吸附逻辑在 QML 侧）。

signals:
  void coreStateChanged();
  void runningChanged();
  void systemProxyEnabledChanged();
  void groupNamesChanged();
  void selectedGroupChanged();
  void proxiesChanged();
  void rulesChanged();
  void logsChanged();
  void connectionsChanged();
  void profilesChanged();
  void trafficChanged();
  void memoryChanged();
  void connectionCountChanged();
  void mitmEnabledChanged();
  void autostartEnabledChanged();
  void outboundModeChanged();
  void siderWidthChanged();
  void controlledConfigChanged();
  void siderOrderChanged();
  void controllerVersionChanged();
  void statusMessageChanged();
  void errorMessage(const QString& message);

private:
  void setCoreState(sparkle::core::CoreState state);
  void setGroups(const std::vector<sparkle::core::ProxyGroup>& groups);
  void setNodes(const std::vector<sparkle::core::ProxyNode>& nodes);
  void updateVisibleNodes();
  void setRules(const std::vector<sparkle::core::RuleItem>& rules);
  void appendLog(const sparkle::core::LogEntry& entry);
  void setConnections(const std::vector<sparkle::core::ConnectionItem>& items);
  void emitProfilesChanged();
  void saveProfileConfig(const nlohmann::json& config);
  void regenerateRuntimeConfig();
  void scheduleProfileUpdater(const QString& id, qint64 delayMs);   // 重置单条定时器
  void clearProfileUpdater(const QString& id);                      // 注销单条定时器
  void updateProfileSilently(const QString& id);                    // 后台更新（成功仅写日志）
  void setControllerVersion(const sparkle::core::ControllerVersion& version);
  void connectCoreSignals();
  void refreshControlledConfigs();   // 受控配置变更后重读 tun/dns/sniffer + outboundMode

  sparkle::core::CoreManager* core_ = nullptr;
  sparkle::core::MihomoApiClient* api_ = nullptr;
  sparkle::core::LogManager* log_ = nullptr;
  sparkle::core::SystemProxyManager* systemProxy_ = nullptr;
  sparkle::core::ConfigManager* config_ = nullptr;
  QString coreState_ = QStringLiteral("stopped");
  bool running_ = false;
  bool systemProxyEnabled_ = false;
  QStringList groupNames_;
  QString selectedGroup_ = QStringLiteral("全部");
  std::vector<sparkle::core::ProxyGroup> groups_;
  std::vector<sparkle::core::ProxyNode> nodes_;
  QVariantList proxies_;
  QVariantList rules_;
  QVariantList logs_;
  QVariantList profiles_;
  QString currentProfileId_;
  // 连接快照：id → item，保持首见顺序（WS 200ms 节流全量推送，diff 后再 emit）。
  std::map<QString, sparkle::core::ConnectionItem> connectionsById_;
  std::vector<QString> connectionsOrder_;
  QVariantList connections_;
  bool connectionsDirty_ = false;
  QTimer connectionsFlushTimer_;   // 连接 UI 刷新节流（300ms 合并）
  bool importingProfile_ = false;  // 订阅拉取进行中（防并发导入）
  // 订阅自动更新定时器池（id → 单发 QTimer；对齐原版 intervalPool）。
  std::map<QString, QTimer*> profileUpdateTimers_;
  QTimer logFlushTimer_;      // 日志 UI 刷新节流（50ms 合并）
  bool logDirty_ = false;
  QVariantMap traffic_;
  QVariantMap memory_;
  int connectionCount_ = 0;
  bool mitmEnabled_ = false;
  bool autostartEnabled_ = false;
  QString outboundMode_ = QStringLiteral("rule");
  int siderWidth_ = 250;
  QVariantMap tunConfig_;
  QVariantMap dnsConfig_;
  QVariantMap snifferConfig_;
  QStringList siderOrder_;
  QString controllerVersion_;
  QString statusMessage_;
  std::vector<QMetaObject::Connection> coreConnections_;   // core/log 信号（setCoreManager/setLogManager 共用）
  std::vector<QMetaObject::Connection> apiConnections_;
  std::vector<QMetaObject::Connection> proxyConnections_;
  std::vector<QMetaObject::Connection> configConnections_;
  std::vector<QMetaObject::Connection> subscriptionConnections_;
  sparkle::core::SubscriptionManager* subscription_ = nullptr;
};

}  // namespace sparkle::ui
