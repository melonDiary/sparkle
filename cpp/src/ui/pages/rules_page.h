#pragma once

#include <vector>

#include "models.h"
#include "page_base.h"

class QLineEdit;
class QListView;

namespace sparkle::core {
class MihomoApiClient;
}

namespace sparkle::ui {

class RuleListModel;

// 规则页面（对应原 rules 页）：实时读取控制器规则并支持关键字筛选。
class RulesPage final : public PageBase {
  Q_OBJECT
public:
  explicit RulesPage(QWidget* parent = nullptr);

  void setApi(sparkle::core::MihomoApiClient* api);
  void setRules(const std::vector<sparkle::core::RuleItem>& rules);
  void refresh() override;

signals:
  void refreshError(const QString& message);

private:
  RuleListModel* model_ = nullptr;
  QListView* view_ = nullptr;
  QLineEdit* filter_ = nullptr;
  sparkle::core::MihomoApiClient* api_ = nullptr;
};

}  // namespace sparkle::ui
