#include "theme_manager.h"

#include <QApplication>
#include <QFile>

namespace sparkle::ui {
namespace {

void applySheet(const QString& resourcePath) {
  QFile file(resourcePath);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
  }
}

}  // namespace

void ThemeManager::applyLight() { applySheet(QStringLiteral(":/themes/light.qss")); }

void ThemeManager::applyDark() { applySheet(QStringLiteral(":/themes/dark.qss")); }

void ThemeManager::applyName(const QString& name) {
  if (name == QLatin1String("dark")) {
    applyDark();
  } else {
    // TODO(phase 2): "system" 跟随系统；自定义主题从 dataDir/themes 读取
    applyLight();
  }
}

}  // namespace sparkle::ui