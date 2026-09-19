import { app, globalShortcut, ipcMain } from 'electron'
import { setNotQuitDialog } from './appLifecycle'
import { getMainWindow, triggerMainWindow } from './window-ref'
import {
  getAppConfig,
  getControledMihomoConfig,
  patchAppConfig,
  patchControledMihomoConfig
} from '../config'
import { triggerSysProxy } from '../sys/sysproxy'
import { patchMihomoConfig } from '../core/mihomoApi'
import { quitWithoutCore, restartCore } from '../core/manager'
import { floatingWindow, triggerFloatingWindow } from './floatingWindow'
import { showNotification } from '../utils/notification'
import { appendAppLog } from '../utils/log'
import { IPC_EVENTS } from '../../shared/ipc'

export async function registerShortcut(
  oldShortcut: string,
  newShortcut: string,
  action: string
): Promise<boolean> {
  if (oldShortcut !== '') {
    globalShortcut.unregister(oldShortcut)
  }
  if (newShortcut === '') {
    return true
  }
  switch (action) {
    case 'showWindowShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        await triggerMainWindow()
      })
    }
    case 'showFloatingWindowShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        await triggerFloatingWindow()
      })
    }
    case 'triggerSysProxyShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        const {
          sysProxy: { enable },
          onlyActiveDevice = false
        } = await getAppConfig()
        try {
          await triggerSysProxy(!enable, onlyActiveDevice)
          await patchAppConfig({ sysProxy: { enable: !enable } })
          void showNotification({
            title: `系统代理已${!enable ? '开启' : '关闭'}`
          })
          getMainWindow()?.webContents.send(IPC_EVENTS.APP_CONFIG_UPDATED)
          floatingWindow?.webContents.send(IPC_EVENTS.APP_CONFIG_UPDATED)
        } catch {
          // ignore
        } finally {
          ipcMain.emit(IPC_EVENTS.UPDATE_TRAY_MENU)
        }
      })
    }
    case 'triggerTunShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        const { tun } = await getControledMihomoConfig()
        const enable = tun?.enable ?? false
        try {
          if (!enable) {
            await patchControledMihomoConfig({ tun: { enable: !enable }, dns: { enable: true } })
          } else {
            await patchControledMihomoConfig({ tun: { enable: !enable } })
          }
          await restartCore()
          void showNotification({
            title: `虚拟网卡已${!enable ? '开启' : '关闭'}`
          })
          getMainWindow()?.webContents.send(IPC_EVENTS.CONTROLLED_MIHOMO_CONFIG_UPDATED)
          floatingWindow?.webContents.send(IPC_EVENTS.APP_CONFIG_UPDATED)
        } catch {
          // ignore
        } finally {
          ipcMain.emit(IPC_EVENTS.UPDATE_TRAY_MENU)
        }
      })
    }
    case 'ruleModeShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        await patchControledMihomoConfig({ mode: 'rule' })
        await patchMihomoConfig({ mode: 'rule' })
        void showNotification({
          title: '已切换至规则模式'
        })
        getMainWindow()?.webContents.send(IPC_EVENTS.CONTROLLED_MIHOMO_CONFIG_UPDATED)
        ipcMain.emit(IPC_EVENTS.UPDATE_TRAY_MENU)
      })
    }
    case 'globalModeShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        await patchControledMihomoConfig({ mode: 'global' })
        await patchMihomoConfig({ mode: 'global' })
        void showNotification({
          title: '已切换至全局模式'
        })
        getMainWindow()?.webContents.send(IPC_EVENTS.CONTROLLED_MIHOMO_CONFIG_UPDATED)
        ipcMain.emit(IPC_EVENTS.UPDATE_TRAY_MENU)
      })
    }
    case 'directModeShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        await patchControledMihomoConfig({ mode: 'direct' })
        await patchMihomoConfig({ mode: 'direct' })
        void showNotification({
          title: '已切换至直连模式'
        })
        getMainWindow()?.webContents.send(IPC_EVENTS.CONTROLLED_MIHOMO_CONFIG_UPDATED)
        ipcMain.emit(IPC_EVENTS.UPDATE_TRAY_MENU)
      })
    }
    case 'quitWithoutCoreShortcut': {
      return globalShortcut.register(newShortcut, async () => {
        setNotQuitDialog()
        await quitWithoutCore()
      })
    }
    case 'restartAppShortcut': {
      return globalShortcut.register(newShortcut, () => {
        setNotQuitDialog()
        app.relaunch()
        app.quit()
      })
    }
  }
  throw new Error('Unknown action')
}

type ShortcutConfigKey =
  | 'showWindowShortcut'
  | 'showFloatingWindowShortcut'
  | 'triggerSysProxyShortcut'
  | 'triggerTunShortcut'
  | 'ruleModeShortcut'
  | 'globalModeShortcut'
  | 'directModeShortcut'
  | 'quitWithoutCoreShortcut'
  | 'restartAppShortcut'

export async function initShortcut(): Promise<void> {
  const appConfig = await getAppConfig()

  const shortcutEntries: [ShortcutConfigKey, ShortcutConfigKey][] = [
    ['showWindowShortcut', 'showWindowShortcut'],
    ['showFloatingWindowShortcut', 'showFloatingWindowShortcut'],
    ['triggerSysProxyShortcut', 'triggerSysProxyShortcut'],
    ['triggerTunShortcut', 'triggerTunShortcut'],
    ['ruleModeShortcut', 'ruleModeShortcut'],
    ['globalModeShortcut', 'globalModeShortcut'],
    ['directModeShortcut', 'directModeShortcut'],
    ['quitWithoutCoreShortcut', 'quitWithoutCoreShortcut'],
    ['restartAppShortcut', 'restartAppShortcut']
  ]

  for (const [configKey, action] of shortcutEntries) {
    const shortcut = appConfig[configKey]
    if (!shortcut) continue
    if (typeof shortcut !== 'string') continue
    try {
      await registerShortcut('', shortcut, action)
    } catch (error) {
      // A failed registration must not block the remaining shortcuts, but it
      // should not be silent either: the user expects the binding to work.
      await appendAppLog(`[Shortcut]: register ${action} failed, ${error}\n`).catch(() => {})
      void showNotification({
        title: '快捷键注册失败',
        body: `${shortcut} 可能已被其他应用占用`,
        variant: 'warning'
      })
    }
  }
}
