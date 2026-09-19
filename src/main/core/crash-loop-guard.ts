import { appendAppLog } from '../utils/log'
import { showNotification } from '../utils/notification'

const crashWindowMs = 60 * 1000
const crashLimit = 5

const state = {
  crashTimes: [] as number[],
  notificationShown: false
}

/**
 * Bounds the automatic restart triggered by `child.on('close')`. Every
 * successful startup resets the window, so this only trips when the core dies
 * repeatedly in quick succession (e.g. a port conflict or a corrupt profile)
 * instead of restart-looping forever.
 */
export function registerCoreCrash(): boolean {
  const now = Date.now()
  state.crashTimes = state.crashTimes.filter((time) => now - time < crashWindowMs)
  state.crashTimes.push(now)

  if (state.crashTimes.length < crashLimit) return true

  if (!state.notificationShown) {
    state.notificationShown = true
    void showNotification({
      title: '内核反复崩溃，已停止自动重启',
      body: '请检查内核设置或订阅配置，或手动重启内核',
      variant: 'danger'
    })
  }
  void appendAppLog(
    `[Manager]: core crashed ${state.crashTimes.length} times within ${crashWindowMs / 1000}s, auto-restart disabled\n`
  ).catch(() => {})
  return false
}

export function registerCoreStartSuccess(): void {
  state.crashTimes = []
  state.notificationShown = false
}
