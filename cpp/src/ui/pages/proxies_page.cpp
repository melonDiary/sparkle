#include "proxies_page.h"

#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QListView>
#include <QVBoxLayout>

#include "mihomo_api_client.h"
#include "models/proxy_list_model.h"

namespace sparkle::ui {

ProxiesPage::ProxiesPage(QWidget* parent) : PageBase(parent) {
  auto* layout = new QVBoxLayout(this);

  auto* title = new QLabel(QStringLiteral("代理节点"), this);
  layout->addWidget(title);

  groupCombo_ = new QComboBox(this);
  groupCombo_->addItem(QStringLiteral("全部"));
  layout->addWidget(groupCombo_);

  model_ = new ProxyListModel(this);
  view_ = new QListView(this);
  view_->setModel(model_);
  layout->addWidget(view_);

  connect(groupCombo_, &QComboBox::currentIndexChanged, this,
          [this](int) { updateVisibleNodes(); });
}

void ProxiesPage::setNodes(const std::vector<sparkle::core::ProxyNode>& nodes) {
  nodes_ = nodes;
  updateVisibleNodes();
}

void ProxiesPage::setGroups(const std::vector<sparkle::core::ProxyGroup>& groups) {
  const QString previous = groupCombo_->currentText();
  groups_ = groups;

  QSignalBlocker blocker(groupCombo_);
  groupCombo_->clear();
  groupCombo_->addItem(QStringLiteral("全部"));
  for (const auto& group : groups_) {
    if (!group.hidden) groupCombo_->addItem(group.name);
  }

  const int restored = groupCombo_->findText(previous);
  groupCombo_->setCurrentIndex(restored >= 0 ? restored : 0);
  updateVisibleNodes();
}

void ProxiesPage::setApi(sparkle::core::MihomoApiClient* api) { api_ = api; }

void ProxiesPage::refresh() {
  if (!api_) return;

  api_->fetchProxies(
      [this](const std::vector<sparkle::core::ProxyNode>& nodes) { setNodes(nodes); },
      [this](const QString& message) { emit refreshError(message); });
  api_->fetchGroups(
      [this](const std::vector<sparkle::core::ProxyGroup>& groups) { setGroups(groups); },
      [this](const QString& message) { emit refreshError(message); });
}

void ProxiesPage::updateVisibleNodes() {
  if (!model_ || !groupCombo_) return;
  const QString selectedGroup = groupCombo_->currentText();
  if (selectedGroup == QStringLiteral("全部") || selectedGroup.isEmpty()) {
    model_->setNodes(nodes_);
    return;
  }

  for (const auto& group : groups_) {
    if (group.name != selectedGroup) continue;
    std::vector<sparkle::core::ProxyNode> visible;
    for (const auto& node : nodes_) {
      if (group.all.contains(node.name)) visible.push_back(node);
    }
    model_->setNodes(visible);
    return;
  }
  model_->setNodes({});
}

}  // namespace sparkle::ui