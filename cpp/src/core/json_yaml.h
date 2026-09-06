#pragma once

#include <yaml-cpp/yaml.h>

#include <nlohmann/json.hpp>
#include <QFile>
#include <QString>

// JSON <-> YAML 桥接与文件读写。受控/机器管理配置用 YAML 持久化，运行时用 nlohmann::json。

namespace sparkle::core {

inline nlohmann::json yamlToJson(const YAML::Node& node) {
  switch (node.Type()) {
    case YAML::NodeType::Undefined:
    case YAML::NodeType::Null:
      return nullptr;
    case YAML::NodeType::Scalar: {
      // 按 bool → 整数 → 浮点 → 字符串 依次尝试，保证类型正确。
      try { return node.as<bool>(); } catch (...) {}
      try { return node.as<long long>(); } catch (...) {}
      try { return node.as<unsigned long long>(); } catch (...) {}
      try { return node.as<double>(); } catch (...) {}
      try { return node.as<std::string>(); } catch (...) {}
      return nullptr;
    }
    case YAML::NodeType::Sequence: {
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& item : node) {
        arr.push_back(yamlToJson(item));
      }
      return arr;
    }
    case YAML::NodeType::Map: {
      nlohmann::json obj = nlohmann::json::object();
      for (const auto& item : node) {
        obj[item.first.as<std::string>()] = yamlToJson(item.second);
      }
      return obj;
    }
  }
  return nullptr;
}

inline YAML::Node jsonToYaml(const nlohmann::json& j) {
  if (j.is_null()) return YAML::Node(YAML::NodeType::Null);
  if (j.is_boolean()) return YAML::Node(j.get<bool>());
  if (j.is_number_integer()) return YAML::Node(j.get<long long>());
  if (j.is_number_unsigned()) return YAML::Node(j.get<unsigned long long>());
  if (j.is_number_float()) return YAML::Node(j.get<double>());
  if (j.is_string()) return YAML::Node(j.get<std::string>());
  if (j.is_array()) {
    YAML::Node n(YAML::NodeType::Sequence);
    for (const auto& e : j) {
      n.push_back(jsonToYaml(e));
    }
    return n;
  }
  if (j.is_object()) {
    YAML::Node n(YAML::NodeType::Map);
    for (auto it = j.begin(); it != j.end(); ++it) {
      n[it.key()] = jsonToYaml(it.value());
    }
    return n;
  }
  return YAML::Node(YAML::NodeType::Null);
}

// 读取 YAML 文件为 json；失败时 *ok=false 并返回空对象。
inline nlohmann::json loadYaml(const QString& path, bool* ok = nullptr) {
  try {
    const YAML::Node node = YAML::LoadFile(path.toStdString());
    if (ok) *ok = true;
    return yamlToJson(node);
  } catch (...) {
    if (ok) *ok = false;
    return nlohmann::json::object();
  }
}

// 解析内存中的 YAML 文本为 json（字符串模板/订阅原文；失败返回空对象）。
inline nlohmann::json parseYamlStr(const std::string& content, bool* ok = nullptr) {
  try {
    const YAML::Node node = YAML::Load(content);
    if (ok) *ok = true;
    return yamlToJson(node);
  } catch (...) {
    if (ok) *ok = false;
    return nlohmann::json::object();
  }
}

// 序列化 json 为 YAML 文本（用于 JS 覆写的 yaml.stringify）。
inline std::string toYamlString(const nlohmann::json& j) {
  YAML::Emitter emitter;
  emitter << jsonToYaml(j);
  return emitter.good() ? std::string(emitter.c_str()) : std::string();
}

// 原子写 YAML（tmp + rename）。
inline bool saveYaml(const QString& path, const nlohmann::json& j) {
  YAML::Emitter emitter;
  emitter << jsonToYaml(j);
  if (!emitter.good()) {
    return false;
  }

  const QString tmpPath = path + QStringLiteral(".tmp");
  {
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return false;
    }
    const std::string content = emitter.c_str();
    if (file.write(content.data(), static_cast<qint64>(content.size())) !=
        static_cast<qint64>(content.size())) {
      return false;
    }
    file.close();
  }
  QFile::remove(path);
  if (!QFile::rename(tmpPath, path)) {
    QFile::remove(tmpPath);
    return false;
  }
  return true;
}

}  // namespace sparkle::core