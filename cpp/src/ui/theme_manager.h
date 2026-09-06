#pragma once

#include <QString>

namespace sparkle::ui {

// 主题管理（对应原 resolve/theme.ts）：加载 QSS 并应用。骨架仅内置深/浅色。
class ThemeManager {
public:
  static void applyLight();
  static void applyDark();
  static void applyName(const QString& name); // "light" | "dark" | "system"

  ThemeManager() = delete;
};

}  // namespace sparkle::ui