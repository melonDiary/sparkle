#include "proxy_list_model.h"

namespace sparkle::ui {

ProxyListModel::ProxyListModel(QObject* parent) : QAbstractListModel(parent) {}

int ProxyListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(nodes_.size());
}

QVariant ProxyListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(nodes_.size())) {
    return {};
  }
  const auto& node = nodes_[static_cast<std::size_t>(index.row())];
  if (role == Qt::DisplayRole) {
    const QString delay = node.delay >= 0 ? QStringLiteral(" · %1ms").arg(node.delay)
                                          : QString();
    return node.name + delay;
  }
  if (role == Qt::ToolTipRole) {
    return QStringLiteral("[%1] %2").arg(sparkle::core::toString(node.type), node.server);
  }
  return {};
}

const sparkle::core::ProxyNode* ProxyListModel::nodeAt(const QModelIndex& index) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(nodes_.size())) {
    return nullptr;
  }
  return &nodes_[static_cast<std::size_t>(index.row())];
}

void ProxyListModel::setNodes(const std::vector<sparkle::core::ProxyNode>& nodes) {
  beginResetModel();
  nodes_ = nodes;
  endResetModel();
}

}  // namespace sparkle::ui