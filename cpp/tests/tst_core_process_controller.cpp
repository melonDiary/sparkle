#include "core_process_controller.h"

#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QtTest>

// 验证内核进程封装的核心行为：启动/状态查询、优雅停止、stdout 逐行捕获、边界条件。
class TstCoreProcess : public QObject {
  Q_OBJECT

private slots:
  // 需求 1/3/5：启动 → isRunning() → 优雅停止 → finished → 状态复位。
  void lifecycleAndGracefulStop() {
    sparkle::core::CoreProcessController ctl;
    QSignalSpy finishedSpy(&ctl, &sparkle::core::CoreProcessController::finished);

    ctl.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 30")},
              QProcessEnvironment::systemEnvironment());

    QTRY_VERIFY_WITH_TIMEOUT(ctl.isRunning(), 5000);
    QVERIFY(ctl.processId() > 0);

    ctl.stop();   // SIGINT → sleep 默认处理直接退出；异常时升级 SIGTERM/SIGKILL
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 8000);
    QVERIFY(!ctl.isRunning());
  }

  // 需求 2：stdout 按行缓冲，逐行经 stdOutLine 信号发出。
  void captureStdoutLines() {
    sparkle::core::CoreProcessController ctl;
    QSignalSpy outSpy(&ctl, &sparkle::core::CoreProcessController::stdOutLine);

    ctl.start(QStringLiteral("/bin/sh"),
              {QStringLiteral("-c"), QStringLiteral("printf 'alpha\\nbeta\\ngamma\\n'")},
              QProcessEnvironment::systemEnvironment());

    QTRY_COMPARE_WITH_TIMEOUT(outSpy.count(), 3, 5000);
    QCOMPARE(outSpy.at(0).at(0).toString(), QStringLiteral("alpha"));
    QCOMPARE(outSpy.at(1).at(0).toString(), QStringLiteral("beta"));
    QCOMPARE(outSpy.at(2).at(0).toString(), QStringLiteral("gamma"));
  }

  // 边界：stop() 对未启动的控制器应安全空操作。
  void stopWithoutStart() {
    sparkle::core::CoreProcessController ctl;
    QVERIFY(!ctl.isRunning());
    QCOMPARE(ctl.processId(), qint64(0));
    ctl.stop();   // 不应崩溃
    QVERIFY(!ctl.isRunning());
  }

  // 边界：重复 start() 应被拒绝。
  void doubleStart() {
    sparkle::core::CoreProcessController ctl;
    ctl.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 30")},
              QProcessEnvironment::systemEnvironment());
    QTRY_VERIFY_WITH_TIMEOUT(ctl.isRunning(), 3000);
    const qint64 firstPid = ctl.processId();

    // 第二次 start 应被忽略（防御性，不重启）。
    ctl.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 1")},
              QProcessEnvironment::systemEnvironment());
    QThread::msleep(200);
    QCOMPARE(ctl.processId(), firstPid);   // pid 不变
    ctl.stop();
    QTRY_COMPARE_WITH_TIMEOUT(ctl.isRunning(), false, 8000);
  }

  // 边界：析构时自动终止子进程（RAII）——创建控制器、启动进程、销毁控制器。
  void destructorKillsOrphan() {
    qint64 pid = 0;
    {
      sparkle::core::CoreProcessController ctl;
      ctl.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("sleep 60")},
                QProcessEnvironment::systemEnvironment());
      QTRY_VERIFY_WITH_TIMEOUT(ctl.isRunning(), 3000);
      pid = ctl.processId();
      QVERIFY(pid > 0);
    }
    // ctl 已析构：给子进程 2s 退出；若仍存活则测试失败。
    QThread::msleep(2000);
    QProcess probe;
    probe.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
                 QStringLiteral("kill -0 %1 2>/dev/null && echo alive || echo dead").arg(pid)});
    probe.waitForFinished(3000);
    QCOMPARE(QString::fromUtf8(probe.readAllStandardOutput()).trimmed(), QStringLiteral("dead"));
  }

  // stderr 逐行捕获。
  void captureStderrLines() {
    sparkle::core::CoreProcessController ctl;
    QSignalSpy errSpy(&ctl, &sparkle::core::CoreProcessController::stdErrLine);

    ctl.start(QStringLiteral("/bin/sh"),
              {QStringLiteral("-c"), QStringLiteral("echo err1 >&2; echo err2 >&2")},
              QProcessEnvironment::systemEnvironment());

    QTRY_COMPARE_WITH_TIMEOUT(errSpy.count(), 2, 5000);
    QCOMPARE(errSpy.at(0).at(0).toString(), QStringLiteral("err1"));
    QCOMPARE(errSpy.at(1).at(0).toString(), QStringLiteral("err2"));
  }
};

QTEST_GUILESS_MAIN(TstCoreProcess)
#include "tst_core_process_controller.moc"