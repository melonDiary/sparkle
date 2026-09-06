#pragma once

#include <QObject>
#include <memory>
#include <vector>

#include "models.h"

namespace sparkle::core {

// 日志管理（对应原 utils/log.ts）：spdlog 落盘 + 内存环形缓冲 + 广播信号。
class LogManager final : public QObject {
  Q_OBJECT
public:
  explicit LogManager(QObject* parent = nullptr);
  ~LogManager() override;

  void configure(bool saveLogs, std::size_t maxFileSizeMB, std::size_t maxEntries);

  void appendAppLog(const QString& line);          // 应用日志（写文件）
  void setMihomoLogSourceFromConsole(bool fromConsole); // 'out' vs 'ws'
  void publishMihomoLog(const LogEntry& entry);    // WebSocket 来源日志
  void publishMihomoLogLines(const QString& chunk); // WS /logs 块：按行解析并发布
  void appendRawCoreChunk(const QString& chunk, LogLevel fallbackLevel); // stdout 行缓冲+解析

  std::vector<LogEntry> cachedMihomoLogs() const;  // 日志页回放
  void clearCachedMihomoLogs();

signals:
  void mihomoLog(const sparkle::core::LogEntry& entry);

private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace sparkle::core