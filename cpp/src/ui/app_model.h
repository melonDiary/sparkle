#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <vector>

#include "models.h"

namespace sparkle::core {
class CoreManager;
class MihomoApiClient;
class LogManager;
class SystemProxyManager;
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
  Q_PROPERTY(QVariantMap traffic READ traffic NOTIFY trafficChanged)
  Q_PROPERTY(QString controllerVersion READ controllerVersion NOTIFY controllerVersionChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
public:
  explicit AppModel(QObject* parent = nullptr);
  ~AppModel() override;

  void setCoreManager(sparkle::core::CoreManager* core);
  void setApiClient(sparkle::core::MihomoApiClient* api);
  void setLogManager(sparkle::core::LogManager* log);
  void setSystemProxyManager(sparkle::core::SystemProxyManager* manager);

  QString coreState() const;
  bool running() const;
  bool systemProxyEnabled() const;
  QStringList groupNames() const;
  QString selectedGroup() const;
  QVariantList proxies() const;
  QVariantList rules() const;
  QVariantList logs() const;
  QVariantMap traffic() const;
  QString controllerVersion() const;
  QString statusMessage() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void setRuleDisabled(qulonglong index, bool disabled);
  Q_INVOKABLE void setStatusMessage(const QString& message);
  Q_INVOKABLE void setSelectedGroup(const QString& group);
  Q_INVOKABLE void setSystemProxyEnabled(bool enabled);

signals:
  void coreStateChanged();
  void runningChanged();
  void systemProxyEnabledChanged();
  void groupNamesChanged();
  void selectedGroupChanged();
  void proxiesChanged();
  void rulesChanged();
  void logsChanged();
  void trafficChanged();
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
  void setControllerVersion(const sparkle::core::ControllerVersion& version);
  void connectCoreSignals();

  sparkle::core::CoreManager* core_ = nullptr;
  sparkle::core::MihomoApiClient* api_ = nullptr;
  sparkle::core::LogManager* log_ = nullptr;
  sparkle::core::SystemProxyManager* systemProxy_ = nullptr;
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
  QVariantMap traffic_;
  QString controllerVersion_;
  QString statusMessage_;
  std::vector<QMetaObject::Connection> connections_;
  std::vector<QMetaObject::Connection> apiConnections_;
  std::vector<QMetaObject::Connection> proxyConnections_;
};

}  // namespace sparkle::ui
