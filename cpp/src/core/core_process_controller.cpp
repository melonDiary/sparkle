#include "core_process_controller.h"

#include <QTimer>

#if !defined(Q_OS_WIN)
#include <csignal>
#include <sys/types.h>
#endif

namespace sparkle::core {

CoreProcessController::CoreProcessController(QObject* parent) : QObject(parent) {
  // 唯一所有权：QProcess 不设 Qt 父对象，由 unique_ptr 独占管理，避免双重释放。
  process_ = std::make_unique<QProcess>();
  process_->setProcessChannelMode(QProcess::SeparateChannels);

  connect(process_.get(), &QProcess::started, this, &CoreProcessController::started);
  connect(process_.get(), &QProcess::finished, this, &CoreProcessController::finished);
  connect(process_.get(), &QProcess::errorOccurred, this, &CoreProcessController::errorOccurred);
  connect(process_.get(), &QProcess::readyReadStandardOutput, this,
          &CoreProcessController::onReadyReadOut);
  connect(process_.get(), &QProcess::readyReadStandardError, this,
          &CoreProcessController::onReadyReadErr);
}

CoreProcessController::~CoreProcessController() {
  // RAII：若子进程仍在运行，优雅终止——先 terminate（POSIX 等价 SIGTERM），
  // 2 秒内未退出则 kill（SIGKILL），确保不会留下孤儿进程。
  if (process_ && process_->state() != QProcess::NotRunning) {
    process_->terminate();
    if (!process_->waitForFinished(2000)) {
      process_->kill();
      process_->waitForFinished(1000);
    }
  }
}

void CoreProcessController::start(const QString& program, const QStringList& args,
                                  const QProcessEnvironment& env) {
  // 防御：已在运行则拒绝重复启动（上层应先进 stop）。
  if (process_ && process_->state() == QProcess::Running) return;

  ++stopGeneration_;   // 使上一次 stop() 派生的升级定时器失效
  process_->setProcessEnvironment(env);
  process_->start(program, args);
}

void CoreProcessController::stop() {
  if (process_->state() == QProcess::NotRunning) return;

  // 记录本次停止的代际，升级定时器只在代际仍一致时才生效。
  const int generation = ++stopGeneration_;

  // Starting 窗口：spawn 尚未完成，没有可信号的 pid，直接 kill。
  if (process_->state() == QProcess::Starting) {
    process_->kill();
    return;
  }

  terminateAt(0);  // 第一级：SIGINT（mihomo 捕获后优雅收尾）
  QTimer::singleShot(3000, this, [this, generation] {
    if (generation != stopGeneration_ || !isRunning()) return;
    terminateAt(1);  // 第二级：SIGTERM
  });
  QTimer::singleShot(6000, this, [this, generation] {
    if (generation != stopGeneration_ || !isRunning()) return;
    terminateAt(2);  // 第三级：SIGKILL（强杀兜底）
  });
}

bool CoreProcessController::isRunning() const {
  return process_ && process_->state() == QProcess::Running;
}

qint64 CoreProcessController::processId() const {
  return process_ ? process_->processId() : 0;
}

bool CoreProcessController::waitForStarted(int msecs) {
  return process_ ? process_->waitForStarted(msecs) : false;
}

bool CoreProcessController::waitForFinished(int msecs) {
  return process_ ? process_->waitForFinished(msecs) : true;
}

void CoreProcessController::terminateAt(int level) {
  // 防误杀：仅当有真实子进程 pid 才发信号（pid==0 会信号整个进程组！）。
  if (process_->state() != QProcess::Running || processId() <= 0) return;

#if defined(Q_OS_WIN)
  // Windows 无 POSIX 信号：第一级用 terminate()（WM_CLOSE），后续直接 kill()。
  if (level == 0) {
    process_->terminate();
  } else {
    process_->kill();
  }
#else
  const int sig = (level == 0) ? SIGINT : (level == 1) ? SIGTERM : SIGKILL;
  ::kill(static_cast<pid_t>(process_->processId()), sig);
#endif
}

void CoreProcessController::onReadyReadOut() {
  emitLines(outBuffer_, process_->readAllStandardOutput(), false);
}

void CoreProcessController::onReadyReadErr() {
  emitLines(errBuffer_, process_->readAllStandardError(), true);
}

void CoreProcessController::emitLines(QString& buffer, const QByteArray& chunk, bool isError) {
  buffer += QString::fromUtf8(chunk);
  buffer.replace(QLatin1String("\r\n"), QLatin1String("\n"));

  int index = 0;
  while ((index = buffer.indexOf(QLatin1Char('\n'))) >= 0) {
    const QString line = buffer.left(index);
    buffer.remove(0, index + 1);
    if (isError) {
      emit stdErrLine(line);
    } else {
      emit stdOutLine(line);
    }
  }
}

}  // namespace sparkle::core