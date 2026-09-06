#include "app_model.h"

#include <QVariantMap>

#include "core_manager.h"
#include "log_manager.h"
#include "mihomo_api_client.h"
#include "system_proxy_manager.h"

namespace sparkle::ui {

AppModel::AppModel(QObject* parent) : QObject(parent) {}

AppModel::~AppModel() {
  for (const auto& connection : connections_) QObject::disconnect(connection);
  for (const auto& connection : apiConnections_) QObject::disconnect(connection);
  for (const auto& connection : proxyConnections_) QObject::disconnect(connection);
}

void AppModel::setCoreManager(sparkle::core::CoreManager* core) {
  for (const auto& connection : connections_) QObject::disconnect(connection);
  connections_.clear();
  core_ = core;
  if (!core_) return;

  setCoreState(core_->state());
  connections_.push_back(connect(core_, &sparkle::core::CoreManager::stateChanged, this,
                                 &AppModel::setCoreState));
  connections_.push_back(connect(core_, &sparkle::core::CoreManager::coreStarted, this,
                                 &AppModel::refresh));
}

void AppModel::setApiClient(sparkle::core::MihomoApiClient* api) {
  if (api_ == api) return;
  for (const auto& connection : apiConnections_) QObject::disconnect(connection);
  apiConnections_.clear();
  api_ = api;
  if (!api_) return;
  apiConnections_.push_back(connect(api_, &sparkle::core::MihomoApiClient::trafficUpdated, this,
                                     [this](const sparkle::core::TrafficStats& stats) {
                                       traffic_.insert(QStringLiteral("upload"),
                                                       static_cast<qulonglong>(stats.upload));
                                       traffic_.insert(QStringLiteral("download"),
                                                       static_cast<qulonglong>(stats.download));
                                       emit trafficChanged();
                                     }));
}

void AppModel::setSystemProxyManager(sparkle::core::SystemProxyManager* manager) {
  if (systemProxy_ == manager) return;
  for (const auto& connection : proxyConnections_) QObject::disconnect(connection);
  proxyConnections_.clear();
  systemProxy_ = manager;
  if (!systemProxy_) return;
  systemProxyEnabled_ = systemProxy_->isProxyEnabled();
  emit systemProxyEnabledChanged();
  proxyConnections_.push_back(connect(systemProxy_, &sparkle::core::SystemProxyManager::proxyStateChanged,
                                     this, [this](bool enabled) {
                                       if (systemProxyEnabled_ == enabled) return;
                                       systemProxyEnabled_ = enabled;
                                       emit systemProxyEnabledChanged();
                                     }));
}

void AppModel::setLogManager(sparkle::core::LogManager* log) {
  if (log_ == log) return;
  for (const auto& connection : connections_) QObject::disconnect(connection);
  connections_.clear();
  log_ = log;
  if (core_) {
    connections_.push_back(connect(core_, &sparkle::core::CoreManager::stateChanged, this,
                                   &AppModel::setCoreState));
    connections_.push_back(connect(core_, &sparkle::core::CoreManager::coreStarted, this,
                                   &AppModel::refresh));
  }
  if (log_) {
    connections_.push_back(connect(log_, &sparkle::core::LogManager::mihomoLog, this,
                                   &AppModel::appendLog));
    logs_.clear();
    for (const auto& entry : log_->cachedMihomoLogs()) appendLog(entry);
  }
}

QString AppModel::coreState() const { return coreState_; }
bool AppModel::running() const { return running_; }
bool AppModel::systemProxyEnabled() const { return systemProxyEnabled_; }
QStringList AppModel::groupNames() const { return groupNames_; }
QString AppModel::selectedGroup() const { return selectedGroup_; }
QVariantList AppModel::proxies() const { return proxies_; }
QVariantList AppModel::rules() const { return rules_; }
QVariantList AppModel::logs() const { return logs_; }
QVariantMap AppModel::traffic() const { return traffic_; }
QString AppModel::controllerVersion() const { return controllerVersion_; }
QString AppModel::statusMessage() const { return statusMessage_; }

void AppModel::setCoreState(sparkle::core::CoreState state) {
  const QString next = sparkle::core::toString(state);
  const bool nextRunning = state == sparkle::core::CoreState::Starting ||
                           state == sparkle::core::CoreState::Running;
  if (coreState_ != next) {
    coreState_ = next;
    emit coreStateChanged();
  }
  if (running_ != nextRunning) {
    running_ = nextRunning;
    emit runningChanged();
  }
  setStatusMessage(QStringLiteral("内核状态：") + next);
}

void AppModel::setGroups(const std::vector<sparkle::core::ProxyGroup>& groups) {
  groups_ = groups;
  QStringList next;
  next << QStringLiteral("全部");
  for (const auto& group : groups_) {
    if (!group.hidden) next << group.name;
  }
  if (groupNames_ != next) {
    groupNames_ = next;
    emit groupNamesChanged();
  }
  if (!groupNames_.contains(selectedGroup_)) setSelectedGroup(QStringLiteral("全部"));
  updateVisibleNodes();
}

void AppModel::setNodes(const std::vector<sparkle::core::ProxyNode>& nodes) {
  nodes_ = nodes;
  updateVisibleNodes();
}

void AppModel::updateVisibleNodes() {
  std::vector<sparkle::core::ProxyNode> visible;
  if (selectedGroup_ == QStringLiteral("全部") || selectedGroup_.isEmpty()) {
    visible = nodes_;
  } else {
    for (const auto& group : groups_) {
      if (group.name != selectedGroup_) continue;
      for (const auto& node : nodes_) {
        if (group.all.contains(node.name)) visible.push_back(node);
      }
      break;
    }
  }

  QVariantList next;
  for (const auto& node : visible) {
    QVariantMap item;
    item.insert(QStringLiteral("name"), node.name);
    item.insert(QStringLiteral("type"), node.typeName);
    item.insert(QStringLiteral("server"), node.server);
    item.insert(QStringLiteral("port"), node.port);
    item.insert(QStringLiteral("alive"), node.alive);
    item.insert(QStringLiteral("delay"), node.delay);
    item.insert(QStringLiteral("provider"), node.providerName);
    next.push_back(item);
  }
  proxies_ = next;
  emit proxiesChanged();
}

void AppModel::setSelectedGroup(const QString& group) {
  const QString next = group.isEmpty() ? QStringLiteral("全部") : group;
  if (selectedGroup_ == next) return;
  selectedGroup_ = next;
  emit selectedGroupChanged();
  updateVisibleNodes();
}

void AppModel::setRules(const std::vector<sparkle::core::RuleItem>& rules) {
  QVariantList next;
  for (const auto& rule : rules) {
    QVariantMap item;
    item.insert(QStringLiteral("index"), static_cast<qulonglong>(rule.index));
    item.insert(QStringLiteral("type"), rule.type);
    item.insert(QStringLiteral("payload"), rule.payload);
    item.insert(QStringLiteral("proxy"), rule.proxy);
    item.insert(QStringLiteral("disabled"), rule.disabled);
    item.insert(QStringLiteral("hitCount"), static_cast<qulonglong>(rule.hitCount));
    item.insert(QStringLiteral("missCount"), static_cast<qulonglong>(rule.missCount));
    next.push_back(item);
  }
  rules_ = next;
  emit rulesChanged();
}

void AppModel::appendLog(const sparkle::core::LogEntry& entry) {
  QVariantMap item;
  item.insert(QStringLiteral("seq"), static_cast<qulonglong>(entry.seq));
  item.insert(QStringLiteral("level"), sparkle::core::toString(entry.level));
  item.insert(QStringLiteral("payload"), entry.payload);
  logs_.push_back(item);
  constexpr int kMaxLogs = 2000;
  if (logs_.size() > kMaxLogs) logs_.removeFirst();
  emit logsChanged();
}

void AppModel::setControllerVersion(const sparkle::core::ControllerVersion& version) {
  if (controllerVersion_ == version.version) return;
  controllerVersion_ = version.version;
  emit controllerVersionChanged();
}

void AppModel::setRuleDisabled(qulonglong index, bool disabled) {
  if (!api_) {
    emit errorMessage(QStringLiteral("控制器尚未连接"));
    return;
  }

  // 先更新本地展示，接口失败时通过重新拉取恢复真实状态，避免 UI 长时间漂移。
  for (auto& value : rules_) {
    QVariantMap item = value.toMap();
    if (item.value(QStringLiteral("index")).toULongLong() != index) continue;
    item.insert(QStringLiteral("disabled"), disabled);
    value = item;
    emit rulesChanged();
    break;
  }

  api_->setRuleDisabled(
      static_cast<std::size_t>(index), disabled,
      [] {},
      [this](const QString& message) {
        emit errorMessage(QStringLiteral("规则状态更新失败：") + message);
        refresh();
      });
}

void AppModel::refresh() {
  if (!api_) return;
  api_->fetchVersion([this](const sparkle::core::ControllerVersion& version) {
    setControllerVersion(version);
  });
  api_->fetchProxies(
      [this](const std::vector<sparkle::core::ProxyNode>& nodes) { setNodes(nodes); },
      [this](const QString& message) { emit errorMessage(message); });
  api_->fetchGroups(
      [this](const std::vector<sparkle::core::ProxyGroup>& groups) { setGroups(groups); },
      [this](const QString& message) { emit errorMessage(message); });
  api_->fetchRules(
      [this](const std::vector<sparkle::core::RuleItem>& rules) { setRules(rules); },
      [this](const QString& message) { emit errorMessage(message); });
}

void AppModel::setSystemProxyEnabled(bool enabled) {
  if (systemProxy_) {
    systemProxy_->setProxy(enabled);
    return;
  }
  if (systemProxyEnabled_ == enabled) return;
  systemProxyEnabled_ = enabled;
  emit systemProxyEnabledChanged();
}

void AppModel::setStatusMessage(const QString& message) {
  if (statusMessage_ == message) return;
  statusMessage_ = message;
  emit statusMessageChanged();
}

}  // namespace sparkle::ui
