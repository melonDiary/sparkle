#pragma once

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QString>

namespace sparkle::core {

// 经过 MITM 规则链的 HTTP 请求。body 当前限制为小体积文本/二进制的 QByteArray，
// 避免脚本一次性占用不可控内存；大请求会被拒绝并记录日志。
struct MITMRequest {
  QString method;
  QString url;
  QHash<QString, QString> headers;
  QByteArray body;
};

}  // namespace sparkle::core

Q_DECLARE_METATYPE(sparkle::core::MITMRequest)
