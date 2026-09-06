#pragma once

#include <QMainWindow>

#include "models.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace sparkle::ui {

class LogsPage;
class ProxiesPage;
class RulesPage;

// 主窗口（对应原 App.tsx）：顶栏（启动/停止 + 模式切换 + 状态）+ 侧边导航 + 页面栈。
class MainWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  LogsPage* logsPage() const;
  ProxiesPage* proxiesPage() const;
  RulesPage* rulesPage() const;

signals:
  void startClicked();
  void stopClicked();
  void modeChanged(const QString& mode);

public slots:
  void setCoreState(sparkle::core::CoreState state);
  void setTraffic(const sparkle::core::TrafficStats& stats);

private:
  QListWidget* sidebar_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QPushButton* startButton_ = nullptr;
  QPushButton* stopButton_ = nullptr;
  QComboBox* modeCombo_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  LogsPage* logsPage_ = nullptr;
  ProxiesPage* proxiesPage_ = nullptr;
  RulesPage* rulesPage_ = nullptr;
};

}  // namespace sparkle::ui