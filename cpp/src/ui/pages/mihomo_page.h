#pragma once

#include "page_base.h"

namespace sparkle::ui {

// 内核页面（对应原 mihomo 页）。
class MihomoPage final : public PageBase {
  Q_OBJECT
public:
  explicit MihomoPage(QWidget* parent = nullptr);
  void refresh() override;
};

}  // namespace sparkle::ui