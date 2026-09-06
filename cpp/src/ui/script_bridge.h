#pragma once

#include <QObject>
#include <QString>

namespace sparkle::core {
class CoreManager;
class ScriptEngine;
}

namespace sparkle::ui {

// QML ↔ C++ ↔ JS 桥接层。QML 只调用本类，不直接依赖 QuickJS C API。
class ScriptBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)
public:
  explicit ScriptBridge(QObject* parent = nullptr);

  void setScriptEngine(sparkle::core::ScriptEngine* engine);
  bool available() const;

  Q_INVOKABLE bool startProxy();
  Q_INVOKABLE bool stopProxy();
  Q_INVOKABLE bool restartProxy();
  Q_INVOKABLE void call(const QString& functionName);

signals:
  void availableChanged();
  void scriptError(const QString& message);

private:
  sparkle::core::ScriptEngine* engine_ = nullptr;
  bool available_ = false;
};

}  // namespace sparkle::ui
