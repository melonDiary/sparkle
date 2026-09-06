#pragma once

#include <QAbstractListModel>
#include <QString>
#include <vector>

#include "models.h"

namespace sparkle::ui {

// 规则列表模型：保存控制器返回的规则，并按 type/payload/proxy 做大小写不敏感筛选。
class RuleListModel final : public QAbstractListModel {
  Q_OBJECT
public:
  explicit RuleListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;

  void setRules(const std::vector<sparkle::core::RuleItem>& rules);
  void setFilter(const QString& filter);
  const sparkle::core::RuleItem* ruleAt(const QModelIndex& index) const;

private:
  void rebuildFiltered();

  std::vector<sparkle::core::RuleItem> rules_;
  std::vector<sparkle::core::RuleItem> filtered_;
  QString filter_;
};

}  // namespace sparkle::ui
