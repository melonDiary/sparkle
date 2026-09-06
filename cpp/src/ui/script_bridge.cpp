#include "script_bridge.h"

#include "script_engine.h"

namespace sparkle::ui {

ScriptBridge::ScriptBridge(QObject* parent) : QObject(parent) {}

void ScriptBridge::setScriptEngine(sparkle::core::ScriptEngine* engine) {
  engine_ = engine;
  const bool next = engine_ && engine_->isInitialized();
  if (available_ == next) return;
  available_ = next;
  emit availableChanged();
}

bool ScriptBridge::available() const { return available_; }

bool ScriptBridge::startProxy() {
  if (!engine_) {
    emit scriptError(QStringLiteral("脚本引擎不可用"));
    return false;
  }
  try {
    engine_->invokeFunction("onStartProxy");
    return true;
  } catch (const std::exception& error) {
    emit scriptError(QString::fromUtf8(error.what()));
    return false;
  }
}

bool ScriptBridge::stopProxy() {
  if (!engine_) {
    emit scriptError(QStringLiteral("脚本引擎不可用"));
    return false;
  }
  try {
    engine_->invokeFunction("onStopProxy");
    return true;
  } catch (const std::exception& error) {
    emit scriptError(QString::fromUtf8(error.what()));
    return false;
  }
}

bool ScriptBridge::restartProxy() {
  if (!engine_) {
    emit scriptError(QStringLiteral("脚本引擎不可用"));
    return false;
  }
  try {
    engine_->invokeFunction("onRestartProxy");
    return true;
  } catch (const std::exception& error) {
    emit scriptError(QString::fromUtf8(error.what()));
    return false;
  }
}

void ScriptBridge::call(const QString& functionName) {
  if (!engine_) {
    emit scriptError(QStringLiteral("脚本引擎不可用"));
    return;
  }
  try {
    engine_->invokeFunction(functionName.toStdString());
  } catch (const std::exception& error) {
    emit scriptError(QString::fromUtf8(error.what()));
  }
}

}  // namespace sparkle::ui
