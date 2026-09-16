#pragma once

// QuickJS JSValue / C 字符串的 RAII 门面。此前项目只有 RuntimePtr/ContextPtr；本头补上
// 值级 JSValue 与 JS_ToCString 结果的自动释放，避免沿错误路径早退时遗漏 JS_FreeValue。
//
// 只封装「所有权」这一薄层，不改变 QuickJS 的 steal/dup 语义：
//  * 需要交还所有权给 JS_SetPropertyStr / JS_Eval 等「接管」API 时用 release()；
//  * 仅借用（不释放）的场合仍应使用裸 JSValue。
// move-only，禁止拷贝，防止同一 JSValue 被两处释放。

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
extern "C" {
#include "quickjs.h"
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace sparkle::core {

class JSValuePtr {
public:
  JSValuePtr() = default;
  JSValuePtr(JSContext* ctx, JSValue value) : ctx_(ctx), value_(value) {}

  JSValuePtr(const JSValuePtr&) = delete;
  JSValuePtr& operator=(const JSValuePtr&) = delete;

  JSValuePtr(JSValuePtr&& other) noexcept : ctx_(other.ctx_), value_(other.value_) {
    other.value_ = JS_UNDEFINED;
  }
  JSValuePtr& operator=(JSValuePtr&& other) noexcept {
    if (this != &other) {
      reset();
      ctx_ = other.ctx_;
      value_ = other.value_;
      other.value_ = JS_UNDEFINED;
    }
    return *this;
  }

  ~JSValuePtr() { reset(); }

  JSValue get() const { return value_; }
  JSContext* ctx() const { return ctx_; }

  // 交还所有权（此后不再由本对象释放）。用于 JS_SetPropertyStr 等「接管 value」的 API。
  JSValue release() {
    JSValue value = value_;
    value_ = JS_UNDEFINED;
    return value;
  }

  void reset() {
    if (ctx_ && !JS_IsUndefined(value_)) JS_FreeValue(ctx_, value_);
    value_ = JS_UNDEFINED;
  }

  bool isException() const { return JS_IsException(value_); }
  bool isUndefined() const { return JS_IsUndefined(value_); }

private:
  JSContext* ctx_ = nullptr;
  JSValue value_ = JS_UNDEFINED;
};

// JS_ToCString 返回的 C 字符串由 JS_FreeCString 释放；借用内嵌缓冲时 JS_FreeCString
// 会自动判定而不重复释放，因此总是成对调用即可。
class JSCStringPtr {
public:
  JSCStringPtr() = default;
  JSCStringPtr(JSContext* ctx, const char* text) : ctx_(ctx), text_(text) {}

  JSCStringPtr(const JSCStringPtr&) = delete;
  JSCStringPtr& operator=(const JSCStringPtr&) = delete;

  JSCStringPtr(JSCStringPtr&& other) noexcept : ctx_(other.ctx_), text_(other.text_) {
    other.text_ = nullptr;
  }
  JSCStringPtr& operator=(JSCStringPtr&& other) noexcept {
    if (this != &other) {
      reset();
      ctx_ = other.ctx_;
      text_ = other.text_;
      other.text_ = nullptr;
    }
    return *this;
  }

  ~JSCStringPtr() { reset(); }

  const char* get() const { return text_; }
  void reset() {
    if (text_ && ctx_) JS_FreeCString(ctx_, text_);
    text_ = nullptr;
  }

private:
  JSContext* ctx_ = nullptr;
  const char* text_ = nullptr;
};

}  // namespace sparkle::core