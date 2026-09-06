#pragma once

#include <QAbstractListModel>
#include <vector>

#include "models.h"

namespace sparkle::ui {

// 日志列表模型：环形缓冲 + 级别过滤（对应原 logs 页）。
class LogListModel final : public QAbstractListModel {
  Q_OBJECT
public:
  explicit LogListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;

  void append(const sparkle::core::LogEntry& entry);
  void setEntries(const std::vector<sparkle::core::LogEntry>& entries);
  void clear();
  void setMinimumLevel(sparkle::core::LogLevel minLevel);

private:
  void rebuildFiltered();

  std::vector<sparkle::core::LogEntry> entries_;
  std::vector<sparkle::core::LogEntry> filtered_;
  sparkle::core::LogLevel minLevel_ = sparkle::core::LogLevel::Debug;
  static constexpr std::size_t kMaxEntries = 2000;
};

}  // namespace sparkle::ui