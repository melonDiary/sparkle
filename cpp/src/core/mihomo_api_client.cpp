#include "mihomo_api_client.h"

#include <QDateTime>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "config_manager.h"
#include "log_manager.h"
#include "paths.h"
#include "ws_client.h"

namespace sparkle::core {
namespace {

using nlohmann::json;

QString controllerError(const HttpResult& result) {
  if (!result.error.isEmpty()) return result.error;
  if (result.status > 0) {
    return QStringLiteral("controller returned HTTP %1").arg(result.status);
  }
  return QStringLiteral("controller request failed");
}

QString encodedPathPart(const QString& value) {
  return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

std::vector<DelaySample> delayHistory(const json& detail) {
  std::vector<DelaySample> result;
  if (!detail.contains("history") || !detail["history"].is_array()) return result;
  for (const auto& sample : detail["history"]) {
    if (!sample.is_object()) continue;
    DelaySample item;
    const QString time = QString::fromStdString(sample.value("time", std::string()));
    const QDateTime parsed = QDateTime::fromString(time, Qt::ISODateWithMs);
    item.time = parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
    item.delay = sample.value("delay", -1);
    result.push_back(item);
  }
  return result;
}

// mihomo /connections 单条连接 → ConnectionItem（metadata 平铺到 item）。
ConnectionItem connectionItemFromJson(const json& j) {
  ConnectionItem item;
  item.id = QString::fromStdString(j.value("id", std::string()));
  const json meta = j.value("metadata", json::object());
  item.network = QString::fromStdString(meta.value("network", std::string()));
  item.type = QString::fromStdString(meta.value("type", std::string()));
  item.sourceIp = QString::fromStdString(meta.value("sourceIP", std::string()));
  item.sourcePort = QString::fromStdString(meta.value("sourcePort", std::string()));
  item.destinationIp = QString::fromStdString(meta.value("destinationIP", std::string()));
  item.destinationPort = QString::fromStdString(meta.value("destinationPort", std::string()));
  item.host = QString::fromStdString(meta.value("host", std::string()));
  item.process = QString::fromStdString(meta.value("process", std::string()));
  item.processPath = QString::fromStdString(meta.value("processPath", std::string()));
  if (j.contains("chains") && j["chains"].is_array()) {
    for (const auto& c : j["chains"]) {
      item.chains << QString::fromStdString(c.get<std::string>());
    }
  }
  item.rule = QString::fromStdString(j.value("rule", std::string()));
  item.rulePayload = QString::fromStdString(j.value("rulePayload", std::string()));
  item.upload = j.value("upload", 0ull);
  item.download = j.value("download", 0ull);

  const QString startStr = QString::fromStdString(j.value("start", std::string()));
  const QDateTime start = QDateTime::fromString(startStr, Qt::ISODateWithMs);
  item.start = start.isValid() ? start.toMSecsSinceEpoch() : 0;
  return item;
}

}  // namespace

ProxyNode proxyNodeFromControllerJson(const QString& name, const json& detail) {
  ProxyNode node;
  node.name = name;
  node.typeName = QString::fromStdString(detail.value("type", std::string()));
  node.type = proxyTypeFromString(node.typeName);
  node.alive = detail.value("alive", false);
  node.delay = -1;
  node.providerName = QString::fromStdString(detail.value("provider-name", std::string()));
  node.history = delayHistory(detail);
  if (!node.history.empty()) node.delay = node.history.back().delay;
  node.server = QString::fromStdString(detail.value("server", std::string()));
  node.port = detail.value("port", 0);
  node.cipher = QString::fromStdString(detail.value("cipher", std::string()));
  node.password = QString::fromStdString(detail.value("password", std::string()));
  return node;
}

std::vector<RuleItem> rulesFromControllerJson(const json& response) {
  std::vector<RuleItem> result;
  if (!response.is_object() || !response.contains("rules") || !response["rules"].is_array()) {
    return result;
  }

  for (const auto& value : response["rules"]) {
    if (!value.is_object()) continue;
    RuleItem rule;
    rule.index = value.value("index", result.size());
    rule.type = QString::fromStdString(value.value("type", std::string()));
    rule.payload = QString::fromStdString(value.value("payload", std::string()));
    rule.proxy = QString::fromStdString(value.value("proxy", std::string()));
    rule.size = value.value("size", 0u);
    const json extra = value.value("extra", json::object());
    if (extra.is_object()) {
      rule.disabled = extra.value("disabled", false);
      rule.hitCount = extra.value("hitCount", 0ull);
      rule.missCount = extra.value("missCount", 0ull);
    }
    result.push_back(std::move(rule));
  }
  return result;
}

ProxyGroup proxyGroupFromControllerJson(const QString& name, const json& detail) {
  ProxyGroup group;
  group.name = name;
  group.typeName = QString::fromStdString(detail.value("type", std::string()));
  group.type = proxyTypeFromString(group.typeName);
  group.alive = detail.value("alive", false);
  group.now = QString::fromStdString(detail.value("now", std::string()));
  group.hidden = detail.value("hidden", false);
  group.fixed = detail.contains("fixed") && !detail["fixed"].is_null();
  group.testUrl = QString::fromStdString(detail.value("testUrl", std::string()));
  if (detail.contains("all") && detail["all"].is_array()) {
    for (const auto& item : detail["all"]) {
      if (item.is_string()) group.all << QString::fromStdString(item.get<std::string>());
    }
  }
  return group;
}

std::vector<ProxyNode> proxyNodesFromControllerJson(const json& response) {
  std::vector<ProxyNode> result;
  if (!response.is_object() || !response.contains("proxies") || !response["proxies"].is_object()) {
    return result;
  }
  for (auto it = response["proxies"].begin(); it != response["proxies"].end(); ++it) {
    if (!it.value().is_object() || it.value().contains("all")) continue;
    result.push_back(proxyNodeFromControllerJson(QString::fromStdString(it.key()), it.value()));
  }
  return result;
}

std::vector<ProxyGroup> proxyGroupsFromControllerJson(const json& response) {
  std::vector<ProxyGroup> result;
  if (!response.is_object() || !response.contains("proxies") || !response["proxies"].is_object()) {
    return result;
  }
  for (auto it = response["proxies"].begin(); it != response["proxies"].end(); ++it) {
    if (!it.value().is_object() || !it.value().contains("all")) continue;
    const ProxyGroup group = proxyGroupFromControllerJson(
        QString::fromStdString(it.key()), it.value());
    if (!group.hidden) result.push_back(group);
  }
  return result;
}

MihomoApiClient::MihomoApiClient(ConfigManager* config, LogManager* log, QObject* parent)
    : QObject(parent), config_(config), log_(log) {
  trafficSender_ = std::make_unique<LatestSender<TrafficStats>>(
      100, [this](const TrafficStats& stats) { emit trafficUpdated(stats); }, this);
  connectionsSender_ = std::make_unique<LatestSender<std::vector<ConnectionItem>>>(
      200, [this](const std::vector<ConnectionItem>& items) { emit connectionsUpdated(items); },
      this);
}

MihomoApiClient::~MihomoApiClient() { stopStreams(); }

void MihomoApiClient::setServiceMode(bool service) { serviceMode_ = service; }

QString MihomoApiClient::endpoint() const {
  if (serviceMode_) {
    const ServiceControllerEndpoint sc = config_->serviceControllerEndpoint();
    return sc.host + QLatin1Char(':') + QString::number(sc.port);
  }
  return Paths::controllerSocket();
}

QString MihomoApiClient::secret() const {
  if (serviceMode_) {
    return config_->serviceControllerEndpoint().secret;
  }
  return QString();
}

void MihomoApiClient::reset() {
  stopStreams();
  trafficSender_->clear();
  connectionsSender_->clear();
}

void MihomoApiClient::startStreams() {
  const QString ep = endpoint();
  if (ep.isEmpty()) return;
  const QString sec = secret();

  traffic_ = std::make_unique<WsClient>(this);
  traffic_->configure(ep, QStringLiteral("/traffic"), 10, serviceMode_, sec);
  connect(traffic_.get(), &WsClient::messageReceived, this, &MihomoApiClient::onTrafficMessage);
  traffic_->start();

  memory_ = std::make_unique<WsClient>(this);
  memory_->configure(ep, QStringLiteral("/memory"), 10, serviceMode_, sec);
  connect(memory_.get(), &WsClient::messageReceived, this, &MihomoApiClient::onMemoryMessage);
  memory_->start();

  logs_ = std::make_unique<WsClient>(this);
  logs_->configure(ep, QStringLiteral("/logs"), 10, serviceMode_, sec);
  connect(logs_.get(), &WsClient::messageReceived, this, &MihomoApiClient::onLogsMessage);
  logs_->start();

  connections_ = std::make_unique<WsClient>(this);
  connections_->configure(ep, QStringLiteral("/connections"), 10, serviceMode_, sec);
  connect(connections_.get(), &WsClient::messageReceived, this,
          &MihomoApiClient::onConnectionsMessage);
  connections_->start();
}

void MihomoApiClient::stopStreams() {
  if (traffic_) traffic_->stop();
  if (memory_) memory_->stop();
  if (logs_) logs_->stop();
  if (connections_) connections_->stop();
  traffic_.reset();
  memory_.reset();
  logs_.reset();
  connections_.reset();
}

void MihomoApiClient::restartLogsStream() {
  if (logs_) logs_->restart();
}

void MihomoApiClient::restartConnectionsStream() {
  if (connections_) connections_->restart();
}

void MihomoApiClient::fetchProxies(
    const std::function<void(const std::vector<ProxyNode>&)>& onDone,
    const std::function<void(const QString&)>& onError) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  http_.get(QStringLiteral("/proxies"), [onDone, onError](const HttpResult& res) {
    if (!res.ok) {
      if (onError) onError(controllerError(res));
      return;
    }
    try {
      const auto parsed = proxyNodesFromControllerJson(json::parse(res.body.toStdString()));
      if (onDone) onDone(parsed);
    } catch (const std::exception& error) {
      if (onError) onError(QString::fromUtf8(error.what()));
    }
  });
}

void MihomoApiClient::fetchGroups(
    const std::function<void(const std::vector<ProxyGroup>&)>& onDone,
    const std::function<void(const QString&)>& onError) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  http_.get(QStringLiteral("/proxies"), [onDone, onError](const HttpResult& res) {
    if (!res.ok) {
      if (onError) onError(controllerError(res));
      return;
    }
    try {
      const auto parsed = proxyGroupsFromControllerJson(json::parse(res.body.toStdString()));
      if (onDone) onDone(parsed);
    } catch (const std::exception& error) {
      if (onError) onError(QString::fromUtf8(error.what()));
    }
  });
}

void MihomoApiClient::fetchRules(
    const std::function<void(const std::vector<RuleItem>&)>& onDone,
    const std::function<void(const QString&)>& onError) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  http_.get(QStringLiteral("/rules"), [onDone, onError](const HttpResult& res) {
    if (!res.ok) {
      if (onError) onError(controllerError(res));
      return;
    }
    try {
      const auto parsed = rulesFromControllerJson(json::parse(res.body.toStdString()));
      if (onDone) onDone(parsed);
    } catch (const std::exception& error) {
      if (onError) onError(QString::fromUtf8(error.what()));
    }
  });
}

void MihomoApiClient::setRuleDisabled(
    std::size_t index, bool disabled, const std::function<void()>& onDone,
    const std::function<void(const QString&)>& onError) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  const QByteArray body = QByteArray::fromStdString(
      json{{std::to_string(index), disabled}}.dump());
  http_.request(QStringLiteral("PATCH"), QStringLiteral("/rules/disable"), body,
                [onDone, onError](const HttpResult& res) {
                  if (!res.ok) {
                    if (onError) onError(controllerError(res));
                    return;
                  }
                  if (onDone) onDone();
                });
}

void MihomoApiClient::changeProxy(
    const QString& group, const QString& proxy,
    const std::function<void(const ProxyGroup&)>& onDone,
    const std::function<void(const QString&)>& onError) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  const QByteArray body = QByteArray::fromStdString(json{{"name", proxy.toStdString()}}.dump());
  http_.request(QStringLiteral("PUT"),
                QStringLiteral("/proxies/") + encodedPathPart(group), body,
                [onDone, onError, group](const HttpResult& res) {
                  if (!res.ok) {
                    if (onError) onError(controllerError(res));
                    return;
                  }
                  try {
                    const json parsed = json::parse(res.body.toStdString());
                    if (onDone) onDone(proxyGroupFromControllerJson(group, parsed));
                  } catch (const std::exception& error) {
                    if (onError) onError(QString::fromUtf8(error.what()));
                  }
                });
}

void MihomoApiClient::unfixProxy(
    const QString& group, const std::function<void(const ProxyGroup&)>& onDone,
    const std::function<void(const QString&)>& onError) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  http_.request(QStringLiteral("DELETE"),
                QStringLiteral("/proxies/") + encodedPathPart(group), QByteArray(),
                [onDone, onError, group](const HttpResult& res) {
                  if (!res.ok) {
                    if (onError) onError(controllerError(res));
                    return;
                  }
                  try {
                    const json parsed = json::parse(res.body.toStdString());
                    if (onDone) onDone(proxyGroupFromControllerJson(group, parsed));
                  } catch (const std::exception& error) {
                    if (onError) onError(QString::fromUtf8(error.what()));
                  }
                });
}

void MihomoApiClient::fetchVersion(
    const std::function<void(const ControllerVersion&)>& onDone) {
  http_.setEndpoint(endpoint(), serviceMode_);
  http_.setSecret(secret());
  http_.get(QStringLiteral("/version"), [onDone](const HttpResult& res) {
    ControllerVersion v;
    if (res.ok && !res.body.isEmpty()) {
      try {
        const json j = json::parse(res.body.toStdString());
        v.version = QString::fromStdString(j.value("version", std::string()));
        v.meta = j.value("meta", false);
      } catch (...) {
        // 解析失败：version 留空，交由上层判定
      }
    }
    if (onDone) onDone(v);
  });
}

void MihomoApiClient::onTrafficMessage(const QByteArray& payload) {
  TrafficStats stats;
  try {
    const json j = json::parse(payload.toStdString());
    stats.upload = j.value("up", 0ull);
    stats.download = j.value("down", 0ull);
  } catch (...) {
    return;
  }
  trafficSender_->send(stats);
}

void MihomoApiClient::onMemoryMessage(const QByteArray& payload) {
  MemoryStats stats;
  try {
    const json j = json::parse(payload.toStdString());
    // 兼容 inuse/inUse 与 oslimit/osLimit 两种字段拼写。
    stats.inUse = j.contains("inuse") ? j["inuse"].get<std::uint64_t>()
                                      : j.value("inUse", 0ull);
    stats.osLimit = j.contains("oslimit") ? j["oslimit"].get<std::uint64_t>()
                                          : j.value("osLimit", 0ull);
  } catch (...) {
    return;
  }
  emit memoryUpdated(stats);
}

void MihomoApiClient::onLogsMessage(const QByteArray& payload) {
  // /logs 为文本行块，交给 LogManager 按行解析发布（WS 语义）。
  log_->publishMihomoLogLines(QString::fromUtf8(payload));
}

void MihomoApiClient::onConnectionsMessage(const QByteArray& payload) {
  try {
    json j = json::parse(payload.toStdString());
    // 兼容两种形态：顶层数组，或 {"connections": [...]}。
    if (j.is_object() && j.contains("connections") && j["connections"].is_array()) {
      j = j["connections"];
    }
    if (!j.is_array()) return;

    std::vector<ConnectionItem> items;
    items.reserve(j.size());
    for (const auto& c : j) {
      items.push_back(connectionItemFromJson(c));
    }
    connectionsSender_->send(items);
  } catch (...) {
    emit connectionsError(QStringLiteral("connections parse failed"));
  }
}

}  // namespace sparkle::core