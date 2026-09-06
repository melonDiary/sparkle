#pragma once

#include "page_base.h"

namespace sparkle::ui {

// 设置页面（对应原 settings 页）。
class SettingsPage final : public PageBase {
  Q_OBJECT
public:
  explicit SettingsPage(QWidget* parent = nullptr);
  void refresh() override;
};

}  // namespace sparkle::ui