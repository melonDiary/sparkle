#include "logs_page.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

#include "log_manager.h"
#include "models/log_list_model.h"

namespace sparkle::ui {

LogsPage::LogsPage(QWidget* parent) : PageBase(parent) {
  auto* layout = new QVBoxLayout(this);

  auto* top = new QHBoxLayout();
  top->addWidget(new QLabel(QStringLiteral("日志"), this));

  auto* levelCombo = new QComboBox(this);
  levelCombo->addItem(QStringLiteral("Debug"), static_cast<int>(sparkle::core::LogLevel::Debug));
  levelCombo->addItem(QStringLiteral("Info"), static_cast<int>(sparkle::core::LogLevel::Info));
  levelCombo->addItem(QStringLiteral("Warning"),
                      static_cast<int>(sparkle::core::LogLevel::Warning));
  levelCombo->addItem(QStringLiteral("Error"), static_cast<int>(sparkle::core::LogLevel::Error));
  top->addWidget(levelCombo);

  auto* clearButton = new QPushButton(QStringLiteral("清空"), this);
  model_ = new LogListModel(this);
  view_ = new QListView(this);
  view_->setModel(model_);

  connect(levelCombo, &QComboBox::currentIndexChanged, this, [this, levelCombo](int index) {
    const auto level = static_cast<sparkle::core::LogLevel>(levelCombo->itemData(index).toInt());
    model_->setMinimumLevel(level);
  });
  connect(clearButton, &QPushButton::clicked, this, [this] { model_->clear(); });

  top->addWidget(clearButton);
  layout->addLayout(top);
  layout->addWidget(view_);
}

void LogsPage::appendLog(const sparkle::core::LogEntry& entry) { model_->append(entry); }

void LogsPage::setLogManager(sparkle::core::LogManager* log) { log_ = log; }

void LogsPage::refresh() {
  if (!log_) return;
  model_->setEntries(log_->cachedMihomoLogs());
}

}  // namespace sparkle::ui