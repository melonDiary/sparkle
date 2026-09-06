#include "runtime_config_factory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "config_manager.h"
#include "js_engine.h"
#include "json_yaml.h"
#include "paths.h"

namespace sparkle::core {
namespace {

using nlohmann::json;

json deepMergeJson(json base, const json& patch) {
  if (!patch.is_object()) return patch;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_object() && base.contains(it.key()) && base[it.key()].is_object()) {
      base[it.key()] = deepMergeJson(base[it.key()], it.value());
    } else {
      base[it.key()] = it.value();
    }
  }
  return base;
}

bool isTrue(const json& j) { return j.is_boolean() && j.get<bool>(); }
bool isFalse(const json& j) { return j.is_boolean() && !j.get<bool>(); }

bool isEmptyLike(const json& j) {
  return (j.is_array() && j.empty()) || (j.is_string() && j.get<std::string>().empty());
}

// 把 profile 的详细字段（数组去重优先/对象深合并/标量取 profile）回写进 controled。
json mergeDetailed(const json& controled, const json& profile) {
  json result = controled.is_object() ? controled : json::object();
  if (!profile.is_object()) return result;
  for (auto it = profile.begin(); it != profile.end(); ++it) {
    const json& pv = it.value();
    const bool hasCtl = result.contains(it.key()) && !result[it.key()].is_null();
    if (pv.is_array()) {
      json merged = pv;   // profile 优先
      if (hasCtl && result[it.key()].is_array()) {
        for (const auto& e : result[it.key()]) {
          if (std::find(merged.begin(), merged.end(), e) == merged.end()) merged.push_back(e);
        }
      }
      result[it.key()] = std::move(merged);
    } else if (pv.is_object() && hasCtl && result[it.key()].is_object()) {
      result[it.key()] = mergeDetailed(result[it.key()], pv);
    } else {
      result[it.key()] = pv;
    }
  }
  return result;
}

// —— clean*：对齐 factory.ts 的归一化（删默认/空值/不可用字段）——

void configureLanSettings(json& p) {
  if (p.contains("allow-lan") && isFalse(p["allow-lan"])) {
    p.erase("lan-allowed-ips");
    p.erase("lan-disallowed-ips");
    return;
  }
  if (!p.contains("allow-lan") || !isTrue(p["allow-lan"])) {
    p.erase("allow-lan");
    p.erase("lan-allowed-ips");
    p.erase("lan-disallowed-ips");
    return;
  }
  if (p.contains("lan-allowed-ips") && p["lan-allowed-ips"].is_array()) {
    if (p["lan-allowed-ips"].empty()) {
      p.erase("lan-allowed-ips");
    } else {
      bool hasLoopback = false;
      for (const auto& ip : p["lan-allowed-ips"]) {
        if (ip.is_string() && ip.get<std::string>().rfind("127.0.0.1/", 0) == 0) hasLoopback = true;
      }
      if (!hasLoopback) p["lan-allowed-ips"].push_back("127.0.0.1/8");
    }
  }
  if (p.contains("lan-disallowed-ips") && p["lan-disallowed-ips"].is_array() &&
      p["lan-disallowed-ips"].empty()) {
    p.erase("lan-disallowed-ips");
  }
}

void cleanBooleanConfigs(json& p) {
  if (!p.contains("ipv6") || !isFalse(p["ipv6"])) p.erase("ipv6");
  static const char* kBool[] = {"unified-delay", "tcp-concurrent", "geodata-mode",
                                "geo-auto-update", "disable-keep-alive"};
  for (const char* k : kBool) {
    if (p.contains(k) && !isTrue(p[k])) p.erase(k);
  }
  if (!p.contains("profile") || !p["profile"].is_object()) {
    p.erase("profile");
    return;
  }
  const bool sel = p["profile"].contains("store-selected") && isTrue(p["profile"]["store-selected"]);
  const bool fake = p["profile"].contains("store-fake-ip") && isTrue(p["profile"]["store-fake-ip"]);
  if (!sel && !fake) {
    p.erase("profile");
    return;
  }
  if (!sel) p["profile"].erase("store-selected");
  if (!fake) p["profile"].erase("store-fake-ip");
}

void cleanNumberConfigs(json& p) {
  static const char* kNum[] = {"port",     "socks-port",     "redir-port",   "tproxy-port",
                               "mixed-port", "keep-alive-idle", "keep-alive-interval"};
  for (const char* k : kNum) {
    if (p.contains(k) && p[k].is_number() && p[k] == 0) p.erase(k);
  }
}

void cleanStringConfigs(json& p) {
  if (p.contains("mode") && p["mode"] == "rule") p.erase("mode");
  static const char* kStr[] = {"interface-name", "secret", "global-client-fingerprint"};
  for (const char* k : kStr) {
    if (p.contains(k) && p[k] == "") p.erase(k);
  }
  if (p.contains("external-controller") && p["external-controller"] == "") {
    p.erase("external-controller");
    p.erase("external-ui");
    p.erase("external-ui-url");
    p.erase("external-controller-cors");
  } else if (p.contains("external-ui") && p["external-ui"] == "") {
    p.erase("external-ui");
    p.erase("external-ui-url");
  }
}

void cleanAuthenticationConfig(json& p) {
  if (p.contains("authentication") && p["authentication"].is_array() &&
      p["authentication"].empty()) {
    p.erase("authentication");
    p.erase("skip-auth-prefixes");
  }
}

void cleanTunConfig(json& p) {
  if (!p.contains("tun") || !p["tun"].is_object()) return;
  json& t = p["tun"];
  if (!t.contains("enable") || !isTrue(t["enable"])) {
    p.erase("tun");
    return;
  }
  if (!t.contains("auto-route") || !isFalse(t["auto-route"])) t.erase("auto-route");
  if (!t.contains("auto-detect-interface") || !isFalse(t["auto-detect-interface"]))
    t.erase("auto-detect-interface");
  static const char* kTun[] = {"auto-redirect", "strict-route", "disable-icmp-forwarding"};
  for (const char* k : kTun) {
    if (t.contains(k) && !isTrue(t[k])) t.erase(k);
  }
  if (t.contains("device") && t["device"] == "") t.erase("device");
#if defined(Q_OS_MACOS)
  if (t.contains("device") && t["device"].is_string() &&
      t["device"].get<std::string>().rfind("utun", 0) != 0) {
    t.erase("device");
  }
#endif
  if (t.contains("dns-hijack") && t["dns-hijack"].is_array() && t["dns-hijack"].empty())
    t.erase("dns-hijack");
  if (t.contains("route-exclude-address") && t["route-exclude-address"].is_array() &&
      t["route-exclude-address"].empty())
    t.erase("route-exclude-address");
}

void cleanDnsConfig(json& p, bool controlDns) {
  if (!controlDns) return;
  if (!p.contains("dns") || !p["dns"].is_object()) return;
  json& d = p["dns"];
  if (!d.contains("enable") || !isTrue(d["enable"])) {
    p.erase("dns");
    return;
  }
  static const char* kArr[] = {"fake-ip-range",   "fake-ip-range6",   "fake-ip-filter",
                               "proxy-server-nameserver", "direct-nameserver", "nameserver"};
  for (const char* k : kArr) {
    if (d.contains(k) && isEmptyLike(d[k])) d.erase(k);
  }
  const bool rrFalse = d.contains("respect-rules") && isFalse(d["respect-rules"]);
  const bool psnEmpty = d.contains("proxy-server-nameserver") && isEmptyLike(d["proxy-server-nameserver"]);
  if (rrFalse || psnEmpty) d.erase("respect-rules");
  if (d.contains("nameserver-policy") && d["nameserver-policy"].is_object() &&
      d["nameserver-policy"].empty())
    d.erase("nameserver-policy");
  if (d.contains("proxy-server-nameserver-policy") && d["proxy-server-nameserver-policy"].is_object() &&
      d["proxy-server-nameserver-policy"].empty())
    d.erase("proxy-server-nameserver-policy");
  d.erase("fallback");
  d.erase("fallback-filter");
}

void cleanSnifferConfig(json& p, bool controlSniff) {
  if (!controlSniff) return;
  if (!p.contains("sniffer") || !p["sniffer"].is_object()) return;
  if (!p["sniffer"].contains("enable") || !isTrue(p["sniffer"]["enable"])) p.erase("sniffer");
}

void cleanProxyConfigs(json& p) {
  static const char* kArr[] = {"proxies", "proxy-groups", "rules"};
  for (const char* k : kArr) {
    if (p.contains(k) && p[k].is_array() && p[k].empty()) p.erase(k);
  }
  static const char* kObj[] = {"proxy-providers", "rule-providers"};
  for (const char* k : kObj) {
    if (!p.contains(k)) continue;
    const json& v = p[k];
    if (v.is_null() || (v.is_object() && v.empty())) p.erase(k);
  }
}

void cleanProfile(json& p, bool controlDns, bool controlSniff) {
  if (!p.contains("log-level") || (p["log-level"] != "info" && p["log-level"] != "debug")) {
    p["log-level"] = "info";
  }
  configureLanSettings(p);
  cleanBooleanConfigs(p);
  cleanNumberConfigs(p);
  cleanStringConfigs(p);
  cleanAuthenticationConfig(p);
  cleanTunConfig(p);
  cleanDnsConfig(p, controlDns);
  cleanSnifferConfig(p, controlSniff);
  cleanProxyConfigs(p);
}

// merge 后把 UI 受控字段（mode/tun.enable/dns.enable/sniffer.enable）恢复回 profile，
// 避免受控覆盖/覆写把 UI 开关覆盖成与受控面板不一致的值。
void restoreUiControlledFields(json& profile, const json& uiControl) {
  if (uiControl.contains("mode") && !uiControl["mode"].is_null()) profile["mode"] = uiControl["mode"];
  if (uiControl.contains("tunEnable") && !uiControl["tunEnable"].is_null()) {
    if (!profile.contains("tun") || !profile["tun"].is_object()) profile["tun"] = json::object();
    profile["tun"]["enable"] = uiControl["tunEnable"];
  }
  if (uiControl.contains("dnsEnable") && !uiControl["dnsEnable"].is_null()) {
    if (!profile.contains("dns") || !profile["dns"].is_object()) profile["dns"] = json::object();
    profile["dns"]["enable"] = uiControl["dnsEnable"];
  }
  if (uiControl.contains("snifferEnable") && !uiControl["snifferEnable"].is_null()) {
    if (!profile.contains("sniffer") || !profile["sniffer"].is_object()) profile["sniffer"] = json::object();
    profile["sniffer"]["enable"] = uiControl["snifferEnable"];
  }
}

// 把最终 profile 的 tun/dns/sniffer 详细字段回写进 controled，使 UI 反映覆写的详细设置。
void syncControlled(json& controled, const json& profile, bool controlDns, bool controlSniff) {
  if (profile.contains("tun") && profile["tun"].is_object()) {
    controled["tun"] = mergeDetailed(controled.value("tun", json::object()), profile["tun"]);
  } else if (!(controled.contains("tun") && controled["tun"].is_object() &&
               isTrue(controled["tun"].value("enable", json(false))))) {
    json tun = controled.value("tun", json::object());
    tun["enable"] = false;
    controled["tun"] = std::move(tun);
  }
  if (controlDns) {
    if (profile.contains("dns") && profile["dns"].is_object()) {
      controled["dns"] = mergeDetailed(controled.value("dns", json::object()), profile["dns"]);
    } else if (!(controled.contains("dns") && controled["dns"].is_object() &&
                 isTrue(controled["dns"].value("enable", json(false))))) {
      json dns = controled.value("dns", json::object());
      dns["enable"] = false;
      controled["dns"] = std::move(dns);
    }
  }
  if (controlSniff) {
    if (profile.contains("sniffer") && profile["sniffer"].is_object()) {
      controled["sniffer"] = mergeDetailed(controled.value("sniffer", json::object()), profile["sniffer"]);
    } else if (!(controled.contains("sniffer") && controled["sniffer"].is_object() &&
                 isTrue(controled["sniffer"].value("enable", json(false))))) {
      json sniffer = controled.value("sniffer", json::object());
      sniffer["enable"] = false;
      controled["sniffer"] = std::move(sniffer);
    }
  }
}

// 读取文本文件内容（override .js 脚本）。
QString readFileText(const QString& path) {
  QFile f(path);
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QString::fromUtf8(f.readAll());
  }
  return QString();
}

// yaml 覆写合并（js 覆写走 QuickJS）。
json applyOverrides(json profile, ConfigManager& cfg, const QString& current) {
  const json overrideCfg = cfg.overrideConfig();
  const json profileCfg = cfg.profileConfig();

  std::set<std::string> active;   // 生效的 override id
  if (overrideCfg.contains("items") && overrideCfg["items"].is_array()) {
    for (const auto& it : overrideCfg["items"]) {
      if (it.is_object() && it.value("global", false)) {
        const std::string id = it.value("id", std::string());
        if (!id.empty()) active.insert(id);
      }
    }
  }
  if (profileCfg.contains("items") && profileCfg["items"].is_array()) {
    for (const auto& it : profileCfg["items"]) {
      if (!it.is_object()) continue;
      if (it.value("id", std::string()) != current.toStdString()) continue;
      for (const char* key : {"override", "overrideIds"}) {
        if (it.contains(key) && it[key].is_array()) {
          for (const auto& oid : it[key]) {
            if (oid.is_string()) active.insert(oid.get<std::string>());
          }
        }
      }
    }
  }
  if (overrideCfg.contains("items") && overrideCfg["items"].is_array()) {
    for (const auto& it : overrideCfg["items"]) {
      if (!it.is_object()) continue;
      const std::string id = it.value("id", std::string());
      if (id.empty() || !active.count(id)) continue;
      const std::string ext = it.value("ext", std::string("js"));
      if (ext == "yaml") {
        const json patch = loadYaml(Paths::overridePath(QString::fromStdString(id), "yaml"));
        if (patch.is_object()) profile = deepMergeJson(profile, patch);
      } else if (ext == "js") {
        // JS 覆写：QuickJS 执行 main(profile)。失败时保留原 profile，错误已写入 override 日志。
        const QString script = readFileText(Paths::overridePath(QString::fromStdString(id), "js"));
        if (!script.trimmed().isEmpty()) {
          const JsOverrideResult r = runOverrideScript(
              script, profile, Paths::overridePath(QString::fromStdString(id), "log"));
          if (r.ok) profile = r.profile;
        }
      }
    }
  }
  return profile;
}

// diffWorkDir：把 work/ 下的 geo db/dat 拷到 profile 专属工作目录（缺则补）。
void prepareProfileWorkDir(const QString& current) {
  const QString target = Paths::profileWorkDir(current);
  const QString source = Paths::workDir();
  QDir().mkpath(target);
  static const QRegularExpression dbRe(QStringLiteral(".+\\.(db|dat)$"),
                                        QRegularExpression::CaseInsensitiveOption);
  // (db/dat) 复制；源不存在或目标已存在则跳过。
  QDir srcDir(source);
  const QStringList files = srcDir.entryList(QDir::Files);
  for (const QString& name : files) {
    if (!dbRe.match(name).hasMatch()) continue;
    const QString srcPath = srcDir.filePath(name);
    const QString dstPath = QDir(target).filePath(name);
    if (!QFileInfo::exists(dstPath) && QFileInfo::exists(srcPath)) {
      QFile::copy(srcPath, dstPath);
    }
  }
}

QString currentId(ConfigManager& config) {
  const json cfg = config.profileConfig();
  return QString::fromStdString(cfg.value("current", "default"));
}

}  // namespace

RuntimeConfigFactory::RuntimeConfigFactory(ConfigManager* config, QObject* parent)
    : QObject(parent), config_(config) {}

void RuntimeConfigFactory::generate() {
  const QString workId = currentId(*config_);
  const json appConfig = config_->appConfig();
  json controlled = config_->controlledMihomoConfig();

  const bool diffWorkDir = appConfig.value("diffWorkDir", false);
  const bool controlDns = appConfig.value("controlDns", true);
  const bool controlSniff = appConfig.value("controlSniff", true);

  // 1) 基配置：当前订阅原文（缺省则 defaultProfile 模板）。
  json profile = parseYamlStr(config_->profileRawText(workId).toStdString());
  if (!profile.is_object()) profile = json::object();

  // 2) 覆写（yaml 合并；js 覆写 P3）。
  profile = applyOverrides(std::move(profile), *config_, workId);

  // 3) 合并前捕获 UI 受控字段，merge 后恢复（避免被受控覆盖/覆写冲掉）。
  json uiControl;
  uiControl["mode"] = controlled.value("mode", json(nullptr));
  uiControl["tunEnable"] =
      controlled.contains("tun") && controlled["tun"].is_object()
          ? controlled["tun"].value("enable", json(nullptr))
          : json(nullptr);
  uiControl["dnsEnable"] =
      controlled.contains("dns") && controlled["dns"].is_object()
          ? controlled["dns"].value("enable", json(nullptr))
          : json(nullptr);
  uiControl["snifferEnable"] =
      controlled.contains("sniffer") && controlled["sniffer"].is_object()
          ? controlled["sniffer"].value("enable", json(nullptr))
          : json(nullptr);

  // 4) 受控配置合并（controlDns/controlSniff 关闭时剥离 dns/hosts/sniffer）。
  json configToMerge = controlled;
  if (!controlDns) {
    configToMerge.erase("dns");
    configToMerge.erase("hosts");
  }
  if (!controlSniff) configToMerge.erase("sniffer");
  profile = deepMergeJson(profile, configToMerge);

  // 5) 恢复 UI 受控字段 → clean → 回写受控配置。
  restoreUiControlledFields(profile, uiControl);
  cleanProfile(profile, controlDns, controlSniff);
  syncControlled(controlled, profile, controlDns, controlSniff);
  config_->replaceControlledMihomoConfig(controlled);

  // 5.5) service 模式：控制器改走 mihomo 原生 TCP external-controller + secret
  //（此时不再用 -ext-ctl-unix/-pipe 参数），端口/凭据自动生成并持久化、重连复用。
  if (config_->serviceMode()) {
    const ServiceControllerEndpoint sc = config_->serviceControllerEndpoint();
    profile["external-controller"] = sc.host.toStdString() + ":" + std::to_string(sc.port);
    profile["secret"] = sc.secret.toStdString();
  }

  // 6) 落盘运行配置（diffWorkDir 决定目录；并准备专属工作目录）。
  runtime_ = profile;
  if (diffWorkDir) {
    prepareProfileWorkDir(workId);
    generatedPath_ = Paths::workConfigPath(workId);
  } else {
    generatedPath_ = Paths::workConfigPath(QStringLiteral("work"));
  }
  saveYaml(generatedPath_, runtime_);
  emit generated();
}

QString RuntimeConfigFactory::generatedPath() const { return generatedPath_; }

nlohmann::json RuntimeConfigFactory::runtimeConfig() const { return runtime_; }

QString RuntimeConfigFactory::runtimeConfigStr() const {
  return QString::fromStdString(runtime_.dump(2));
}

}  // namespace sparkle::core