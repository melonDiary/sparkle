#include "log_list_model.h"

#include <QColor>

namespace sparkle::ui {

// 级别优先级：Debug=1...Error=4（数值越大越严重），过滤时显示 >= minLevel。
namespace {
int levelRank(sparkle::core::LogLevel level) {
  switch (level) {
    case sparkle::core::LogLevel::Silent: return 0;
    case sparkle::core::LogLevel::Debug: return 1;
    case sparkle::core::LogLevel::Info: return 2;
    case sparkle::core::LogLevel::Warning: return 3;
    case sparkle::core::LogLevel::Error: return 4;
  }
  return 2;
}
}  // namespace

LogListModel::LogListModel(QObject* parent) : QAbstractListModel(parent) {}

int LogListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(filtered_.size());
}

QVariant LogListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(filtered_.size())) {
    return {};
  }
  const auto& entry = filtered_[static_cast<std::size_t>(index.row())];
  if (role == Qt::DisplayRole) {
    return entry.payload;
  }
  if (role == Qt::ForegroundRole) {
    switch (entry.level) {
      case sparkle::core::LogLevel::Error: return QColor(0xf38ba8);
      case sparkle::core::LogLevel::Warning: return QColor(0xf9e2af);
      case sparkle::core::LogLevel::Debug: return QColor(0x7f849c);
      default: return QColor(0xcdd6f4);
    }
  }
  return {};
}

void LogListModel::append(const sparkle::core::LogEntry& entry) {
  entries_.push_back(entry);
  if (entries_.size() > kMaxEntries) {
    entries_.erase(entries_.begin());
  }
  rebuildFiltered();
}

void LogListModel::setEntries(const std::vector<sparkle::core::LogEntry>& entries) {
  entries_ = entries;
  if (entries_.size() > kMaxEntries) {
    const auto first = entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - kMaxEntries);
    entries_.erase(entries_.begin(), first);
  }
  rebuildFiltered();
}

void LogListModel::clear() {
  beginResetModel();
  entries_.clear();
  filtered_.clear();
  endResetModel();
}

void LogListModel::setMinimumLevel(sparkle::core::LogLevel minLevel) {
  minLevel_ = minLevel;
  rebuildFiltered();
}

void LogListModel::rebuildFiltered() {
  beginResetModel();
  filtered_.clear();
  const int minRank = levelRank(minLevel_);
  for (const auto& e : entries_) {
    if (levelRank(e.level) >= minRank) {
      filtered_.push_back(e);
    }
  }
  endResetModel();
}

}  // namespace sparkle::ui