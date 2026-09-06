// Sparkle plugin example. Plugins run in an isolated QuickJS runtime.
module.exports = {
  id: 'hello-world',

  onLoad: function () {
    sparkle.log('hello-world loaded')
    sparkle.ui.showNotification('Hello from Sparkle plugin')
  },

  onUnload: function () {
    sparkle.log('hello-world unloaded')
  },

  onProxyStart: function () {
    sparkle.log('proxy started')
  },

  onProxyStop: function () {
    sparkle.log('proxy stopped')
  }
}
