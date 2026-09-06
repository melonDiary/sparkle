#include "rules_page.h"

#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QVBoxLayout>

#include "mihomo_api_client.h"
#include "models/rule_list_model.h"

namespace sparkle::ui {

RulesPage::RulesPage(QWidget* parent) : PageBase(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("分流规则"), this));

  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText(QStringLiteral("筛选过滤"));
  layout->addWidget(filter_);

  model_ = new RuleListModel(this);
  view_ = new QListView(this);
  view_->setModel(model_);
  layout->addWidget(view_);

  connect(filter_, &QLineEdit::textChanged, model_, &RuleListModel::setFilter);
}

void RulesPage::setApi(sparkle::core::MihomoApiClient* api) { api_ = api; }

void RulesPage::setRules(const std::vector<sparkle::core::RuleItem>& rules) {
  model_->setRules(rules);
}

void RulesPage::refresh() {
  if (!api_) return;
  api_->fetchRules(
      [this](const std::vector<sparkle::core::RuleItem>& rules) { setRules(rules); },
      [this](const QString& message) { emit refreshError(message); });
}

}  // namespace sparkle::ui
