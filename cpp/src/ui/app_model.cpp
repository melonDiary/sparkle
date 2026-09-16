#include "app_model.h"

#include <QDateTime>
#include <QFile>
#include <algorithm>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "config_manager.h"
#include "core_manager.h"
#include "log_manager.h"
#include "mihomo_api_client.h"
#include "paths.h"
#include "subscription_manager.h"
#include "system_integration.h"
#include "system_proxy_manager.h"

namespace sparkle::ui {
namespace {

using nlohmann::json;

QString profileIdFromJson(const json& item) {
  return QString::fromStdString(item.value("id", std::string()));
}

// profile.yaml items 数组 → QVariantList（供 QML 列表绑定）。
QVariantList profilesFromJson(const json& config) {
  QVariantList result;
  if (!config.contains("items") || !config["items"].is_array()) return result;
  for (const auto& item : config["items"]) {
    if (!item.is_object()) continue;
    QVariantMap map;
    map.insert(QStringLiteral("id"), profileIdFromJson(item));
    map.insert(QStringLiteral("type"),
               QString::fromStdString(item.value("type", "local")));
    map.insert(QStringLiteral("name"),
               QString::fromStdString(item.value("name", std::string())));
    map.insert(QStringLiteral("url"),
               QString::fromStdString(item.value("url", std::string())));
    map.insert(QStringLiteral("interval"), item.value("interval", 0));
    map.insert(QStringLiteral("autoUpdate"), item.value("autoUpdate", true) != false);
    const qint64 updated = item.value("updated", static_cast<qint64>(0));
    map.insert(QStringLiteral("updated"), updated);
    result.push_back(map);
  }
  return result;
}

QString genProfileId() {
  return QStringLiteral("p") + QString::number(QDateTime::currentMSecsSinceEpoch(), 16);
}

// json 值 → QVariant（TUN/DNS/嗅探表单绑定用；数组转 QStringList/QVariantList）。
QVariant jsonToVariant(const json& value) {
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number_integer()) return static_cast<int>(value.get<int>());
  if (value.is_number_unsigned()) return static_cast<qulonglong>(value.get<unsigned>());
  if (value.is_number_float()) return value.get<double>();
  if (value.is_string()) return QString::fromStdString(value.get<std::string>());
  if (value.is_array()) {
    QStringList strings;
    bool allStrings = !value.empty();
    for (const auto& element : value) {
      if (element.is_string()) {
        strings.push_back(QString::fromStdString(element.get<std::string>()));
      } else {
        allStrings = false;
        break;
      }
    }
    if (allStrings) return strings;  // dns-hijack / fake-ip-filter / skip-domain 等
    QVariantList list;
    for (const auto& element : value) list.push_back(jsonToVariant(element));
    return list;
  }
  if (value.is_object()) {
    QVariantMap map;
    for (auto it = value.begin(); it != value.end(); ++it) {
      map.insert(QString::fromStdString(it.key()), jsonToVariant(it.value()));
    }
    return map;
  }
  return {};
}

// QVariant 值 → json（patchControlledConfig 表单补丁反序列化）。
json variantToJson(const QVariant& value) {
  switch (value.typeId()) {
    case QMetaType::Bool:
      return json(value.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
      return json(value.toLongLong());
    case QMetaType::UInt:
    case QMetaType::ULongLong:
      return json(static_cast<unsigned long long>(value.toULongLong()));
    case QMetaType::Double:
      return json(value.toDouble());
    case QMetaType::QString: {
      const QString s = value.toString();
      return json(s.toStdString());
    }
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
      json array = json::array();
      const QVariantList list = value.toList();
      for (const QVariant& element : list) array.push_back(variantToJson(element));
      return array;
    }
    case QMetaType::QVariantMap: {
      json object = json::object();
      const QVariantMap map = value.toMap();
      for (auto it = map.begin(); it != map.end(); ++it) {
        object[it.key().toStdString()] = variantToJson(it.value());
      }
      return object;
    }
    default:
      if (value.canConvert<QString>()) return json(value.toString().toStdString());
      return json(nullptr);
  }
}

}  // namespace

AppModel::AppModel(QObject* parent) : QObject(parent) {
  // 日志刷屏节流：logsChanged 按 50ms 窗口合并，避免日志 flood 时每行整表刷新卡 UI。
  logFlushTimer_.setSingleShot(true);
  logFlushTimer_.setInterval(50);
  connect(&logFlushTimer_, &QTimer::timeout, this, [this] {
    if (!logDirty_) return;
    logDirty_ = false;
    emit logsChanged();
  });
  // 连接列表刷新节流：WS 200ms 推送 → 300ms 窗口合并后 diff + emit。
  connectionsFlushTimer_.setSingleShot(true);
  connectionsFlushTimer_.setInterval(300);
  connect(&connectionsFlushTimer_, &QTimer::timeout, this, [this] {
    if (!connectionsDirty_) return;
    connectionsDirty_ = false;
    QVariantList next;
    next.reserve(static_cast<qsizetype>(connectionsOrder_.size()));
    for (const QString& id : connectionsOrder_) {
      const auto it = connectionsById_.find(id);
      if (it == connectionsById_.end()) continue;
      const auto& c = it->second;
      QVariantMap m;
      m.insert(QStringLiteral("id"), c.id);
      m.insert(QStringLiteral("network"), c.network);
      m.insert(QStringLiteral("type"), c.type);
      m.insert(QStringLiteral("host"), c.host.isEmpty() ? c.destinationIp : c.host);
      m.insert(QStringLiteral("destination"),
               c.destinationIp + QStringLiteral(":") + c.destinationPort);
      m.insert(QStringLiteral("source"),
               c.sourceIp + QStringLiteral(":") + c.sourcePort);
      m.insert(QStringLiteral("rule"),
               c.rulePayload.isEmpty() ? c.rule
                                       : c.rule + QStringLiteral(")") + c.rulePayload);
      m.insert(QStringLiteral("chains"), c.chains.join(QStringLiteral(" / ")));
      m.insert(QStringLiteral("process"), c.process);
      m.insert(QStringLiteral("upload"), static_cast<qulonglong>(c.upload));
      m.insert(QStringLiteral("download"), static_cast<qulonglong>(c.download));
      m.insert(QStringLiteral("start"), c.start);
      next.push_back(m);
    }
    connections_ = next;
    emit connectionsChanged();
  });
  // 开机自启状态来自系统登录项，启动时读一次。
  autostartEnabled_ = sparkle::platform::isAutostartEnabled();
}

AppModel::~AppModel() {
  for (const auto& connection : coreConnections_) QObject::disconnect(connection);
  for (const auto& connection : apiConnections_) QObject::disconnect(connection);
  for (const auto& connection : proxyConnections_) QObject::disconnect(connection);
  for (const auto& connection : configConnections_) QObject::disconnect(connection);
  for (const auto& connection : subscriptionConnections_) QObject::disconnect(connection);
}

void AppModel::setCoreManager(sparkle::core::CoreManager* core) {
  for (const auto& connection : coreConnections_) QObject::disconnect(connection);
  coreConnections_.clear();
  core_ = core;
  if (!core_) return;

  setCoreState(core_->state());
  coreConnections_.push_back(connect(core_, &sparkle::core::CoreManager::stateChanged, this,
                                 &AppModel::setCoreState));
  coreConnections_.push_back(connect(core_, &sparkle::core::CoreManager::coreStarted, this,
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
  apiConnections_.push_back(connect(
      api_, &sparkle::core::MihomoApiClient::memoryUpdated, this,
      [this](const sparkle::core::MemoryStats& stats) {
        memory_.insert(QStringLiteral("inUse"), static_cast<qulonglong>(stats.inUse));
        memory_.insert(QStringLiteral("osLimit"), static_cast<qulonglong>(stats.osLimit));
        emit memoryChanged();
      }));
  apiConnections_.push_back(connect(
      api_, &sparkle::core::MihomoApiClient::connectionsUpdated, this,
      [this](const std::vector<sparkle::core::ConnectionItem>& items) {
        // 概况页连接计数。
        const int count = static_cast<int>(items.size());
        if (connectionCount_ != count) {
          connectionCount_ = count;
          emit connectionCountChanged();
        }
        // 连接页快照：controller 每次全量推送活跃连接，diff 出新增/关闭。
        std::map<QString, bool> seen;
        std::vector<QString> nextOrder;
        nextOrder.reserve(items.size());
        for (const auto& item : items) {
          seen[item.id] = true;
          if (!connectionsById_.count(item.id)) nextOrder.push_back(item.id);
          connectionsById_[item.id] = item;
        }
        // 保持首见顺序：已有连接按原顺序，新连接追加。
        for (const QString& id : connectionsOrder_) {
          if (seen[id]) nextOrder.push_back(id);
        }
        connectionsOrder_ = std::move(nextOrder);
        for (const auto& item : items) {
          if (std::find(connectionsOrder_.begin(), connectionsOrder_.end(), item.id) ==
              connectionsOrder_.end()) {
            connectionsOrder_.push_back(item.id);
          }
        }
        connectionsDirty_ = true;
        if (!connectionsFlushTimer_.isActive()) connectionsFlushTimer_.start();
      }));
}

void AppModel::setSubscriptionManager(sparkle::core::SubscriptionManager* subscription) {
  if (subscription_ == subscription) return;
  for (const auto& connection : subscriptionConnections_) QObject::disconnect(connection);
  subscriptionConnections_.clear();
  subscription_ = subscription;
  if (!subscription_) return;
  // 订阅拉取失败 → 状态栏提示。
  subscriptionConnections_.push_back(
      connect(subscription_, &sparkle::core::SubscriptionManager::fetchFailed, this,
              [this](const QString& message) {
                importingProfile_ = false;
                emit errorMessage(QStringLiteral("订阅拉取失败：") + message);
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

void AppModel::setConfigManager(sparkle::core::ConfigManager* config) {
  if (config_ == config) return;
  for (const auto& connection : configConnections_) QObject::disconnect(connection);
  configConnections_.clear();
  config_ = config;
  if (!config_) return;
  mitmEnabled_ = config_->mitmEnabled();
  emit mitmEnabledChanged();
  configConnections_.push_back(connect(config_, &sparkle::core::ConfigManager::mitmConfigChanged,
                                       this, [this] {
                                         const bool next = config_ ? config_->mitmEnabled() : false;
                                         if (mitmEnabled_ == next) return;
                                         mitmEnabled_ = next;
                                         emit mitmEnabledChanged();
                                       }));

  // 出站模式/侧栏宽度/卡片顺序初始值 + tun/dns/sniffer 表单数据源。
  if (config_) {
    refreshControlledConfigs();
    // 侧栏宽度持久化在 appConfig.siderWidth（原版默认 250，拖拽吸附见 MainWindow）；
    // 卡片顺序在 appConfig.siderOrder（字符串数组，对齐原版 siderOrder）。
    const nlohmann::json app = config_->appConfig();
    if (app.contains("siderWidth") && app["siderWidth"].is_number_integer()) {
      const int width = app["siderWidth"].get<int>();
      if (width > 0 && width != siderWidth_) {
        siderWidth_ = width;
        emit siderWidthChanged();
      }
    }
    if (app.contains("siderOrder") && app["siderOrder"].is_array()) {
      QStringList order;
      for (const auto& entry : app["siderOrder"]) {
        if (entry.is_string()) order << QString::fromStdString(entry.get<std::string>());
      }
      if (!order.isEmpty()) {
        siderOrder_ = order;
        emit siderOrderChanged();
      }
    }
    configConnections_.push_back(
        connect(config_, &sparkle::core::ConfigManager::controlledMihomoConfigChanged, this,
                &AppModel::refreshControlledConfigs));
    // profile.yaml 变更 → 刷新订阅列表 + 重排自动更新定时器（interval/updated 变化都会走这里）。
    configConnections_.push_back(
        connect(config_, &sparkle::core::ConfigManager::profileConfigChanged, this,
                &AppModel::emitProfilesChanged));
    configConnections_.push_back(
        connect(config_, &sparkle::core::ConfigManager::profileConfigChanged, this,
                &AppModel::rescheduleProfileUpdaters));
    // 正文落盘失败 → 稍后重试一次（10s，失败静默降级为状态提示）。
    configConnections_.push_back(
        connect(config_, &sparkle::core::ConfigManager::profileWriteFailed, this, [this](const QString& id) {
          emit errorMessage(QStringLiteral("订阅正文写入失败：") + id);
          scheduleProfileUpdater(id, 10000);
        }));
    emitProfilesChanged();
    rescheduleProfileUpdaters();
  }
}

void AppModel::refreshControlledConfigs() {
  if (!config_) return;
  const nlohmann::json controlled = config_->controlledMihomoConfig();

  // 出站模式（与原版 controledMihomoConfig.mode 一致）。
  const QString mode = controlled.contains("mode") && controlled["mode"].is_string()
                           ? QString::fromStdString(controlled["mode"].get<std::string>())
                           : QString();
  if (!mode.isEmpty() && mode != outboundMode_) {
    outboundMode_ = mode;
    emit outboundModeChanged();
  }

  // TUN/DNS/嗅探对象整体转 QVariantMap，QML 表单直接绑字段。
  tunConfig_ = controlled.contains("tun") && controlled["tun"].is_object()
                   ? jsonToVariant(controlled["tun"]).toMap()
                   : QVariantMap();
  dnsConfig_ = controlled.contains("dns") && controlled["dns"].is_object()
                   ? jsonToVariant(controlled["dns"]).toMap()
                   : QVariantMap();
  snifferConfig_ = controlled.contains("sniffer") && controlled["sniffer"].is_object()
                       ? jsonToVariant(controlled["sniffer"]).toMap()
                       : QVariantMap();
  emit controlledConfigChanged();
}

void AppModel::setMitmEnabled(bool enabled) {
  if (!config_) {
    emit errorMessage(QStringLiteral("配置尚未就绪"));
    return;
  }
  if (mitmEnabled_ == enabled) return;
  nlohmann::json patch;
  patch["mitm_enabled"] = enabled;
  // patchAppConfig 会触发 mitmConfigChanged：AppController 据此启动/停止 MITM，
  // 同时写回 mitmEnabled_ 并刷新 UI。
  config_->patchAppConfig(patch);
}

void AppModel::setAutostartEnabled(bool enabled) {
  if (autostartEnabled_ == enabled) return;
  const sparkle::platform::AutostartResult result =
      sparkle::platform::setAutostartEnabled(enabled);
  if (!result.ok) {
    emit errorMessage(QStringLiteral("设置开机自启失败：") + result.message);
    return;
  }
  autostartEnabled_ = enabled;
  emit autostartEnabledChanged();
}

QString AppModel::outboundMode() const { return outboundMode_; }

void AppModel::setOutboundMode(const QString& mode) { requestOutboundMode(mode); }

void AppModel::requestOutboundMode(const QString& mode) {
  if (mode != QStringLiteral("rule") && mode != QStringLiteral("global") &&
      mode != QStringLiteral("direct")) {
    return;
  }
  if (!config_) {
    emit errorMessage(QStringLiteral("配置尚未就绪"));
    return;
  }
  if (outboundMode_ == mode) return;
  nlohmann::json patch;
  patch["mode"] = mode.toStdString();
  // patchControlledMihomoConfig：持久化 + controlledMihomoConfigChanged（回写 UI）
  // + reloadRequested（AppController 重启内核使模式生效）。
  config_->patchControlledMihomoConfig(patch);
}

int AppModel::siderWidth() const { return siderWidth_; }

void AppModel::setSiderWidth(int width) {
  if (width == siderWidth_) return;
  siderWidth_ = width;
  emit siderWidthChanged();
  if (!config_) return;
  nlohmann::json patch;
  patch["siderWidth"] = width;
  config_->patchAppConfig(patch);
}

QVariantMap AppModel::tunConfig() const { return tunConfig_; }
QVariantMap AppModel::dnsConfig() const { return dnsConfig_; }
QVariantMap AppModel::snifferConfig() const { return snifferConfig_; }

void AppModel::patchControlledConfig(const QVariantMap& patch) {
  if (!config_) {
    emit errorMessage(QStringLiteral("配置尚未就绪"));
    return;
  }
  if (patch.isEmpty()) return;
  // variantToJson：QVariantMap → json；patchControlledMihomoConfig 深合并 →
  // controlledMihomoConfigChanged（回写表单）+ reloadRequested（重启内核生效）。
  config_->patchControlledMihomoConfig(variantToJson(patch));
}

QStringList AppModel::siderOrder() const { return siderOrder_; }

void AppModel::setSiderOrder(const QStringList& order) {
  if (siderOrder_ == order) return;
  siderOrder_ = order;
  emit siderOrderChanged();
  if (!config_) return;
  nlohmann::json patch;
  patch["siderOrder"] = nlohmann::json::array();
  for (const QString& key : order) patch["siderOrder"].push_back(key.toStdString());
  config_->patchAppConfig(patch);
}

void AppModel::setLogManager(sparkle::core::LogManager* log) {
  if (log_ == log) return;
  for (const auto& connection : coreConnections_) QObject::disconnect(connection);
  coreConnections_.clear();
  log_ = log;
  if (core_) {
    coreConnections_.push_back(connect(core_, &sparkle::core::CoreManager::stateChanged, this,
                                   &AppModel::setCoreState));
    coreConnections_.push_back(connect(core_, &sparkle::core::CoreManager::coreStarted, this,
                                   &AppModel::refresh));
  }
  if (log_) {
    coreConnections_.push_back(connect(log_, &sparkle::core::LogManager::mihomoLog, this,
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
QVariantMap AppModel::memory() const { return memory_; }
int AppModel::connectionCount() const { return connectionCount_; }
bool AppModel::mitmEnabled() const { return mitmEnabled_; }
bool AppModel::autostartEnabled() const { return autostartEnabled_; }
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

  // 仅在选中具体分组时标注当前节点（"全部"视图无法唯一确定节点归属）。
  const bool concrete = selectedGroup_ != QStringLiteral("全部") && !selectedGroup_.isEmpty();
  QString currentNow;
  if (concrete) {
    for (const auto& group : groups_) {
      if (group.name == selectedGroup_) {
        currentNow = group.now;
        break;
      }
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
    item.insert(QStringLiteral("current"), concrete && node.name == currentNow);
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
  logDirty_ = true;
  if (!logFlushTimer_.isActive()) logFlushTimer_.start();
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

void AppModel::activateNode(const QString& name) {
  if (name.isEmpty()) return;
  if (!api_) {
    emit errorMessage(QStringLiteral("控制器尚未连接"));
    return;
  }

  QString group = selectedGroup_;
  if (group.isEmpty() || group == QStringLiteral("全部")) {
    // "全部"视图：定位到包含该节点的第一个非隐藏分组，避免要求用户先切分组。
    for (const auto& g : groups_) {
      if (g.all.contains(name)) {
        group = g.name;
        break;
      }
    }
  }
  if (group.isEmpty() || group == QStringLiteral("全部")) {
    emit errorMessage(QStringLiteral("请先选择具体分组，再切换节点"));
    return;
  }

  api_->changeProxy(
      group, name,
      [this](const sparkle::core::ProxyGroup& updated) {
        // 就地更新该分组的 now，避免整表刷新打断用户正在查看的分组。
        for (auto& g : groups_) {
          if (g.name == updated.name) {
            g.now = updated.now;
            break;
          }
        }
        updateVisibleNodes();
      },
      [this](const QString& message) {
        emit errorMessage(QStringLiteral("切换节点失败：") + message);
      });
}

void AppModel::testNodeDelay(const QString& name) {
  if (name.isEmpty()) return;
  if (!api_) {
    emit errorMessage(QStringLiteral("控制器尚未连接"));
    return;
  }
  api_->testDelay(
      name,
      [this, name](int delay) {
        for (auto& value : proxies_) {
          QVariantMap item = value.toMap();
          if (item.value(QStringLiteral("name")).toString() != name) continue;
          item.insert(QStringLiteral("delay"), delay);
          value = item;
          emit proxiesChanged();
          break;
        }
      },
      [this](const QString& message) {
        emit errorMessage(QStringLiteral("延迟测试失败：") + message);
      });
}

void AppModel::setStatusMessage(const QString& message) {
  if (statusMessage_ == message) return;
  statusMessage_ = message;
  emit statusMessageChanged();
}

// ===== 连接页 =====

QVariantList AppModel::connections() const { return connections_; }

void AppModel::setConnections(const std::vector<sparkle::core::ConnectionItem>& items) {
  // 由 connectionsUpdated 回调在 flush 定时器里重建列表；此方法保留给需要
  // 全量覆盖的调用方（当前无），逻辑见构造函数中的 connectionsFlushTimer_ lambda。
  Q_UNUSED(items);
}

void AppModel::closeConnection(const QString& id) {
  if (!api_) {
    emit errorMessage(QStringLiteral("控制器尚未连接"));
    return;
  }
  api_->closeConnection(id, [] {}, [this](const QString& message) {
    emit errorMessage(QStringLiteral("关闭连接失败：") + message);
  });
}

void AppModel::closeAllConnections() {
  if (!api_) {
    emit errorMessage(QStringLiteral("控制器尚未连接"));
    return;
  }
  api_->closeAllConnections([] {}, [this](const QString& message) {
    emit errorMessage(QStringLiteral("关闭全部连接失败：") + message);
  });
}

// ===== 订阅页 =====

QVariantList AppModel::profiles() const { return profiles_; }

QString AppModel::currentProfileId() const { return currentProfileId_; }

void AppModel::emitProfilesChanged() {
  if (!config_) return;
  const json config = config_->profileConfig();
  profiles_ = profilesFromJson(config);
  currentProfileId_ =
      QString::fromStdString(config.value("current", std::string()));
  emit profilesChanged();
}

void AppModel::saveProfileConfig(const json& config) {
  if (!config_) return;
  config_->setProfileConfig(config);  // 触发 profileConfigChanged → emitProfilesChanged
}

void AppModel::regenerateRuntimeConfig() {
  // 与 ConfigManager::reloadRequested 的消费者一致：重新生成运行配置 + 重启内核。
  if (core_) core_->restart();
}

void AppModel::importProfile(const QString& url, const QString& name) {
  if (!config_) {
    emit errorMessage(QStringLiteral("配置尚未就绪"));
    return;
  }
  if (importingProfile_) {
    emit errorMessage(QStringLiteral("已有订阅正在导入，请稍候"));
    return;
  }
  if (url.trimmed().isEmpty()) {
    emit errorMessage(QStringLiteral("订阅地址不能为空"));
    return;
  }

  json config = config_->profileConfig();
  json item;
  item["id"] = genProfileId().toStdString();
  item["type"] = "remote";
  const QString resolvedName = name.trimmed().isEmpty()
                                   ? QStringLiteral("订阅 %1").arg(
                                         QDateTime::currentMSecsSinceEpoch() % 10000)
                                   : name.trimmed();
  item["name"] = resolvedName.toStdString();
  item["url"] = url.trimmed().toStdString();
  item["updated"] = QDateTime::currentMSecsSinceEpoch();
  item["autoUpdate"] = true;  // 新订阅默认自动更新（interval 由订阅响应头或后续编辑设置）

  // 先落盘条目（含 url），再异步拉取正文；失败时回滚条目并提示。
  if (!config.contains("items") || !config["items"].is_array()) config["items"] = json::array();
  config["items"].push_back(item);
  saveProfileConfig(config);
  updateProfile(QString::fromStdString(item["id"].get<std::string>()));
}

void AppModel::updateProfile(const QString& id) {
  if (!config_ || !subscription_) {
    emit errorMessage(QStringLiteral("订阅服务尚未就绪"));
    return;
  }
  if (importingProfile_) {
    emit errorMessage(QStringLiteral("已有订阅正在导入，请稍候"));
    return;
  }

  const json config = config_->profileConfig();
  const json* target = nullptr;
  if (config.contains("items") && config["items"].is_array()) {
    for (const auto& item : config["items"]) {
      if (item.is_object() && profileIdFromJson(item) == id) {
        target = &item;
        break;
      }
    }
  }
  if (!target) {
    emit errorMessage(QStringLiteral("未找到订阅：") + id);
    return;
  }
  const QString url = QString::fromStdString(target->value("url", std::string()));
  if (url.isEmpty()) {
    emit errorMessage(QStringLiteral("该订阅没有可用的远程地址"));
    return;
  }

  importingProfile_ = true;
  setStatusMessage(QStringLiteral("正在拉取订阅…"));
  subscription_->fetch(
      url,
      [this, id](const std::vector<sparkle::core::ProxyNode>& nodes) {
        importingProfile_ = false;
        // 正文 = 节点 YAML（RuntimeConfigFactory 的 profile 基座格式）。
        QString text;
        for (const auto& node : nodes) {
          text += QStringLiteral("- {name: ") + node.name +
                  QStringLiteral(", type: ") + node.typeName +
                  QStringLiteral(", server: ") + node.server +
                  QStringLiteral(", port: ") + QString::number(node.port) +
                  QStringLiteral("}\n");
        }
        config_->setProfileRawText(id, text);

        // 刷新 updated 时间戳。
        json cfg = config_->profileConfig();
        if (cfg.contains("items") && cfg["items"].is_array()) {
          for (auto& item : cfg["items"]) {
            if (item.is_object() && profileIdFromJson(item) == id) {
              item["updated"] = QDateTime::currentMSecsSinceEpoch();
              break;
            }
          }
          saveProfileConfig(cfg);
        }
        setStatusMessage(QStringLiteral("订阅已更新（%1 个节点）").arg(nodes.size()));
        // 当前订阅正文变更 → 运行配置重新生成 + 内核重启。
        if (currentProfileId_ == id) regenerateRuntimeConfig();
      },
      [this](const QString& message) {
        importingProfile_ = false;
        emit errorMessage(QStringLiteral("订阅拉取失败：") + message);
      });
}

void AppModel::setCurrentProfile(const QString& id) {
  if (!config_) return;
  if (currentProfileId_ == id) return;
  json config = config_->profileConfig();
  config["current"] = id.toStdString();
  saveProfileConfig(config);
  setStatusMessage(QStringLiteral("已切换订阅，正在重启内核…"));
  regenerateRuntimeConfig();
}

void AppModel::deleteProfile(const QString& id) {
  if (!config_) return;
  json config = config_->profileConfig();
  if (!config.contains("items") || !config["items"].is_array()) return;

  json items = json::array();
  QString removedName;
  for (const auto& item : config["items"]) {
    if (item.is_object() && profileIdFromJson(item) == id) {
      removedName = QString::fromStdString(item.value("name", std::string()));
      continue;
    }
    items.push_back(item);
  }
  if (removedName.isEmpty()) return;
  config["items"] = items;

  // 删掉的是当前订阅 → 自动切到剩余第一个。
  const bool wasCurrent = currentProfileId_ == id;
  if (wasCurrent) {
    config["current"] = !items.empty() && items.front().is_object()
                            ? items.front().value("id", std::string())
                            : std::string();
  }
  saveProfileConfig(config);

  // 定时器与正文文件一并清理（对齐原版 delProfileUpdater）。
  clearProfileUpdater(id);
  QFile::remove(sparkle::core::Paths::profilePath(id));
  setStatusMessage(QStringLiteral("已删除订阅：") + removedName);
  if (wasCurrent) regenerateRuntimeConfig();
}

// ===== 订阅自动更新（对齐原版 profileUpdater.ts）=====

void AppModel::updateProfileSilently(const QString& id) {
  if (!config_ || !subscription_) return;
  if (importingProfile_) {
    // 手动导入/更新进行中：稍后重试（10s，对齐原版当前订阅 +10s 的错峰思路）。
    scheduleProfileUpdater(id, 10000);
    return;
  }

  const json config = config_->profileConfig();
  const json* target = nullptr;
  if (config.contains("items") && config["items"].is_array()) {
    for (const auto& item : config["items"]) {
      if (item.is_object() && profileIdFromJson(item) == id) {
        target = &item;
        break;
      }
    }
  }
  if (!target) return;
  const QString url = QString::fromStdString(target->value("url", std::string()));
  const QString name = QString::fromStdString(target->value("name", std::string()));
  if (url.isEmpty()) return;

  importingProfile_ = true;
  subscription_->fetch(
      url,
      [this, id, name](const std::vector<sparkle::core::ProxyNode>& nodes) {
        importingProfile_ = false;
        QString text;
        for (const auto& node : nodes) {
          text += QStringLiteral("- {name: ") + node.name +
                  QStringLiteral(", type: ") + node.typeName +
                  QStringLiteral(", server: ") + node.server +
                  QStringLiteral(", port: ") + QString::number(node.port) +
                  QStringLiteral("}\n");
        }
        config_->setProfileRawText(id, text);

        // 刷新 updated 时间戳（下一次调度据此计算剩余延迟）。
        json cfg = config_->profileConfig();
        if (cfg.contains("items") && cfg["items"].is_array()) {
          for (auto& item : cfg["items"]) {
            if (item.is_object() && profileIdFromJson(item) == id) {
              item["updated"] = QDateTime::currentMSecsSinceEpoch();
              break;
            }
          }
          saveProfileConfig(cfg);
        }
        setStatusMessage(QStringLiteral("订阅自动更新成功（%1，%2 个节点）").arg(name).arg(nodes.size()));
        // 当前订阅正文变更 → 运行配置重新生成 + 内核重启。
        if (currentProfileId_ == id) regenerateRuntimeConfig();
      },
      [this, id, name](const QString& message) {
        importingProfile_ = false;
        emit errorMessage(QStringLiteral("订阅自动更新失败（%1）：%2").arg(name, message));
      });
}

void AppModel::scheduleProfileUpdater(const QString& id, qint64 delayMs) {
  QTimer* timer = nullptr;
  const auto it = profileUpdateTimers_.find(id);
  if (it != profileUpdateTimers_.end()) {
    timer = it->second;
  } else {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, id] {
      // 触发时重新校验该订阅仍存在且允许自动更新（期间可能被改/删）。
      const json cfg = config_ ? config_->profileConfig() : json::object();
      bool valid = false;
      qint64 intervalMinutes = 0;
      if (cfg.contains("items") && cfg["items"].is_array()) {
        for (const auto& item : cfg["items"]) {
          if (item.is_object() && profileIdFromJson(item) == id) {
            intervalMinutes = static_cast<qint64>(item.value("interval", 0));
            valid = item.value("type", "") == "remote" && intervalMinutes > 0 &&
                    item.value("autoUpdate", true) != false;
            break;
          }
          
        }
      }
      if (!valid) return;
      updateProfileSilently(id);
      // 周期化：按 interval 重新调度（从本次触发时刻起算）。
      scheduleProfileUpdater(id, intervalMinutes * 60 * 1000);
    });
    profileUpdateTimers_[id] = timer;
  }
  timer->start(static_cast<int>(delayMs));
}

void AppModel::clearProfileUpdater(const QString& id) {
  const auto it = profileUpdateTimers_.find(id);
  if (it == profileUpdateTimers_.end()) return;
  it->second->stop();
  it->second->deleteLater();
  profileUpdateTimers_.erase(it);
}

void AppModel::rescheduleProfileUpdaters() {
  if (!config_) return;
  const json config = config_->profileConfig();
  if (!config.contains("items") || !config["items"].is_array()) return;

  const QString current = QString::fromStdString(config.value("current", std::string()));
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  for (const auto& item : config["items"]) {
    if (!item.is_object()) continue;
    const QString id = profileIdFromJson(item);
    if (id.isEmpty()) continue;
    const bool isRemote = item.value("type", "") == "remote";
    const qint64 intervalMinutes = static_cast<qint64>(item.value("interval", 0));
    const bool autoUpdate = item.value("autoUpdate", true) != false;
    if (!isRemote || intervalMinutes <= 0 || !autoUpdate) {
      clearProfileUpdater(id);
      continue;
    }

    // calculateUpdateDelay：已到期 → 立即更新并按完整 interval 重排；
    // 未到期 → 剩余延迟后触发。当前订阅额外 +10s 错峰（对齐原版）。
    const bool isCurrent = id == current;
    const qint64 intervalMs = intervalMinutes * 60 * 1000;
    const qint64 lastUpdated = item.value("updated", static_cast<qint64>(0));
    const qint64 sinceUpdate = now - lastUpdated;
    qint64 delay = sinceUpdate >= intervalMs ? 0 : intervalMs - sinceUpdate;
    if (isCurrent) delay += 10000;

    if (delay == 0 || delay == 10000) {
      updateProfileSilently(id);
      scheduleProfileUpdater(id, intervalMs + (isCurrent ? 10000 : 0));
    } else {
      scheduleProfileUpdater(id, delay);
    }
  }
}

void AppModel::setProfileAutoUpdate(const QString& id, bool enabled) {
  if (!config_) return;
  json config = config_->profileConfig();
  if (!config.contains("items") || !config["items"].is_array()) return;
  for (auto& item : config["items"]) {
    if (item.is_object() && profileIdFromJson(item) == id) {
      item["autoUpdate"] = enabled;
      break;
    }
  }
  saveProfileConfig(config);  // profileConfigChanged → reschedule（见构造/绑定处）
}

}  // namespace sparkle::ui
