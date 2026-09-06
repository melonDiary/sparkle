#pragma once

#include <QAbstractListModel>
#include <vector>

#include "models.h"

namespace sparkle::ui {

// 代理节点列表模型（对应原 proxies 页）。
class ProxyListModel final : public QAbstractListModel {
  Q_OBJECT
public:
  explicit ProxyListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;

  void setNodes(const std::vector<sparkle::core::ProxyNode>& nodes);
  const sparkle::core::ProxyNode* nodeAt(const QModelIndex& index) const;

private:
  std::vector<sparkle::core::ProxyNode> nodes_;
};

}  // namespace sparkle::ui