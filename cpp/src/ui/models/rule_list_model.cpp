#include "rule_list_model.h"

#include <QColor>

namespace sparkle::ui {

RuleListModel::RuleListModel(QObject* parent) : QAbstractListModel(parent) {}

int RuleListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(filtered_.size());
}

QVariant RuleListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(filtered_.size())) {
    return {};
  }

  const auto& rule = filtered_[static_cast<std::size_t>(index.row())];
  if (role == Qt::DisplayRole) {
    const QString target = rule.proxy.isEmpty() ? QStringLiteral("DIRECT") : rule.proxy;
    return QStringLiteral("%1 · %2 → %3").arg(rule.type, rule.payload, target);
  }
  if (role == Qt::ToolTipRole) {
    return QStringLiteral("#%1  %2\n命中：%3 · 未命中：%4")
        .arg(static_cast<qulonglong>(rule.index + 1))
        .arg(rule.disabled ? QStringLiteral("已禁用") : QStringLiteral("启用"))
        .arg(static_cast<qulonglong>(rule.hitCount))
        .arg(static_cast<qulonglong>(rule.missCount));
  }
  if (role == Qt::ForegroundRole && rule.disabled) {
    return QColor(0x7f849c);
  }
  return {};
}

void RuleListModel::setRules(const std::vector<sparkle::core::RuleItem>& rules) {
  rules_ = rules;
  rebuildFiltered();
}

void RuleListModel::setFilter(const QString& filter) {
  if (filter_ == filter) return;
  filter_ = filter;
  rebuildFiltered();
}

const sparkle::core::RuleItem* RuleListModel::ruleAt(const QModelIndex& index) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(filtered_.size())) {
    return nullptr;
  }
  return &filtered_[static_cast<std::size_t>(index.row())];
}

void RuleListModel::rebuildFiltered() {
  beginResetModel();
  filtered_.clear();
  const QString needle = filter_.trimmed();
  for (const auto& rule : rules_) {
    if (needle.isEmpty() || rule.type.contains(needle, Qt::CaseInsensitive) ||
        rule.payload.contains(needle, Qt::CaseInsensitive) ||
        rule.proxy.contains(needle, Qt::CaseInsensitive)) {
      filtered_.push_back(rule);
    }
  }
  endResetModel();
}

}  // namespace sparkle::ui
