#pragma once

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QString>

namespace sparkle::core {

// 经过 MITM 规则链的 HTTP 响应。
// statusCode=0 表示尚未收到上游响应；脚本可将其修改为合成响应。
struct MITMResponse {
  int statusCode = 0;
  QString statusText;
  QHash<QString, QString> headers;
  QByteArray body;
};

}  // namespace sparkle::core

Q_DECLARE_METATYPE(sparkle::core::MITMResponse)
