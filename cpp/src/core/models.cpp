#include "models.h"

namespace sparkle::core {
namespace {

struct ProxyTypeEntry {
  ProxyType type;
  const char* name;
};

constexpr ProxyTypeEntry kProxyTypes[] = {
    {ProxyType::Direct, "Direct"},
    {ProxyType::Reject, "Reject"},
    {ProxyType::RejectDrop, "RejectDrop"},
    {ProxyType::Compatible, "Compatible"},
    {ProxyType::Pass, "Pass"},
    {ProxyType::Dns, "Dns"},
    {ProxyType::Relay, "Relay"},
    {ProxyType::Selector, "Selector"},
    {ProxyType::Fallback, "Fallback"},
    {ProxyType::UrlTest, "URLTest"},
    {ProxyType::LoadBalance, "LoadBalance"},
    {ProxyType::Shadowsocks, "Shadowsocks"},
    {ProxyType::ShadowsocksR, "ShadowsocksR"},
    {ProxyType::Snell, "Snell"},
    {ProxyType::Socks5, "Socks5"},
    {ProxyType::Http, "Http"},
    {ProxyType::Vmess, "Vmess"},
    {ProxyType::Vless, "Vless"},
    {ProxyType::Trojan, "Trojan"},
    {ProxyType::Hysteria, "Hysteria"},
    {ProxyType::Hysteria2, "Hysteria2"},
    {ProxyType::WireGuard, "WireGuard"},
    {ProxyType::Tuic, "Tuic"},
    {ProxyType::Ssh, "Ssh"},
    {ProxyType::Mieru, "Mieru"},
    {ProxyType::AnyTls, "AnyTLS"},
    {ProxyType::Sudoku, "Sudoku"},
    {ProxyType::Masque, "Masque"},
};

}  // namespace

void registerCoreMetatypes() {
  qRegisterMetaType<TrafficStats>("sparkle::core::TrafficStats");
  qRegisterMetaType<MemoryStats>("sparkle::core::MemoryStats");
  qRegisterMetaType<DelaySample>("sparkle::core::DelaySample");
  qRegisterMetaType<ProxyNode>("sparkle::core::ProxyNode");
  qRegisterMetaType<ProxyGroup>("sparkle::core::ProxyGroup");
  qRegisterMetaType<LogEntry>("sparkle::core::LogEntry");
  qRegisterMetaType<ConnectionItem>("sparkle::core::ConnectionItem");
  qRegisterMetaType<RuleItem>("sparkle::core::RuleItem");
  qRegisterMetaType<ControllerVersion>("sparkle::core::ControllerVersion");
  qRegisterMetaType<CoreState>("sparkle::core::CoreState");
}

QString toString(OutboundMode mode) {
  switch (mode) {
    case OutboundMode::Rule: return QStringLiteral("rule");
    case OutboundMode::Global: return QStringLiteral("global");
    case OutboundMode::Direct: return QStringLiteral("direct");
  }
  return QStringLiteral("rule");
}

QString toString(LogLevel level) {
  switch (level) {
    case LogLevel::Silent: return QStringLiteral("silent");
    case LogLevel::Error: return QStringLiteral("error");
    case LogLevel::Warning: return QStringLiteral("warning");
    case LogLevel::Info: return QStringLiteral("info");
    case LogLevel::Debug: return QStringLiteral("debug");
  }
  return QStringLiteral("info");
}

QString toString(CoreState state) {
  switch (state) {
    case CoreState::Stopped: return QStringLiteral("stopped");
    case CoreState::Starting: return QStringLiteral("starting");
    case CoreState::Running: return QStringLiteral("running");
    case CoreState::Stopping: return QStringLiteral("stopping");
    case CoreState::Failed: return QStringLiteral("failed");
  }
  return QStringLiteral("stopped");
}

QString toString(ProxyType type) {
  for (const auto& e : kProxyTypes) {
    if (e.type == type) return QString::fromLatin1(e.name);
  }
  return QStringLiteral("Unknown");
}

ProxyType proxyTypeFromString(const QString& name) {
  for (const auto& e : kProxyTypes) {
    if (name == QLatin1String(e.name)) return e.type;
  }
  return ProxyType::Unknown;
}

LogLevel logLevelFromString(const QString& name) {
  if (name == QLatin1String("silent")) return LogLevel::Silent;
  if (name == QLatin1String("error")) return LogLevel::Error;
  if (name == QLatin1String("warning") || name == QLatin1String("warn")) return LogLevel::Warning;
  if (name == QLatin1String("info")) return LogLevel::Info;
  if (name == QLatin1String("debug")) return LogLevel::Debug;
  return LogLevel::Info;
}

OutboundMode outboundModeFromString(const QString& name) {
  if (name == QLatin1String("global")) return OutboundMode::Global;
  if (name == QLatin1String("direct")) return OutboundMode::Direct;
  return OutboundMode::Rule;
}

}  // namespace sparkle::core