import { app } from 'electron'
import {
  mihomoChangeProxy,
  mihomoCloseConnections,
  mihomoCloseConnection,
  mihomoGroupDelay,
  mihomoGroups,
  mihomoProxies,
  mihomoProxyDelay,
  mihomoProxyProviders,
  mihomoRuleProviders,
  mihomoRules,
  mihomoUnfixedProxy,
  mihomoUpdateProxyProviders,
  mihomoUpdateRuleProviders,
  mihomoUpgrade,
  mihomoUpgradeUI,
  mihomoUpgradeGeo,
  mihomoVersion,
  mihomoConfig,
  patchMihomoConfig,
  restartMihomoLogs,
  restartMihomoConnections,
  mihomoRulesDisable
} from '../core/mihomoApi'
import { checkAutoRun, disableAutoRun, enableAutoRun } from '../sys/autoRun'
import {
  getAppConfig,
  patchAppConfig,
  getControledMihomoConfig,
  patchControledMihomoConfig,
  getProfileConfig,
  getCurrentProfileItem,
  getProfileItem,
  addProfileItem,
  removeProfileItem,
  changeCurrentProfile,
  getProfileStr,
  getFileStr,
  getFilePreviewStr,
  setFileStr,
  saveFileStrWithElevation,
  setProfileStr,
  updateProfileItem,
  setProfileConfig,
  getOverrideConfig,
  setOverrideConfig,
  getOverrideItem,
  addOverrideItem,
  removeOverrideItem,
  getOverride,
  setOverride,
  updateOverrideItem
} from '../config'
import { quitWithoutCore, restartCore, startNetworkDetection, stopCore } from '../core/manager'
import { stopNetworkDetection } from '../core/network'
import {
  checkCorePermission,
  manualGrantCorePermition,
  revokeCorePermission
} from '../core/permission'
import { triggerSysProxy } from '../sys/sysproxy'
import { checkUpdate, downloadAndInstallUpdate, cancelUpdate } from '../resolve/autoUpdater'
import {
  checkElevateTask,
  deleteElevateTask,
  getFilePath,
  openFile,
  openUWPTool,
  readImageFileDataURL,
  readTextFile,
  resetAppConfig,
  setNativeTheme,
  setupFirewall
} from '../sys/misc'
import {
  serviceStatus,
  installService,
  uninstallService,
  startService,
  stopService,
  initService,
  testServiceConnection,
  restartService
} from '../service/manager'
import { patchCoreProfile } from '../service/api'
import { coreLogPath, findSystemMihomo, logDir } from './dirs'
import {
  getRuntimeConfig,
  getRuntimeConfigStr,
  getRawProfileStr,
  getCurrentProfileStr,
  getOverrideProfileStr
} from '../core/factory'
import { listWebdavBackups, webdavBackup, webdavDelete, webdavRestore } from '../resolve/backup'
import { getInterfaces } from '../sys/interface'
import {
  closeTrayIcon,
  copyEnv,
  setDockVisible,
  showTrayIcon,
  updateTrayIcon
} from '../resolve/tray'
import { registerShortcut } from '../resolve/shortcut'
import {
  closeMainWindow,
  mainWindow,
  setNotQuitDialog,
  showMainWindow,
  triggerMainWindow
} from '..'
import {
  applyTheme,
  fetchThemes,
  importThemes,
  readTheme,
  resolveThemes,
  writeTheme
} from '../resolve/theme'
import path from 'path'
import v8 from 'v8'
import { getGistUrl } from '../resolve/gistApi'
import { getIconDataURL, getImageDataURL } from './icon'
import { startMonitor } from '../resolve/trafficMonitor'
import { closeFloatingWindow, showContextMenu, showFloatingWindow } from '../resolve/floatingWindow'
import { getAppName } from '@uruhalushia/sparkle-native'
import { showNotification } from './notification'
import { getUserAgent } from './userAgent'
import { appendAppLog, clearCachedMihomoLogs, getCachedMihomoLogs } from './log'
import { ageIdentityToRecipient, generateAgeKeyPair } from './age'
import { registerIpcHandler } from './ipc-registration'
import { registerSubStoreIpc } from './ipc-substore'

function ipcErrorWrapper<T>( // eslint-disable-next-line @typescript-eslint/no-explicit-any
  fn: (...args: any[]) => T | Promise<T> // eslint-disable-next-line @typescript-eslint/no-explicit-any
): (...args: any[]) => Promise<T | { invokeError: unknown }> {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  return async (...args: any[]) => {
    try {
      return await fn(...args)
    } catch (e) {
      if (e && typeof e === 'object') {
        if ('message' in e) {
          return { invokeError: e.message }
        } else {
          return { invokeError: JSON.stringify(e) }
        }
      }
      if (e instanceof Error || typeof e === 'string') {
        return { invokeError: e }
      }
      return { invokeError: 'Unknown Error' }
    }
  }
}

async function patchAppConfigWithServiceSync(patch: Partial<AppConfig>): Promise<AppConfig> {
  const nextConfig = await patchAppConfig(await normalizeServiceModePatch(patch))

  if (!('saveLogs' in patch || 'maxLogFileSizeMB' in patch)) {
    return nextConfig
  }

  const {
    corePermissionMode = 'elevated',
    saveLogs = true,
    maxLogFileSizeMB = 20
  } = await getAppConfig()
  if (corePermissionMode !== 'service') {
    return nextConfig
  }

  void patchCoreProfile({
    log_path: coreLogPath(),
    save_logs: saveLogs,
    max_log_file_size_mb: maxLogFileSizeMB
  }).catch((error) => {
    appendAppLog(`[Service]: sync core log config failed, ${error}\n`).catch(() => {})
  })

  return nextConfig
}

async function normalizeServiceModePatch(patch: Partial<AppConfig>): Promise<Partial<AppConfig>> {
  if (patch.sysProxy?.settingMode !== 'service') {
    return patch
  }

  const status = await serviceStatus().catch(() => 'unknown' as const)
  if (status === 'running') {
    return patch
  }

  void showNotification({ title: '服务不可用，已切换到执行命令模式' })
  return {
    ...patch,
    sysProxy: {
      ...patch.sysProxy,
      settingMode: 'exec',
      guard: false,
      guardNotify: false
    }
  }
}

export function registerIpcMainHandlers(): void {
  const register = registerIpcHandler
  register('mihomoVersion', ipcErrorWrapper(mihomoVersion))
  register('mihomoConfig', ipcErrorWrapper(mihomoConfig))
  register('mihomoCloseConnection', (_e, id) => ipcErrorWrapper(mihomoCloseConnection)(id))
  register('mihomoCloseConnections', (_e, name) =>
    ipcErrorWrapper(mihomoCloseConnections)(name)
  )
  register('mihomoRules', ipcErrorWrapper(mihomoRules))
  register('mihomoProxies', ipcErrorWrapper(mihomoProxies))
  register('mihomoGroups', ipcErrorWrapper(mihomoGroups))
  register('mihomoProxyProviders', ipcErrorWrapper(mihomoProxyProviders))
  register('mihomoUpdateProxyProviders', (_e, name) =>
    ipcErrorWrapper(mihomoUpdateProxyProviders)(name)
  )
  register('mihomoRuleProviders', ipcErrorWrapper(mihomoRuleProviders))
  register('mihomoUpdateRuleProviders', (_e, name) =>
    ipcErrorWrapper(mihomoUpdateRuleProviders)(name)
  )
  register('mihomoChangeProxy', (_e, group, proxy) =>
    ipcErrorWrapper(mihomoChangeProxy)(group, proxy)
  )
  register('mihomoUnfixedProxy', (_e, group) => ipcErrorWrapper(mihomoUnfixedProxy)(group))
  register('mihomoUpgradeGeo', ipcErrorWrapper(mihomoUpgradeGeo))
  register('mihomoUpgradeUI', ipcErrorWrapper(mihomoUpgradeUI))
  register('mihomoUpgrade', (_e, channel) => ipcErrorWrapper(mihomoUpgrade)(channel))
  register('mihomoProxyDelay', (_e, proxy, url, provider) =>
    ipcErrorWrapper(mihomoProxyDelay)(proxy, url, provider)
  )
  register('mihomoGroupDelay', (_e, group, url) =>
    ipcErrorWrapper(mihomoGroupDelay)(group, url)
  )
  register('mihomoRulesDisable', (_e, rules) => ipcErrorWrapper(mihomoRulesDisable)(rules))
  register('patchMihomoConfig', (_e, patch) => ipcErrorWrapper(patchMihomoConfig)(patch))
  register('restartMihomoLogs', ipcErrorWrapper(restartMihomoLogs))
  register('checkAutoRun', ipcErrorWrapper(checkAutoRun))
  register('enableAutoRun', ipcErrorWrapper(enableAutoRun))
  register('disableAutoRun', ipcErrorWrapper(disableAutoRun))
  register('getAppConfig', (_e, force) => ipcErrorWrapper(getAppConfig)(force))
  register('getCachedMihomoLogs', () => getCachedMihomoLogs())
  register('clearCachedMihomoLogs', () => clearCachedMihomoLogs())
  register('patchAppConfig', (_e, config) =>
    ipcErrorWrapper(patchAppConfigWithServiceSync)(config)
  )
  register('getControledMihomoConfig', (_e, force) =>
    ipcErrorWrapper(getControledMihomoConfig)(force)
  )
  register('patchControledMihomoConfig', (_e, config) =>
    ipcErrorWrapper(patchControledMihomoConfig)(config)
  )
  register('getProfileConfig', (_e, force) => ipcErrorWrapper(getProfileConfig)(force))
  register('setProfileConfig', (_e, config) => ipcErrorWrapper(setProfileConfig)(config))
  register('getCurrentProfileItem', ipcErrorWrapper(getCurrentProfileItem))
  register('getProfileItem', (_e, id) => ipcErrorWrapper(getProfileItem)(id))
  register('getProfileStr', (_e, id) => ipcErrorWrapper(getProfileStr)(id))
  register('getFileStr', (_e, path, ageSecretKey) =>
    ipcErrorWrapper(getFileStr)(path, ageSecretKey)
  )
  register('getFilePreviewStr', (_e, path, format) =>
    ipcErrorWrapper(getFilePreviewStr)(path, format)
  )
  register('setFileStr', (_e, path, str) => ipcErrorWrapper(setFileStr)(path, str))
  register('saveFileStrWithElevation', (_e, path, str) =>
    ipcErrorWrapper(saveFileStrWithElevation)(path, str)
  )
  register('setProfileStr', (_e, id, str) => ipcErrorWrapper(setProfileStr)(id, str))
  register('updateProfileItem', (_e, item) => ipcErrorWrapper(updateProfileItem)(item))
  register('changeCurrentProfile', (_e, id) => ipcErrorWrapper(changeCurrentProfile)(id))
  register('addProfileItem', (_e, item) => ipcErrorWrapper(addProfileItem)(item))
  register('removeProfileItem', (_e, id) => ipcErrorWrapper(removeProfileItem)(id))
  register('getOverrideConfig', (_e, force) => ipcErrorWrapper(getOverrideConfig)(force))
  register('setOverrideConfig', (_e, config) => ipcErrorWrapper(setOverrideConfig)(config))
  register('getOverrideItem', (_e, id) => ipcErrorWrapper(getOverrideItem)(id))
  register('addOverrideItem', (_e, item) => ipcErrorWrapper(addOverrideItem)(item))
  register('removeOverrideItem', (_e, id) => ipcErrorWrapper(removeOverrideItem)(id))
  register('updateOverrideItem', (_e, item) => ipcErrorWrapper(updateOverrideItem)(item))
  register('getOverride', (_e, id, ext) => ipcErrorWrapper(getOverride)(id, ext))
  register('setOverride', (_e, id, ext, str) => ipcErrorWrapper(setOverride)(id, ext, str))
  register('restartCore', ipcErrorWrapper(restartCore))
  register('stopCore', ipcErrorWrapper(stopCore))
  register('restartMihomoConnections', ipcErrorWrapper(restartMihomoConnections))
  register('startMonitor', (_e, detached) => ipcErrorWrapper(startMonitor)(detached))
  register('triggerSysProxy', (_e, enable, onlyActiveDevice, useRegistry) =>
    ipcErrorWrapper(triggerSysProxy)(enable, onlyActiveDevice, useRegistry)
  )
  register('manualGrantCorePermition', (_e, cores?: ('mihomo' | 'mihomo-alpha')[]) =>
    ipcErrorWrapper(manualGrantCorePermition)(cores)
  )
  register('checkCorePermission', () => ipcErrorWrapper(checkCorePermission)())
  register('revokeCorePermission', (_e, cores?: ('mihomo' | 'mihomo-alpha')[]) =>
    ipcErrorWrapper(revokeCorePermission)(cores)
  )
  register('checkElevateTask', () => ipcErrorWrapper(checkElevateTask)())
  register('deleteElevateTask', () => ipcErrorWrapper(deleteElevateTask)())
  register('serviceStatus', () => ipcErrorWrapper(serviceStatus)())
  register('testServiceConnection', () => ipcErrorWrapper(testServiceConnection)())
  register('initService', () => ipcErrorWrapper(initService)())
  register('installService', () => ipcErrorWrapper(installService)())
  register('uninstallService', () => ipcErrorWrapper(uninstallService)())
  register('startService', () => ipcErrorWrapper(startService)())
  register('restartService', () => ipcErrorWrapper(restartService)())
  register('stopService', () => ipcErrorWrapper(stopService)())
  register('findSystemMihomo', () => findSystemMihomo())
  register('getFilePath', (_e, ext, title, filterName) => getFilePath(ext, title, filterName))
  register('readTextFile', (_e, filePath) => ipcErrorWrapper(readTextFile)(filePath))
  register('readImageFileDataURL', (_e, filePath) =>
    ipcErrorWrapper(readImageFileDataURL)(filePath)
  )
  register('getRuntimeConfigStr', ipcErrorWrapper(getRuntimeConfigStr))
  register('getRawProfileStr', ipcErrorWrapper(getRawProfileStr))
  register('getCurrentProfileStr', ipcErrorWrapper(getCurrentProfileStr))
  register('getOverrideProfileStr', ipcErrorWrapper(getOverrideProfileStr))
  register('getRuntimeConfig', ipcErrorWrapper(getRuntimeConfig))
  register('downloadAndInstallUpdate', (_e, version, tag) =>
    ipcErrorWrapper(downloadAndInstallUpdate)(version, tag)
  )
  register('checkUpdate', ipcErrorWrapper(checkUpdate))
  register('cancelUpdate', ipcErrorWrapper(cancelUpdate))
  register('getVersion', () => app.getVersion())
  register('platform', () => process.platform)
  register('openUWPTool', ipcErrorWrapper(openUWPTool))
  register('setupFirewall', ipcErrorWrapper(setupFirewall))
  register('getInterfaces', getInterfaces)
  register('webdavBackup', ipcErrorWrapper(webdavBackup))
  register('webdavRestore', (_e, filename) => ipcErrorWrapper(webdavRestore)(filename))
  register('listWebdavBackups', ipcErrorWrapper(listWebdavBackups))
  register('webdavDelete', (_e, filename) => ipcErrorWrapper(webdavDelete)(filename))
  register('registerShortcut', (_e, oldShortcut, newShortcut, action) =>
    ipcErrorWrapper(registerShortcut)(oldShortcut, newShortcut, action)
  )
  registerSubStoreIpc(ipcErrorWrapper)
  register('getGistUrl', ipcErrorWrapper(getGistUrl))
  register('setNativeTheme', (_e, theme) => {
    setNativeTheme(theme)
  })
  register('setTitleBarOverlay', (_e, overlay) =>
    ipcErrorWrapper(async (overlay): Promise<void> => {
      if (typeof mainWindow?.setTitleBarOverlay === 'function') {
        mainWindow.setTitleBarOverlay(overlay)
      }
    })(overlay)
  )
  register('setAlwaysOnTop', (_e, alwaysOnTop) => {
    mainWindow?.setAlwaysOnTop(alwaysOnTop)
  })
  register('isAlwaysOnTop', () => {
    return mainWindow?.isAlwaysOnTop()
  })
  register('showTrayIcon', () => ipcErrorWrapper(showTrayIcon)())
  register('closeTrayIcon', () => ipcErrorWrapper(closeTrayIcon)())
  register('updateTrayIcon', () => ipcErrorWrapper(updateTrayIcon)())
  register('setDockVisible', (_e, visible: boolean) => setDockVisible(visible))
  register('showMainWindow', showMainWindow)
  register('closeMainWindow', closeMainWindow)
  register('triggerMainWindow', triggerMainWindow)
  register('showFloatingWindow', () => ipcErrorWrapper(showFloatingWindow)())
  register('closeFloatingWindow', () => ipcErrorWrapper(closeFloatingWindow)())
  register('showContextMenu', () => ipcErrorWrapper(showContextMenu)())
  register('openFile', (_e, type, id, ext) => openFile(type, id, ext))
  register('openDevTools', () => {
    mainWindow?.webContents.openDevTools()
  })
  register('createHeapSnapshot', () => {
    return v8.writeHeapSnapshot(path.join(logDir(), `${Date.now()}.heapsnapshot`))
  })
  register('getUserAgent', () => ipcErrorWrapper(getUserAgent)())
  register('generateAgeKeyPair', () => ipcErrorWrapper(generateAgeKeyPair)())
  register('ageIdentityToRecipient', (_e, identity) =>
    ipcErrorWrapper(ageIdentityToRecipient)(identity)
  )
  register('getAppName', (_e, appPath) => ipcErrorWrapper(getAppName)(appPath))
  register('getImageDataURL', (_e, url) => ipcErrorWrapper(getImageDataURL)(url))
  register('getIconDataURL', (_e, appPath) => ipcErrorWrapper(getIconDataURL)(appPath))
  register('resolveThemes', () => ipcErrorWrapper(resolveThemes)())
  register('fetchThemes', () => ipcErrorWrapper(fetchThemes)())
  register('importThemes', (_e, file) => ipcErrorWrapper(importThemes)(file))
  register('readTheme', (_e, theme) => ipcErrorWrapper(readTheme)(theme))
  register('writeTheme', (_e, theme, css) => ipcErrorWrapper(writeTheme)(theme, css))
  register('applyTheme', (_e, theme) => ipcErrorWrapper(applyTheme)(theme))
  register('copyEnv', (_e, type) => ipcErrorWrapper(copyEnv)(type))
  register('alert', (_e, msg) => {
    void showNotification({ title: 'Sparkle', body: msg, variant: 'danger' })
  })
  register('resetAppConfig', resetAppConfig)
  register('relaunchApp', () => {
    setNotQuitDialog()
    app.relaunch()
    app.quit()
  })
  register('quitWithoutCore', ipcErrorWrapper(quitWithoutCore))
  register('startNetworkDetection', ipcErrorWrapper(startNetworkDetection))
  register('stopNetworkDetection', ipcErrorWrapper(stopNetworkDetection))
  register('quitApp', () => app.quit())
  register('notDialogQuit', () => {
    setNotQuitDialog()
    app.quit()
  })
}
