import { app, BrowserWindow } from 'electron'
import { registerIpcHandler } from './ipc-registration'
import { ipcErrorWrapper } from './ipc-error'
import { setNotQuitDialog } from '../resolve/appLifecycle'
import { showTrayIcon, closeTrayIcon, updateTrayIcon, setDockVisible, copyEnv } from '../resolve/tray'
import { applyTheme, fetchThemes, importThemes, readTheme, resolveThemes, writeTheme } from '../resolve/theme'
import { showFloatingWindow, closeFloatingWindow, showContextMenu } from '../resolve/floatingWindow'
import { setNativeTheme, openFile, readTextFile, readImageFileDataURL, openUWPTool, setupFirewall, resetAppConfig } from '../sys/misc'
import { getFilePath } from '../sys/misc'
import { findSystemMihomo, logDir } from './dirs'
import { getInterfaces } from '../sys/interface'
import { webdavBackup, webdavRestore, listWebdavBackups, webdavDelete } from '../resolve/backup'
import { registerShortcut } from '../resolve/shortcut'
import { getGistUrl } from '../resolve/gistApi'
import { getIconDataURL, getImageDataURL } from './icon'
import { getAppName } from '@uruhalushia/sparkle-native'
import { showNotification } from './notification'
import { getUserAgent } from './userAgent'
import { ageIdentityToRecipient, generateAgeKeyPair } from './age'
import { quitWithoutCore } from '../core/manager'
import path from 'path'
import v8 from 'v8'

export interface WindowIpcDeps {
  getMainWindow: () => BrowserWindow | null
  showMainWindow: () => Promise<void>
  closeMainWindow: () => void
  triggerMainWindow: () => Promise<void>
}

export function registerWindowIpc(deps: WindowIpcDeps): void {
  const { getMainWindow, showMainWindow, closeMainWindow, triggerMainWindow } = deps
  const r = registerIpcHandler
  const w = ipcErrorWrapper
  const mainWindow = getMainWindow
  r('openUWPTool', w(openUWPTool))
  r('setupFirewall', w(setupFirewall))
  r('getInterfaces', getInterfaces)
  r('webdavBackup', w(webdavBackup))
  r('webdavRestore', (_e, filename) => w(webdavRestore)(filename))
  r('listWebdavBackups', w(listWebdavBackups))
  r('webdavDelete', (_e, filename) => w(webdavDelete)(filename))
  r('registerShortcut', (_e, oldShortcut, newShortcut, action) =>
    w(registerShortcut)(oldShortcut, newShortcut, action)
  )
  r('getGistUrl', w(getGistUrl))
  r('setNativeTheme', (_e, theme) => { setNativeTheme(theme as 'system' | 'light' | 'dark') })
  r('setTitleBarOverlay', (_e, overlay) =>
    w(async (overlay): Promise<void> => {
      const win = mainWindow()
      if (typeof win?.setTitleBarOverlay === 'function') win.setTitleBarOverlay(overlay)
    })(overlay)
  )
  r('setAlwaysOnTop', (_e, alwaysOnTop) => { mainWindow()?.setAlwaysOnTop(alwaysOnTop as boolean) })
  r('isAlwaysOnTop', () => mainWindow()?.isAlwaysOnTop())
  r('showTrayIcon', () => w(showTrayIcon)())
  r('closeTrayIcon', () => w(closeTrayIcon)())
  r('updateTrayIcon', () => w(updateTrayIcon)())
  r('setDockVisible', (_e, visible: boolean) => setDockVisible(visible))
  r('showMainWindow', showMainWindow)
  r('closeMainWindow', closeMainWindow)
  r('triggerMainWindow', triggerMainWindow)
  r('showFloatingWindow', () => w(showFloatingWindow)())
  r('closeFloatingWindow', () => w(closeFloatingWindow)())
  r('showContextMenu', () => w(showContextMenu)())
  r('openFile', (_e, type, id, ext) => openFile(type as 'profile' | 'override', id as string, ext as 'yaml' | 'js' | undefined))
  r('openDevTools', () => { mainWindow()?.webContents.openDevTools() })
  r('createHeapSnapshot', () => v8.writeHeapSnapshot(path.join(logDir(), `${Date.now()}.heapsnapshot`)))
  r('getUserAgent', () => w(getUserAgent)())
  r('generateAgeKeyPair', () => w(generateAgeKeyPair)())
  r('ageIdentityToRecipient', (_e, identity) => w(ageIdentityToRecipient)(identity))
  r('getAppName', (_e, appPath) => w(getAppName)(appPath))
  r('getImageDataURL', (_e, url) => w(getImageDataURL)(url))
  r('getIconDataURL', (_e, appPath) => w(getIconDataURL)(appPath))
  r('resolveThemes', () => w(resolveThemes)())
  r('fetchThemes', () => w(fetchThemes)())
  r('importThemes', (_e, file) => w(importThemes)(file as string[]))
  r('readTheme', (_e, theme) => w(readTheme)(theme))
  r('writeTheme', (_e, theme, css) => w(writeTheme)(theme, css))
  r('applyTheme', (_e, theme) => w(applyTheme)(theme))
  r('copyEnv', (_e, type) => w(copyEnv)(type))
  r('alert', (_e, msg) => { void showNotification({ title: 'Sparkle', body: msg as string, variant: 'danger' }) })
  r('resetAppConfig', resetAppConfig)
  r('relaunchApp', () => { setNotQuitDialog(); app.relaunch(); app.quit() })
  r('quitWithoutCore', w(quitWithoutCore))
  r('findSystemMihomo', () => findSystemMihomo())
  r('getFilePath', (_e, ext, title, filterName) => getFilePath(ext as string[], title as string | undefined, filterName as string | undefined))
  r('readTextFile', (_e, filePath) => w(readTextFile)(filePath))
  r('readImageFileDataURL', (_e, filePath) => w(readImageFileDataURL)(filePath))
}
