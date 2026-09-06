#include "settings_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme_manager.h"

namespace sparkle::ui {

SettingsPage::SettingsPage(QWidget* parent) : PageBase(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("设置"), this));

  auto* themeRow = new QHBoxLayout();
  themeRow->addWidget(new QLabel(QStringLiteral("主题"), this));
  auto* light = new QPushButton(QStringLiteral("浅色"), this);
  auto* dark = new QPushButton(QStringLiteral("深色"), this);
  connect(light, &QPushButton::clicked, [] { ThemeManager::applyLight(); });
  connect(dark, &QPushButton::clicked, [] { ThemeManager::applyDark(); });
  themeRow->addWidget(light);
  themeRow->addWidget(dark);
  themeRow->addStretch();
  layout->addLayout(themeRow);

  layout->addWidget(new QLabel(QStringLiteral("（完整设置项 P1/P2 接入，见设计文档 §18）"), this));
  layout->addStretch();
}

void SettingsPage::refresh() { /* TODO(phase 1) */ }

}  // namespace sparkle::ui