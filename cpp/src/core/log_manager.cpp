#include "log_manager.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <QDateTime>
#include <deque>

#include "paths.h"

namespace sparkle::core {
namespace {

constexpr std::size_t kCachedLogLimit = 2000;

LogLevel normalizeOutLogLevel(const QString& level, LogLevel fallback) {
  if (level == QLatin1String("error")) return LogLevel::Error;
  if (level == QLatin1String("warn") || level == QLatin1String("warning"))
    return LogLevel::Warning;
  if (level == QLatin1String("info")) return LogLevel::Info;
  if (level == QLatin1String("debug")) return LogLevel::Debug;
  return fallback;
}

// 按 logfmt 规则切分字段（key=value，value 可为带引号且含转义）。
// 形如：time="..." level=info msg="hello world"。
QStringList splitLogfmtFields(const QString& line) {
  QStringList fields;
  QChar quote = QChar::Null;
  bool escaped = false;
  QString current;
  for (const QChar& c : line) {
    if (escaped) {
      current += c;
      escaped = false;
      continue;
    }
    if (c == QLatin1Char('\\') && quote != QChar::Null) {
      current += c;   // 保留反斜杠，解析值阶段统一反转义
      escaped = true;
      continue;
    }
    if (quote != QChar::Null) {
      if (c == quote) quote = QChar::Null;
      current += c;
      continue;
    }
    if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
      quote = c;
      current += c;
      continue;
    }
    if (c == QLatin1Char(' ')) {
      if (!current.isEmpty()) fields << current;
      current.clear();
      continue;
    }
    current += c;
  }
  if (!current.isEmpty()) fields << current;
  return fields;
}

// 去除首尾引号并反转义常见转义序列。
QString unquoteLogfmtValue(QString value) {
  if (value.size() >= 2 &&
      ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')) ||
       (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')))) {
    value = value.mid(1, value.size() - 2);
  }
  value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
  value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
  value.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
  value.replace(QStringLiteral("\\t"), QStringLiteral("\t"));
  return value;
}

// 完整 logfmt 解析：time=... level=... msg=...（含引号/转义）。
LogEntry parseCoreLine(const QString& line, LogLevel fallback) {
  LogEntry entry;
  entry.level = fallback;
  entry.payload = line;
  entry.time = QDateTime::currentMSecsSinceEpoch();

  for (const QString& field : splitLogfmtFields(line)) {
    const int eq = field.indexOf(QLatin1Char('='));
    if (eq <= 0) continue;
    const QString key = field.left(eq);
    const QString value = unquoteLogfmtValue(field.mid(eq + 1));

    if (key == QLatin1String("level")) {
      entry.level = normalizeOutLogLevel(value, fallback);
    } else if (key == QLatin1String("msg")) {
      entry.payload = value;
    } else if (key == QLatin1String("time")) {
      const QDateTime t = QDateTime::fromString(value, Qt::ISODate);
      if (t.isValid()) entry.time = t.toMSecsSinceEpoch();
    }
  }
  return entry;
}

}  // namespace

struct LogManager::Impl {
  std::shared_ptr<spdlog::logger> app;
  std::shared_ptr<spdlog::logger> core;
  std::deque<LogEntry> buffer;
  std::uint64_t seq = 0;
  std::size_t maxEntries = kCachedLogLimit;
  bool fromConsole = true;
  QString lineBuffer;
};

LogManager::LogManager(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {
  configure(true, 20, kCachedLogLimit);
}

LogManager::~LogManager() {
  if (d_->app) spdlog::drop(d_->app->name());
  if (d_->core) spdlog::drop(d_->core->name());
}

void LogManager::configure(bool saveLogs, std::size_t maxFileSizeMB, std::size_t maxEntries) {
  d_->maxEntries = maxEntries;

  // 先释放可能存在的旧 logger（rotating_logger_mt 同名重注册会抛异常）。
  auto drop = [this] {
    if (d_->app) {
      spdlog::drop(d_->app->name());
      d_->app = nullptr;
    }
    if (d_->core) {
      spdlog::drop(d_->core->name());
      d_->core = nullptr;
    }
  };
  drop();

  if (!saveLogs) {
    // 不落盘：仅保留内存缓冲（原 saveLogs=false 语义）。
    return;
  }

  const auto maxBytes = static_cast<size_t>(maxFileSizeMB) * 1024 * 1024;
  try {
    d_->app = spdlog::rotating_logger_mt("sparkle_app", Paths::appLogPath().toStdString(),
                                         maxBytes, 1);
    d_->core = spdlog::rotating_logger_mt("sparkle_core", Paths::coreLogPath().toStdString(),
                                          maxBytes, 1);
  } catch (const spdlog::spdlog_ex&) {
    d_->app = nullptr;
    d_->core = nullptr;
  }
}

void LogManager::appendAppLog(const QString& line) {
  if (d_->app) d_->app->info(line.toStdString());
}

void LogManager::setMihomoLogSourceFromConsole(bool fromConsole) { d_->fromConsole = fromConsole; }

void LogManager::publishMihomoLog(const LogEntry& entry) {
  if (d_->fromConsole) return;
  if (d_->buffer.size() >= d_->maxEntries) d_->buffer.pop_front();
  d_->buffer.push_back(entry);
  emit mihomoLog(entry);
}

void LogManager::publishMihomoLogLines(const QString& chunk) {
  // 与 appendRawCoreChunk 共用 parseCoreLine，但走 WS 发布语义（fromConsole=false 才广播）。
  const QStringList lines = chunk.split(QLatin1Char('\n'));
  for (const QString& raw : lines) {
    if (raw.trimmed().isEmpty()) continue;
    LogEntry entry = parseCoreLine(raw, LogLevel::Info);
    entry.seq = ++d_->seq;
    publishMihomoLog(entry);
  }
}

void LogManager::appendRawCoreChunk(const QString& chunk, LogLevel fallbackLevel) {
  d_->lineBuffer += chunk;
  // 归一化换行
  d_->lineBuffer.replace(QLatin1String("\r\n"), QLatin1String("\n"));

  int index = 0;
  while ((index = d_->lineBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
    const QString line = d_->lineBuffer.left(index);
    d_->lineBuffer.remove(0, index + 1);
    if (line.trimmed().isEmpty()) continue;

    LogEntry entry = parseCoreLine(line, fallbackLevel);
    entry.seq = ++d_->seq;
    if (d_->buffer.size() >= d_->maxEntries) d_->buffer.pop_front();
    d_->buffer.push_back(entry);
    if (d_->fromConsole) {
      emit mihomoLog(entry);
    }
  }
  // 半行超限裁剪（原 directCoreLogLineLimit=16K）
  constexpr int kLineLimit = 16 * 1024;
  if (d_->lineBuffer.size() > kLineLimit) {
    d_->lineBuffer = d_->lineBuffer.right(kLineLimit);
  }
}

std::vector<LogEntry> LogManager::cachedMihomoLogs() const {
  return {d_->buffer.begin(), d_->buffer.end()};
}

void LogManager::clearCachedMihomoLogs() { d_->buffer.clear(); }

}  // namespace sparkle::core