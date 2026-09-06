#pragma once

#include <QObject>
#include <QTimer>
#include <functional>
#include <utility>

namespace sparkle::core {

// 首尾最新值节流（对应原 utils/latest-sender.ts）：首值立即发射；窗口内只保留最新值；
// clear() 清待发值与定时器。T 需可默认构造 + 拷贝。
template <typename T>
class LatestSender {
public:
  LatestSender(int intervalMs, std::function<void(const T&)> emitFn, QObject* parent)
      : intervalMs_(intervalMs), emitFn_(std::move(emitFn)), timer_(parent) {
    timer_.setSingleShot(true);
    timer_.setInterval(intervalMs_);
    QObject::connect(&timer_, &QTimer::timeout, [this] { flush(); });
  }

  void send(const T& value) {
    if (!sentFirst_) {
      sentFirst_ = true;
      emitFn_(value);
      return;
    }
    pending_ = value;
    hasPending_ = true;
    if (!timer_.isActive()) timer_.start();
  }

  void clear() {
    timer_.stop();
    hasPending_ = false;
    sentFirst_ = false;
    pending_ = T{};
  }

private:
  void flush() {
    if (hasPending_) {
      hasPending_ = false;
      emitFn_(pending_);
    }
  }

  int intervalMs_;
  std::function<void(const T&)> emitFn_;
  QTimer timer_;
  bool sentFirst_ = false;
  bool hasPending_ = false;
  T pending_{};
};

}  // namespace sparkle::core