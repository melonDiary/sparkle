// Sparkle C++/QuickJS 示例脚本。
// 该脚本由 cpp/src/app/main.cpp 在应用启动时加载；脚本错误会写入应用日志，
// 但不会阻止桌面应用继续启动。

core.on('log', function (line) {
  console.log('[mihomo]', line)
})

core.on('state', function (state) {
  console.log('内核状态：' + state)
})

core.on('started', function () {
  console.log('Mihomo 已启动')
})

core.on('stopped', function () {
  console.log('Mihomo 已停止')
})

core.on('crash', function (exitCode) {
  console.error('Mihomo 异常退出，退出码：' + exitCode)
})

function onStartProxy() {
  ui.status('正在启动代理…')
  core.start()
}

function onStopProxy() {
  ui.status('正在停止代理…')
  core.stop()
}

function onRestartProxy() {
  ui.status('正在重启代理…')
  core.restart()
}

console.log('当前内核状态：' + core.state())
// 如需由脚本主动启动，可使用：core.start('/path/to/config.yaml')
