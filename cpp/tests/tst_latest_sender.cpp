#include "latest_sender.h"

#include <QtTest>

#include <vector>

// 测试首尾最新值节流的确定性行为（首值立即发射、窗口内只保留最新、clear 重置）。
class TstLatestSender : public QObject {
  Q_OBJECT

private slots:
  void firstValueEmitsImmediately() {
    std::vector<int> received;
    sparkle::core::LatestSender<int> sender(
        100, [&](const int& v) { received.push_back(v); }, this);

    sender.send(1);
    QCOMPARE(received.size(), 1);
    QCOMPARE(received[0], 1);
  }

  void clearResetsFirstFlag() {
    std::vector<int> received;
    sparkle::core::LatestSender<int> sender(
        100, [&](const int& v) { received.push_back(v); }, this);

    sender.send(1);  // 立即
    sender.send(2);  // 挂起（不发射，等待定时器）
    sender.clear();  // 重置
    sender.send(3);  // 再次作为首值立即发射

    QCOMPARE(received.size(), 2);
    QCOMPARE(received[0], 1);
    QCOMPARE(received[1], 3);
  }
};

QTEST_GUILESS_MAIN(TstLatestSender)
#include "tst_latest_sender.moc"