#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>

// 单实例守卫（对应原 bootstrap/single-instance.ts）。
//
// 用 QLocalServer 监听作为"唯一锁"：只有先成功 listen 的一方是主实例。
// 存活探测用一次真实的连接尝试，因此崩溃/SIGKILL 残留的旧 socket 文件不会像
// QSharedMemory 那样造成"永久误判已运行"——死进程不在监听，探测即失败，随后清理即可重启。
namespace sparkle::core {

class SingleInstance final : public QObject {
  Q_OBJECT
public:
  explicit SingleInstance(QObject* parent = nullptr);
  ~SingleInstance() override;

  bool tryAcquire();                 // 返回 true=主实例；false=已有实例（应转发后退）
  void forwardToPrimary(int argc, char** argv);

signals:
  // 二次启动 / deep link（主实例收到）
  void activatedFromSecondary(const QStringList& args);

private:
  void onNewConnection();

  QLocalServer server_;
  static constexpr const char* kSocketName = "sparkle_single_instance_comm";
};

}  // namespace sparkle::core