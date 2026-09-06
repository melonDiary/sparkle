#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

// 纯数据模型（POD 风格）。字符串/时间字段统一用 Qt 类型以贴合信号槽与视图模型。
// 与设计文档 §6 对应；时间用 epoch 毫秒（qint64）。

namespace sparkle::core {

// ===== 枚举 =====

enum class OutboundMode { Rule, Global, Direct };
enum class LogLevel { Silent, Error, Warning, Info, Debug };
enum class CoreState { Stopped, Starting, Running, Stopping, Failed };
enum class CoreKind { Mihomo, MihomoAlpha, System };
enum class SysProxyMode { Auto, Manual };
enum class SysProxySettingMode { Exec, Service };

enum class ProxyType {
  Direct, Reject, RejectDrop, Compatible, Pass, Dns, Relay,
  Selector, Fallback, UrlTest, LoadBalance,
  Shadowsocks, ShadowsocksR, Snell, Socks5, Http, Vmess, Vless, Trojan,
  Hysteria, Hysteria2, WireGuard, Tuic, Ssh, Mieru, AnyTls, Sudoku, Masque,
  Unknown
};

// ===== 基础量 =====

using Bytes = std::uint64_t;
using TimestampMs = std::int64_t;

// ===== 流量 / 内存 =====

struct TrafficStats {
  Bytes upload = 0;     // 字节/秒
  Bytes download = 0;   // 字节/秒
};

struct MemoryStats {
  Bytes inUse = 0;
  Bytes osLimit = 0;
};

struct DelaySample {
  TimestampMs time = 0;
  int delay = -1;       // -1 = 无数据/超期
};

// ===== 代理节点 =====

struct ProxyNode {
  QString name;
  ProxyType type = ProxyType::Unknown;
  QString typeName;     // 原始 type 字符串（前向兼容）
  bool alive = false;
  int delay = -1;       // -1 = 未测
  QString providerName;
  std::vector<DelaySample> history;

  // 静态配置字段（来自 profile YAML，用于节点增删改）
  QString server;
  int port = 0;
  QString cipher;       // 加密方式（Shadowsocks 等）
  QString password;
};

// ===== 代理组 =====

struct ProxyGroup {
  QString name;
  ProxyType type = ProxyType::Unknown;
  bool alive = false;
  QString typeName;     // 原始 type 字符串（Selector/URLTest/...）
  QString now;          // 当前选中节点名
  QStringList all;      // 成员名列表
  bool hidden = false;
  bool fixed = false;
  QString testUrl;
};

// ===== 日志条目 =====

struct LogEntry {
  std::uint64_t seq = 0;
  LogLevel level = LogLevel::Info;
  QString payload;
  TimestampMs time = 0;
};

// ===== 连接 =====

struct ConnectionItem {
  QString id;
  QString network;      // tcp/udp
  QString type;
  QString sourceIp, destinationIp;
  QString sourcePort, destinationPort;
  QString host, process, processPath;
  QStringList chains;
  QString rule, rulePayload;
  Bytes upload = 0, download = 0;
  TimestampMs start = 0;
};

// ===== 规则 =====

struct RuleItem {
  std::size_t index = 0;
  QString type, payload, proxy;
  std::size_t size = 0;
  bool disabled = false;
  std::uint64_t hitCount = 0;
  std::uint64_t missCount = 0;
};

struct ControllerVersion {
  QString version;
  bool meta = false;
};

// ===== 配置结构体 =====

struct SysProxyConfig {
  bool enable = false;
  QString host;                                    // 默认 127.0.0.1
  SysProxyMode mode = SysProxyMode::Manual;        // manual | auto
  QStringList bypass;
  SysProxySettingMode settingMode = SysProxySettingMode::Exec; // exec | service
  bool guard = false;
  bool guardNotify = false;
};

struct ProfileItem {
  QString id;
  QString type = QStringLiteral("local");          // local | remote
  QString name;
  QString url;                                     // remote
  QString fingerprint;                             // remote
  QString ua;                                      // remote
  QString file;                                    // local
  bool verify = false;
  int interval = 0;
  QString home;
  std::int64_t updated = 0;
  QStringList overrideIds;
  bool useProxy = false;
  QString ageRecipient;
  QString ageIdentity;
  bool substore = false;
  bool locked = false;
  bool autoUpdate = true;
};

struct ProfileConfig {
  QString current;                                 // 空 = 无当前
  std::vector<ProfileItem> items;
};

struct OverrideItem {
  QString id;
  QString type = QStringLiteral("local");          // local | remote
  QString ext = QStringLiteral("js");              // js | yaml
  QString name;
  bool global = false;
  std::int64_t updated = 0;
  QString url;
  QString file;
  QString fingerprint;
};

struct OverrideConfig {
  std::vector<OverrideItem> items;
};

// ===== 字符串转换与元类型注册 =====

void registerCoreMetatypes();

QString toString(OutboundMode mode);
QString toString(LogLevel level);
QString toString(CoreState state);
QString toString(ProxyType type);

ProxyType proxyTypeFromString(const QString& name);
LogLevel logLevelFromString(const QString& name);
OutboundMode outboundModeFromString(const QString& name);

}  // namespace sparkle::core

Q_DECLARE_METATYPE(sparkle::core::TrafficStats)
Q_DECLARE_METATYPE(sparkle::core::MemoryStats)
Q_DECLARE_METATYPE(sparkle::core::DelaySample)
Q_DECLARE_METATYPE(sparkle::core::ProxyNode)
Q_DECLARE_METATYPE(sparkle::core::ProxyGroup)
Q_DECLARE_METATYPE(sparkle::core::LogEntry)
Q_DECLARE_METATYPE(sparkle::core::ConnectionItem)
Q_DECLARE_METATYPE(sparkle::core::RuleItem)
Q_DECLARE_METATYPE(sparkle::core::ControllerVersion)
Q_DECLARE_METATYPE(sparkle::core::CoreState)