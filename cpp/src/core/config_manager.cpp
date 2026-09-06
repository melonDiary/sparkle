#include "config_manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QDebug>

#include "json_yaml.h"
#include "paths.h"
#include "script_engine.h"

namespace sparkle::core {
namespace {

using nlohmann::json;

json defaultAppConfig() {
  return json{
      {"core", "mihomo"},
      {"corePermissionMode", "elevated"},
      {"coreStartupMode", "log"},
      {"mode", "rule"},
      {"logLevel", "info"},
      {"realtimeLogLevel", "info"},
      {"saveLogs", true},
      {"maxLogFileSizeMB", 20},
      {"maxLogEntries", 2000},
      {"onlyActiveDevice", false},
      {"mitm_enabled", false},
      {"mitm_port", 8080},
      {"mitm_script_dir", ""},
      {"sysProxy",
       {{"enable", false},
        {"host", "127.0.0.1"},
        {"mode", "manual"},
        {"bypass", json::array()},
        {"settingMode", "exec"},
        {"guard", false},
        {"guardNotify", false}}},
  };
}

json itemsConfig() {
  return json{{"items", json::array()}};
}

json normalizeObject(json j) {
  return j.is_object() ? j : json::object();
}

json normalizeItems(json j) {
  if (!j.is_object()) return itemsConfig();
  if (!j.contains("items") || !j["items"].is_array()) j["items"] = json::array();
  return j;
}

// 深度合并（对应原 utils/merge.ts）
json deepMerge(json base, const json& patch) {
  if (!patch.is_object()) return patch;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object()) {
      base[it.key()] = deepMerge(base[it.key()], it.value());
    } else {
      base[it.key()] = it.value();
    }
  }
  return base;
}

QString currentProfileId(const ConfigManager& self);

SysProxyConfig sysProxyFromJson(const json& j) {
  SysProxyConfig c;
  if (!j.is_object()) return c;
  c.enable = j.value("enable", false);
  c.host = QString::fromStdString(j.value("host", "127.0.0.1"));
  c.mode = j.value("mode", "manual") == "auto" ? SysProxyMode::Auto : SysProxyMode::Manual;
  if (j.contains("bypass") && j["bypass"].is_array()) {
    for (const auto& b : j["bypass"]) c.bypass << QString::fromStdString(b.get<std::string>());
  }
  c.settingMode =
      j.value("settingMode", "exec") == "service" ? SysProxySettingMode::Service
                                                   : SysProxySettingMode::Exec;
  c.guard = j.value("guard", false);
  c.guardNotify = j.value("guardNotify", false);
  return c;
}

json sysProxyToJson(const SysProxyConfig& c) {
  json::array_t bypass;
  for (const auto& b : c.bypass) bypass.push_back(b.toStdString());
  return json{{"enable", c.enable},
              {"host", c.host.toStdString()},
              {"mode", c.mode == SysProxyMode::Auto ? "auto" : "manual"},
              {"bypass", std::move(bypass)},
              {"settingMode", c.settingMode == SysProxySettingMode::Service ? "service" : "exec"},
              {"guard", c.guard},
              {"guardNotify", c.guardNotify}};
}

// 端口是否可监听（用于 service 模式控制器端口校验）。
bool portAvailable(quint16 port) {
  QTcpServer server;
  return server.listen(QHostAddress::LocalHost, port);
}

// 选一个空闲回环端口（供 mihomo external-controller 监听）。
quint16 pickFreePort() {
  QTcpServer server;
  if (server.listen(QHostAddress::LocalHost, 0)) {
    return server.serverPort();
  }
  return 9090;   // 兜底（正常不应走到）
}

// 随机 secret（32 字节 hex，作 Bearer 凭据）。
QString randomControllerSecret() {
  QByteArray bytes(32, '\0');
  for (auto& b : bytes) b = static_cast<char>(QRandomGenerator::global()->bounded(256));
  return QString::fromLatin1(bytes.toHex());
}

}  // namespace

struct ConfigManager::Impl {
  YamlStore<json> appStore;
  YamlStore<json> controlledStore;
  YamlStore<json> profileStore;
  YamlStore<json> overrideStore;

  Impl()
      : appStore(Paths::appConfigPath(), [] { return defaultAppConfig(); }, normalizeObject, true),
        controlledStore(Paths::controlledMihomoConfigPath(), [] { return json::object(); },
                        normalizeObject, true),
        profileStore(Paths::profileConfigPath(), [] { return itemsConfig(); }, normalizeItems,
                     false),
        overrideStore(Paths::overrideConfigPath(), [] { return itemsConfig(); }, normalizeItems,
                      false) {}
};

ConfigManager::ConfigManager(QObject* parent) : QObject(parent), d_(std::make_unique<Impl>()) {}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::loadConfig(const std::string& path) {
  const QString configPath = QString::fromStdString(path);
  if (!QFileInfo::exists(configPath)) return false;

  const QString suffix = QFileInfo(configPath).suffix().toLower();
  nlohmann::json loaded;
  if (suffix == QLatin1String("js") || suffix == QLatin1String("mjs")) {
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    // config.js 使用 CommonJS 风格导出：module.exports = { ... }；同时兼容
    // 直接声明全局 config = { ... }。脚本仍运行在独立 QuickJS Runtime 中。
    ScriptEngine engine;
    if (!engine.initialize()) return false;
    const std::string source = file.readAll().toStdString();
    const std::string wrapped =
        "var module = { exports: {} }; var exports = module.exports;\n" + source +
        "\n;((typeof config !== 'undefined' && config !== module.exports) ? config : module.exports);";
    try {
      const std::string serialized = engine.evaluate(wrapped);
      loaded = nlohmann::json::parse(serialized.empty() ? "null" : serialized);
    } catch (const ScriptError& error) {
      qWarning().noquote() << "[ConfigManager] JS 配置执行失败:" << error.what();
      return false;
    } catch (const std::exception& error) {
      qWarning().noquote() << "[ConfigManager] JS 配置解析失败:" << error.what();
      return false;
    }
  } else {
    bool ok = false;
    loaded = loadYaml(configPath, &ok);
    if (!ok) return false;
  }

  if (!loaded.is_object()) return false;
  loadedConfig_ = loaded;
  loadedConfigPath_ = configPath;
  emit configChanged();
  emit reloadRequested();
  return true;
}

std::vector<ProxyNode> ConfigManager::getProxyNodes() const {
  std::vector<ProxyNode> result;
  if (!loadedConfig_.is_object() || !loadedConfig_.contains("proxies") ||
      !loadedConfig_["proxies"].is_array()) {
    return result;
  }
  for (const auto& value : loadedConfig_["proxies"]) {
    if (!value.is_object()) continue;
    ProxyNode node;
    node.name = QString::fromStdString(value.value("name", std::string()));
    node.typeName = QString::fromStdString(value.value("type", std::string()));
    node.type = proxyTypeFromString(node.typeName);
    node.server = QString::fromStdString(value.value("server", std::string()));
    node.port = value.value("port", 0);
    node.cipher = QString::fromStdString(value.value("cipher", std::string()));
    node.password = QString::fromStdString(value.value("password", std::string()));
    result.push_back(std::move(node));
  }
  return result;
}

nlohmann::json ConfigManager::appConfig(bool force) { return d_->appStore.get(force); }

bool ConfigManager::serviceMode() {
  return appConfig().value("corePermissionMode", std::string("elevated")) == "service";
}

bool ConfigManager::mitmEnabled() const {
  auto* self = const_cast<ConfigManager*>(this);
  return self->appConfig().value("mitm_enabled", false);
}

quint16 ConfigManager::mitmPort() const {
  auto* self = const_cast<ConfigManager*>(this);
  const int configured = self->appConfig().value("mitm_port", 8080);
  return static_cast<quint16>((configured >= 1 && configured <= 65535) ? configured : 8080);
}

QString ConfigManager::mitmScriptDir() const {
  auto* self = const_cast<ConfigManager*>(this);
  const std::string configured = self->appConfig().value("mitm_script_dir", std::string());
  if (!configured.empty()) return QString::fromStdString(configured);
  return QDir(Paths::dataDir()).filePath(QStringLiteral("mitm"));
}

ServiceControllerEndpoint ConfigManager::serviceControllerEndpoint() {
  ServiceControllerEndpoint out;
  if (!serviceMode()) return out;

  json app = appConfig();
  if (app.contains("serviceController") && app["serviceController"].is_object()) {
    const json& sc = app["serviceController"];
    out.host = QString::fromStdString(sc.value("host", std::string("127.0.0.1")));
    out.port = static_cast<quint16>(sc.value("port", 0));
    out.secret = QString::fromStdString(sc.value("secret", std::string()));
  }
  if (out.host.isEmpty()) out.host = QStringLiteral("127.0.0.1");

  // 端口缺失/被占用时重选空闲端口；secret 缺失时生成；变更后持久化（重连复用）。
  bool dirty = false;
  if (out.port == 0 || !portAvailable(out.port)) {
    out.port = pickFreePort();
    dirty = true;
  }
  if (out.secret.isEmpty()) {
    out.secret = randomControllerSecret();
    dirty = true;
  }
  if (dirty) {
    json patch;
    patch["serviceController"]["host"] = out.host.toStdString();
    patch["serviceController"]["port"] = out.port;
    patch["serviceController"]["secret"] = out.secret.toStdString();
    patchAppConfig(patch);
  }
  return out;
}

void ConfigManager::patchAppConfig(const nlohmann::json& patch) {
  const json merged = deepMerge(appConfig(), patch);
  d_->appStore.set(merged);
  emit appConfigChanged();
  if (patch.contains("mitm_enabled") || patch.contains("mitm_port") ||
      patch.contains("mitm_script_dir")) {
    emit mitmConfigChanged();
  }
}

nlohmann::json ConfigManager::controlledMihomoConfig(bool force) {
  return d_->controlledStore.get(force);
}

void ConfigManager::patchControlledMihomoConfig(const nlohmann::json& patch) {
  const json merged = deepMerge(controlledMihomoConfig(), patch);
  d_->controlledStore.set(merged);
  emit controlledMihomoConfigChanged();
  // 运行配置的重新生成发生在 RuntimeConfigFactory::generate()（由上层在收到
  // reloadRequested 后重启内核时调用），此处只负责持久化受控配置 —— 与
  // patchControledMihomoConfig 原语义一致（先落盘受控配置，再生成运行配置）。
  emit reloadRequested();
}

void ConfigManager::replaceControlledMihomoConfig(const nlohmann::json& config) {
  d_->controlledStore.set(config);
  emit controlledMihomoConfigChanged();
}

nlohmann::json ConfigManager::profileConfig(bool force) { return d_->profileStore.get(force); }

void ConfigManager::setProfileConfig(const nlohmann::json& config) {
  d_->profileStore.set(config);
  emit profileConfigChanged();
}

nlohmann::json ConfigManager::overrideConfig(bool force) { return d_->overrideStore.get(force); }

void ConfigManager::setOverrideConfig(const nlohmann::json& config) {
  d_->overrideStore.set(config);
  emit overrideConfigChanged();
}

namespace {
QString currentProfileId(const ConfigManager& self) {
  const json cfg = const_cast<ConfigManager&>(self).profileConfig();
  const std::string current = cfg.value("current", "default");
  return QString::fromStdString(current);
}
}  // namespace

QString ConfigManager::profileRawText(const QString& id) {
  const QString path = Paths::profilePath(id.isEmpty() ? QStringLiteral("default") : id);
  QFile file(path);
  if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString::fromUtf8(file.readAll());
  }
  // 缺省分支：返回 defaultProfile 模板（对齐原 getProfileStr：空 proxies/groups/rules）。
  return QStringLiteral("proxies: []\nproxy-groups: []\nrules: []\n");
}

void ConfigManager::setProfileRawText(const QString& id, const QString& content) {
  const QString key = id.isEmpty() ? QStringLiteral("default") : id;
  const QString path = Paths::profilePath(key);
  const QByteArray bytes = content.toUtf8();

  auto writeFile = [&]() -> bool {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(bytes) == bytes.size();
  };

  if (writeFile()) return;
  // 父目录可能尚不存在：确保目录后提权重试一次。
  QDir().mkpath(QFileInfo(path).absolutePath());
  if (writeFile()) return;

  // 重试后仍失败：广播失败通知（对齐原 saveFileStr / 写失败反馈语义）。
  emit profileWriteFailed(key);
}

std::vector<ProxyNode> ConfigManager::parseProxyNodes() {
  std::vector<ProxyNode> result;
  const json cfg = loadYaml(Paths::profilePath(currentProfileId(*this)));
  if (!cfg.contains("proxies") || !cfg["proxies"].is_array()) return result;
  for (const auto& p : cfg["proxies"]) {
    if (!p.is_object()) continue;
    ProxyNode node;
    node.name = QString::fromStdString(p.value("name", ""));
    node.typeName = QString::fromStdString(p.value("type", ""));
    node.type = proxyTypeFromString(node.typeName);
    node.server = QString::fromStdString(p.value("server", ""));
    node.port = p.value("port", 0);
    node.cipher = QString::fromStdString(p.value("cipher", p.value("ciphers", "")));
    node.password = QString::fromStdString(p.value("password", ""));
    result.push_back(std::move(node));
  }
  return result;
}

std::vector<ProxyGroup> ConfigManager::parseProxyGroups() {
  std::vector<ProxyGroup> result;
  const json cfg = loadYaml(Paths::profilePath(currentProfileId(*this)));
  if (!cfg.contains("proxy-groups") || !cfg["proxy-groups"].is_array()) return result;
  for (const auto& g : cfg["proxy-groups"]) {
    if (!g.is_object()) continue;
    ProxyGroup group;
    group.name = QString::fromStdString(g.value("name", ""));
    group.typeName = QString::fromStdString(g.value("type", ""));
    group.type = proxyTypeFromString(group.typeName);
    group.testUrl = QString::fromStdString(g.value("url", ""));
    result.push_back(std::move(group));
  }
  return result;
}

std::vector<RuleItem> ConfigManager::parseRules() {
  std::vector<RuleItem> result;
  const json cfg = loadYaml(Paths::profilePath(currentProfileId(*this)));
  if (!cfg.contains("rules") || !cfg["rules"].is_array()) return result;
  std::size_t index = 0;
  for (const auto& r : cfg["rules"]) {
    RuleItem rule;
    rule.index = index++;
    if (r.is_string()) {
      // 简式规则 "TYPE,payload[,proxy]"：payload 可能含逗号，仅按前两个逗号切分。
      const QString s = QString::fromStdString(r.get<std::string>());
      const int c1 = s.indexOf(QLatin1Char(','));
      rule.type = (c1 < 0) ? s : s.left(c1);
      if (c1 >= 0) {
        const int c2 = s.indexOf(QLatin1Char(','), c1 + 1);
        rule.payload = (c2 < 0) ? s.mid(c1 + 1) : s.mid(c1 + 1, c2 - (c1 + 1));
        rule.proxy = (c2 < 0) ? QString() : s.mid(c2 + 1);
      }
    } else if (r.is_object()) {
      // 对象式规则（新格式）：{type, payload, proxy/target, ...}
      rule.type = QString::fromStdString(r.value("type", ""));
      rule.payload = QString::fromStdString(r.value("payload", ""));
      rule.proxy = QString::fromStdString(r.value("proxy", r.value("target", "")));
    }
    result.push_back(std::move(rule));
  }
  return result;
}

SysProxyConfig ConfigManager::sysProxyConfig() {
  return sysProxyFromJson(appConfig()["sysProxy"]);
}

void ConfigManager::setSysProxyConfig(const SysProxyConfig& config) {
  json patch;
  patch["sysProxy"] = sysProxyToJson(config);
  patchAppConfig(patch);
}

}  // namespace sparkle::core