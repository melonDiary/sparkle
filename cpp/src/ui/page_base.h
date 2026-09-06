#pragma once

#include <QWidget>

namespace sparkle::ui {

// 页面基类：统一刷新接口。
class PageBase : public QWidget {
  Q_OBJECT
public:
  explicit PageBase(QWidget* parent = nullptr) : QWidget(parent) {}
  virtual void refresh() {}
};

}  // namespace sparkle::ui