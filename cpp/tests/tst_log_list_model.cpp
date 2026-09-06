#include "models/log_list_model.h"

#include <QtTest>

using namespace sparkle::core;
using sparkle::ui::LogListModel;

class TstLogListModel : public QObject {
  Q_OBJECT

private slots:
  void replaysEntriesInOrder() {
    LogListModel model;
    const std::vector<LogEntry> entries = {
        LogEntry{1, LogLevel::Info, QStringLiteral("first"), 10},
        LogEntry{2, LogLevel::Error, QStringLiteral("second"), 20},
    };

    model.setEntries(entries);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("first"));
    QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toString(), QStringLiteral("second"));
  }

  void filtersReplayedEntriesWithoutChangingOrder() {
    LogListModel model;
    model.setEntries({
        LogEntry{1, LogLevel::Debug, QStringLiteral("debug"), 0},
        LogEntry{2, LogLevel::Info, QStringLiteral("info"), 0},
        LogEntry{3, LogLevel::Warning, QStringLiteral("warning"), 0},
        LogEntry{4, LogLevel::Error, QStringLiteral("error"), 0},
    });

    model.setMinimumLevel(LogLevel::Warning);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("warning"));
    QCOMPARE(model.data(model.index(1, 0), Qt::DisplayRole).toString(), QStringLiteral("error"));
  }

  void capsReplayedEntriesAtTwoThousand() {
    std::vector<LogEntry> entries;
    entries.reserve(2001);
    for (int i = 0; i < 2001; ++i) {
      entries.push_back(LogEntry{static_cast<std::uint64_t>(i), LogLevel::Info,
                                 QString::number(i), 0});
    }

    LogListModel model;
    model.setEntries(entries);

    QCOMPARE(model.rowCount(), 2000);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(model.index(1999, 0), Qt::DisplayRole).toString(), QStringLiteral("2000"));
  }
};

QTEST_GUILESS_MAIN(TstLogListModel)
#include "tst_log_list_model.moc"