#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

#include "models.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace sparkle::core {

class ConfigManager;
class LogManager;

// 订阅管理：拉取机场订阅（Base64/明文节点列表）→ 解析为 ProxyNode 列表。
//
// 关键设计：
// - 异步拉取（QNetworkAccessManager + 信号回调），杜绝主线程嵌套事件循环；
// - 解析器对 BOM / 多余换行 / 非 UTF-8 / 畸形 Base64 / HTML 错误页（502 等）都做防御，
//   与"不崩溃、不死循环、识别并跳过 HTML"的审计要求对齐；
// - parseSubscription 为纯静态函数，便于单元测试。
class SubscriptionManager final : public QObject {
  Q_OBJECT
 public:
  SubscriptionManager(ConfigManager* config = nullptr, LogManager* log = nullptr,
                      QObject* parent = nullptr);
  ~SubscriptionManager() override;

  // 异步拉取并解析订阅。完成/失败经回调返回；同一时刻只保留最新一次请求。
  void fetch(const QString& url,
             const std::function<void(const std::vector<ProxyNode>&)>& onDone,
             const std::function<void(const QString&)>& onError = {});

  // 纯解析：订阅正文（可能为 Base64 编码，也可能为明文节点列表）→ 节点。
  // HTML/无效内容返回空列表（并可选地通过 errorMessage 输出原因）。
  static std::vector<ProxyNode> parseSubscription(const QByteArray& raw,
                                                  QString* errorMessage = nullptr);

 signals:
  void fetched(const std::vector<sparkle::core::ProxyNode>& nodes);
  void fetchFailed(const QString& message);

 private:
  QNetworkAccessManager* nam_ = nullptr;
  QNetworkReply* reply_ = nullptr;
  LogManager* log_ = nullptr;
};

}  // namespace sparkle::core