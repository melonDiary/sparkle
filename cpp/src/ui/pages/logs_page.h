#pragma once

#include "models.h"
#include "page_base.h"

class QListView;

namespace sparkle::core {
class LogManager;
}

namespace sparkle::ui {

class LogListModel;

// 日志页面（对应原 logs 页）：实时追加 + 级别过滤 + 清空。
class LogsPage final : public PageBase {
  Q_OBJECT
public:
  explicit LogsPage(QWidget* parent = nullptr);

  void appendLog(const sparkle::core::LogEntry& entry);
  void setLogManager(sparkle::core::LogManager* log);
  void refresh() override;

private:
  LogListModel* model_ = nullptr;
  QListView* view_ = nullptr;
  sparkle::core::LogManager* log_ = nullptr;
};

}  // namespace sparkle::ui