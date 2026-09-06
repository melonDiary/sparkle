#pragma once

#include <QListWidget>

namespace sparkle::ui {

// 侧边导航（对应原 App.tsx 的 sider）。
class Sidebar final : public QListWidget {
  Q_OBJECT
public:
  explicit Sidebar(QWidget* parent = nullptr);
};

}  // namespace sparkle::ui