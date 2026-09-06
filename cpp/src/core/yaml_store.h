#pragma once

#include <QDebug>
#include <QFileInfo>
#include <QString>
#include <functional>
#include <optional>
#include <utility>

#include "json_yaml.h"

// 通用 YAML 缓存的读写封装（对应原 config/cached-yaml-store.ts）。
// 行为：lazy load + 内存缓存 + force reload + 序列化 + 原子写 + 缺省初始化。

namespace sparkle::core {

// T 需为 nlohmann::json（或与其兼容、可被 normalize_ 处理的值类型）。
template <typename T>
class YamlStore {
public:
  using Factory = std::function<T()>;
  using Normalizer = std::function<T(T)>;

  YamlStore(QString path, Factory createDefault, Normalizer normalize,
            bool initializeOnMissing = true)
      : path_(std::move(path)),
        createDefault_(std::move(createDefault)),
        normalize_(std::move(normalize)),
        initializeOnMissing_(initializeOnMissing) {}

  // 首次或 force 时读盘并 normalize，否则返回缓存。
  T get(bool force = false) {
    if (!force && cache_) {
      return *cache_;
    }

    T loaded;
    const bool exists = QFileInfo::exists(path_);
    if (!exists) {
      // 文件缺失（首次运行 / 未初始化）：保留缺失语义，回退默认值，不视为错误。
      loaded = createDefault_();
    } else {
      bool ok = false;
      loaded = loadYaml(path_, &ok);
      if (!ok) {
        // 文件存在但解析失败（YAML 语法/类型错误）→ 真正的错误：告警并降级到默认值，
        // 避免静默吞错（对齐原 cached-yaml-store 抛错语义，改为可恢复降级）。
        qWarning().noquote() << "[YamlStore] 解析失败，已回退默认值:" << path_;
        loaded = createDefault_();
      }
    }
    loaded = normalize_(std::move(loaded));

    // 首次运行且允许初始化：把规范化后的默认值落盘，使配置文件就位（写失败不阻塞）。
    if (!exists && initializeOnMissing_) {
      if (!saveYaml(path_, loaded)) {
        qWarning().noquote() << "[YamlStore] 初始化写入失败:" << path_;
      }
    }
    cache_ = loaded;
    return loaded;
  }

  // 先落盘、成功后更新缓存；失败则回滚缓存（清空，下次 get 重新读盘）。
  void set(const T& value) {
    if (!saveYaml(path_, value)) {
      qWarning().noquote() << "[YamlStore] 写入失败，已回滚缓存:" << path_;
      cache_.reset();
      return;
    }
    cache_ = value;
  }

  void clear() { cache_.reset(); }

private:
  QString path_;
  Factory createDefault_;
  Normalizer normalize_;
  bool initializeOnMissing_;
  std::optional<T> cache_;
};

}  // namespace sparkle::core