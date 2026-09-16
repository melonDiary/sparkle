// Sparkle 默认脚本：把 UI 的「启动/停止/重启」按钮接到内核控制。
// 此文件是可选的安检模板 —— 应用内已内置等价默认实现；若此文件存在，
// 其中定义的同名函数会覆盖内置默认（用 core.* 或任意自定义逻辑均可）。

function onStartProxy() {
  if (typeof core !== 'undefined' && core) return core.start();
  return false;
}

function onStopProxy() {
  if (typeof core !== 'undefined' && core) return core.stop();
  return false;
}

function onRestartProxy() {
  if (typeof core !== 'undefined' && core) core.restart();
  return typeof core !== 'undefined' && !!core;
}

// 其余可用的全局对象：
//   core.start([configPath]) / core.stop() / core.restart() / core.isRunning() / core.state()
//   core.on("log"|"crash"|"started"|"stopped"|"state", callback)
//   console.log/info/debug/error(...)
//   ui.status(message)         —— 更新顶部状态提示
//   fetch(url, options)        —— 真网络 fetch（异步 Promise）
//   yaml.parse / yaml.stringify
//   b64e / b64d / Buffer