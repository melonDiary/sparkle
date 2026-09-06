#pragma once

#include <QWidget>
#include <vector>

#include "models.h"
#include "page_base.h"

class QComboBox;
class QListView;

namespace sparkle::core {
class MihomoApiClient;
}

namespace sparkle::ui {

class ProxyListModel;

// 代理页面（对应原 proxies 页）。
class ProxiesPage final : public PageBase {
  Q_OBJECT
public:
  explicit ProxiesPage(QWidget* parent = nullptr);

  void setNodes(const std::vector<sparkle::core::ProxyNode>& nodes);
  void setGroups(const std::vector<sparkle::core::ProxyGroup>& groups);
  void setApi(sparkle::core::MihomoApiClient* api);
  void refresh() override;

signals:
  void refreshError(const QString& message);

private:
  void updateVisibleNodes();

  ProxyListModel* model_ = nullptr;
  QListView* view_ = nullptr;
  QComboBox* groupCombo_ = nullptr;
  sparkle::core::MihomoApiClient* api_ = nullptr;
  std::vector<sparkle::core::ProxyNode> nodes_;
  std::vector<sparkle::core::ProxyGroup> groups_;
};

}  // namespace sparkle::ui