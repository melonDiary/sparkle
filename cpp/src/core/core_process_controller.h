#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <memory>

namespace sparkle::core {

// 一个 Mihomo 内核子进程的封装（对应原 core/process-control.ts）。
//
// 关键设计：
// - 用 std::unique_ptr<QProcess> 持有子进程，遵循 RAII：本对象析构时自动终止子进程，
//   绝不把 QProcess 交给 Qt 父对象（否则对象树与 unique_ptr 会重复 delete）。
// - 优雅终止升级：SIGINT → SIGTERM(3s) → SIGKILL(6s)；Windows 退回 terminate() → kill()。
// - "停止代际"（stopGeneration_）：每次 start()/stop() 都递增，使上一次 stop() 派生的
//   定时器失效，避免"重启后旧定时器误杀新进程"的竞态。
// - stdout/stderr 分离通道 + 按行缓冲，逐行以信号抛出。
class CoreProcessController final : public QObject {
  Q_OBJECT
public:
  explicit CoreProcessController(QObject* parent = nullptr);
  ~CoreProcessController() override;   // RAII：终止子进程

  void start(const QString& program, const QStringList& args, const QProcessEnvironment& env);
  void stop();                          // 优雅停止（异步升级）
  bool isRunning() const;
  qint64 processId() const;
  bool waitForStarted(int msecs);       // 同步等待 spawn（供 startCore 返回确定性结果）
  bool waitForFinished(int msecs);      // 同步等待（析构/收尾场景）

signals:
  void started();
  void finished(int exitCode, QProcess::ExitStatus status);
  void stdOutLine(const QString& line);
  void stdErrLine(const QString& line);
  void errorOccurred(QProcess::ProcessError error);

private:
  void onReadyReadOut();
  void onReadyReadErr();
  void emitLines(QString& buffer, const QByteArray& chunk, bool isError);
  void terminateAt(int level);          // 0=SIGINT/terminate, 1=SIGTERM/kill, 2=SIGKILL/kill

  std::unique_ptr<QProcess> process_;   // RAII：唯一拥有 QProcess
  QString outBuffer_;
  QString errBuffer_;
  int stopGeneration_ = 0;              // 停止代际，防误杀
};

}  // namespace sparkle::core