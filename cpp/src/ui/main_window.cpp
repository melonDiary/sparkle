#include "main_window.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "page_base.h"
#include "pages/logs_page.h"
#include "pages/mihomo_page.h"
#include "pages/proxies_page.h"
#include "pages/rules_page.h"
#include "pages/settings_page.h"
#include "theme_manager.h"

namespace sparkle::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("Sparkle"));
  resize(1000, 680);
  ThemeManager::applyDark();

  auto* central = new QWidget(this);
  auto* rootLayout = new QVBoxLayout(central);

  // 顶栏
  auto* topBar = new QHBoxLayout();
  startButton_ = new QPushButton(QStringLiteral("启动"), central);
  stopButton_ = new QPushButton(QStringLiteral("停止"), central);
  stopButton_->setEnabled(false);
  modeCombo_ = new QComboBox(central);
  modeCombo_->addItem(QStringLiteral("rule"));
  modeCombo_->addItem(QStringLiteral("global"));
  modeCombo_->addItem(QStringLiteral("direct"));
  statusLabel_ = new QLabel(QStringLiteral("状态：已停止"), central);
  topBar->addWidget(startButton_);
  topBar->addWidget(stopButton_);
  topBar->addSpacing(12);
  topBar->addWidget(new QLabel(QStringLiteral("模式"), central));
  topBar->addWidget(modeCombo_);
  topBar->addStretch();
  topBar->addWidget(statusLabel_);
  rootLayout->addLayout(topBar);

  // 内容：侧边栏 + 页面栈
  auto* content = new QHBoxLayout();
  sidebar_ = new QListWidget(central);
  sidebar_->addItems({QStringLiteral("概况"), QStringLiteral("代理"), QStringLiteral("规则"),
                      QStringLiteral("日志"), QStringLiteral("设置"), QStringLiteral("内核")});
  sidebar_->setFixedWidth(180);

  stack_ = new QStackedWidget(central);

  auto* overview = new PageBase(stack_);
  overview->setLayout(new QVBoxLayout());
  overview->layout()->addWidget(new QLabel(QStringLiteral("概况（骨架占位）"), overview));

  proxiesPage_ = new ProxiesPage(stack_);
  stack_->addWidget(overview);                 // idx 0
  stack_->addWidget(proxiesPage_);             // idx 1 代理
  rulesPage_ = new RulesPage(stack_);
  stack_->addWidget(rulesPage_);               // idx 2 规则
  logsPage_ = new LogsPage(stack_);
  stack_->addWidget(logsPage_);                // idx 3
  stack_->addWidget(new SettingsPage(stack_)); // idx 4
  stack_->addWidget(new MihomoPage(stack_));   // idx 5

  content->addWidget(sidebar_);
  content->addWidget(stack_, 1);
  rootLayout->addLayout(content);

  setCentralWidget(central);

  connect(sidebar_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
  sidebar_->setCurrentRow(1); // 默认落在代理页

  connect(startButton_, &QPushButton::clicked, this, &MainWindow::startClicked);
  connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopClicked);
  connect(modeCombo_, &QComboBox::currentTextChanged, this, &MainWindow::modeChanged);
}

MainWindow::~MainWindow() = default;

LogsPage* MainWindow::logsPage() const { return logsPage_; }

ProxiesPage* MainWindow::proxiesPage() const { return proxiesPage_; }

RulesPage* MainWindow::rulesPage() const { return rulesPage_; }

void MainWindow::setCoreState(sparkle::core::CoreState state) {
  const bool running = state == sparkle::core::CoreState::Running ||
                       state == sparkle::core::CoreState::Starting;
  startButton_->setEnabled(!running);
  stopButton_->setEnabled(running);
  statusLabel_->setText(QStringLiteral("状态：") + sparkle::core::toString(state));
}

void MainWindow::setTraffic(const sparkle::core::TrafficStats& stats) {
  statusLabel_->setText(QStringLiteral("↑ %1 MB/s · ↓ %2 MB/s · 状态运行中")
                            .arg(static_cast<double>(stats.upload) / 1024.0 / 1024.0, 0, 'f', 2)
                            .arg(static_cast<double>(stats.download) / 1024.0 / 1024.0, 0, 'f',
                                 2));
}

}  // namespace sparkle::ui