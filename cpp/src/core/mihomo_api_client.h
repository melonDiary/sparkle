#pragma once

#include <QObject>
#include <functional>
#include <memory>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "http_client.h"
#include "latest_sender.h"
#include "models.h"

namespace sparkle::core {

class ConfigManager;
class LogManager;
class WsClient;

// Mihomo 外部控制器客户端（对应原 core/mihomoApi.ts + mihomo-stream.ts）：
// REST（HttpClient）+ 4 条 WebSocket 数据流（traffic/memory/logs/connections）。
class MihomoApiClient final : public QObject {
  Q_OBJECT
public:
  MihomoApiClient(ConfigManager* config, LogManager* log, QObject* parent = nullptr);
  ~MihomoApiClient() override;

  void setServiceMode(bool service);
  void reset();                    // 断开全部流 + 清节流 pending

  void startStreams();
  void stopStreams();
  void restartLogsStream();
  void restartConnectionsStream();

  // ---- REST ----
  void fetchVersion(const std::function<void(const ControllerVersion&)>& onDone);
  void fetchProxies(const std::function<void(const std::vector<ProxyNode>&)>& onDone,
                    const std::function<void(const QString&)>& onError = {});
  void fetchGroups(const std::function<void(const std::vector<ProxyGroup>&)>& onDone,
                   const std::function<void(const QString&)>& onError = {});
  void fetchRules(const std::function<void(const std::vector<RuleItem>&)>& onDone,
                  const std::function<void(const QString&)>& onError = {});
  // 更新规则禁用状态。请求体遵循 Mihomo API：{"<rule-index>": true|false}。
  void setRuleDisabled(std::size_t index, bool disabled,
                       const std::function<void()>& onDone = {},
                       const std::function<void(const QString&)>& onError = {});
  void changeProxy(const QString& group, const QString& proxy,
                   const std::function<void(const ProxyGroup&)>& onDone = {},
                   const std::function<void(const QString&)>& onError = {});
  void unfixProxy(const QString& group,
                  const std::function<void(const ProxyGroup&)>& onDone = {},
                  const std::function<void(const QString&)>& onError = {});

signals:
  void trafficUpdated(const sparkle::core::TrafficStats& stats);                       // 节流 100ms
  void memoryUpdated(const sparkle::core::MemoryStats& stats);
  void connectionsUpdated(const std::vector<sparkle::core::ConnectionItem>& items);   // 节流 200ms
  void connectionsError(const QString& message);

private:
  QString endpoint() const;   // 直连=unix socket/pipe；service=host:port
  QString secret() const;     // service 模式 Bearer 凭据

  void onTrafficMessage(const QByteArray& payload);
  void onMemoryMessage(const QByteArray& payload);
  void onLogsMessage(const QByteArray& payload);
  void onConnectionsMessage(const QByteArray& payload);

  ConfigManager* config_;
  LogManager* log_;
  bool serviceMode_ = false;

  HttpClient http_;
  std::unique_ptr<LatestSender<TrafficStats>> trafficSender_;
  std::unique_ptr<LatestSender<std::vector<ConnectionItem>>> connectionsSender_;

  std::unique_ptr<WsClient> traffic_;
  std::unique_ptr<WsClient> memory_;
  std::unique_ptr<WsClient> logs_;
  std::unique_ptr<WsClient> connections_;
};

// Parse the controller's /proxies response independently of transport/UI code.
std::vector<ProxyNode> proxyNodesFromControllerJson(const nlohmann::json& response);
std::vector<ProxyGroup> proxyGroupsFromControllerJson(const nlohmann::json& response);
std::vector<RuleItem> rulesFromControllerJson(const nlohmann::json& response);
ProxyNode proxyNodeFromControllerJson(const QString& name, const nlohmann::json& detail);
ProxyGroup proxyGroupFromControllerJson(const QString& name, const nlohmann::json& detail);

}  // namespace sparkle::core
