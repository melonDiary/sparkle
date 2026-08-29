import { app } from 'electron'
import { registerIpcHandler } from './ipc-registration'
import { ipcErrorWrapper } from './ipc-error'
import { checkAutoRun, disableAutoRun, enableAutoRun } from '../sys/autoRun'
import { getAppConfig, patchAppConfig, getControledMihomoConfig, patchControledMihomoConfig } from '../config'
import { restartCore, startNetworkDetection, stopCore } from '../core/manager'
import { setNotQuitDialog } from '..'
import { stopNetworkDetection } from '../core/network'
import { checkCorePermission, manualGrantCorePermition, revokeCorePermission } from '../core/permission'
import { triggerSysProxy } from '../sys/sysproxy'
import { checkUpdate, downloadAndInstallUpdate, cancelUpdate } from '../resolve/autoUpdater'
import { checkElevateTask, deleteElevateTask } from '../sys/misc'
import { serviceStatus, installService, uninstallService, startService, stopService, initService, testServiceConnection, restartService } from '../service/manager'
import { patchCoreProfile } from '../service/api'
import { coreLogPath } from './dirs'
import { getRuntimeConfig, getRuntimeConfigStr, getRawProfileStr, getCurrentProfileStr, getOverrideProfileStr } from '../core/factory'
import { appendAppLog } from './log'
import { showNotification } from './notification'
import { startMonitor } from '../resolve/trafficMonitor'
import { clearCachedMihomoLogs, getCachedMihomoLogs } from './log'

async function patchAppConfigWithServiceSync(patch: Partial<AppConfig>): Promise<AppConfig> {
  const nextConfig = await patchAppConfig(await normalizeServiceModePatch(patch))
  if (!('saveLogs' in patch || 'maxLogFileSizeMB' in patch)) return nextConfig
  const { corePermissionMode = 'elevated', saveLogs = true, maxLogFileSizeMB = 20 } = await getAppConfig()
  if (corePermissionMode !== 'service') return nextConfig
  void patchCoreProfile({ log_path: coreLogPath(), save_logs: saveLogs, max_log_file_size_mb: maxLogFileSizeMB }).catch((error) => {
    appendAppLog(`[Service]: sync core log config failed, ${error}\n`).catch(() => {})
  })
  return nextConfig
}

async function normalizeServiceModePatch(patch: Partial<AppConfig>): Promise<Partial<AppConfig>> {
  if (patch.sysProxy?.settingMode !== 'service') return patch
  const status = await serviceStatus().catch(() => 'unknown' as const)
  if (status === 'running') return patch
  void showNotification({ title: '服务不可用，已切换到执行命令模式' })
  return { ...patch, sysProxy: { ...patch.sysProxy, settingMode: 'exec', guard: false, guardNotify: false } }
}

export function registerAppIpc(): void {
  const r = registerIpcHandler
  const w = ipcErrorWrapper
  r('checkAutoRun', w(checkAutoRun))
  r('enableAutoRun', w(enableAutoRun))
  r('disableAutoRun', w(disableAutoRun))
  r('getAppConfig', (_e, force) => w(getAppConfig)(force))
  r('getCachedMihomoLogs', () => getCachedMihomoLogs())
  r('clearCachedMihomoLogs', () => clearCachedMihomoLogs())
  r('patchAppConfig', (_e, config) => w(patchAppConfigWithServiceSync)(config))
  r('getControledMihomoConfig', (_e, force) => w(getControledMihomoConfig)(force))
  r('patchControledMihomoConfig', (_e, config) => w(patchControledMihomoConfig)(config))
  r('restartCore', w(restartCore))
  r('stopCore', w(stopCore))
  r('startMonitor', (_e, detached) => w(startMonitor)(detached))
  r('triggerSysProxy', (_e, enable, onlyActiveDevice, useRegistry) =>
    w(triggerSysProxy)(enable, onlyActiveDevice, useRegistry)
  )
  r('manualGrantCorePermition', (_e, cores?: ('mihomo' | 'mihomo-alpha')[]) =>
    w(manualGrantCorePermition)(cores)
  )
  r('checkCorePermission', () => w(checkCorePermission)())
  r('revokeCorePermission', (_e, cores?: ('mihomo' | 'mihomo-alpha')[]) =>
    w(revokeCorePermission)(cores)
  )
  r('checkElevateTask', () => w(checkElevateTask)())
  r('deleteElevateTask', () => w(deleteElevateTask)())
  r('serviceStatus', () => w(serviceStatus)())
  r('testServiceConnection', () => w(testServiceConnection)())
  r('initService', () => w(initService)())
  r('installService', () => w(installService)())
  r('uninstallService', () => w(uninstallService)())
  r('startService', () => w(startService)())
  r('restartService', () => w(restartService)())
  r('stopService', () => w(stopService)())
  r('startNetworkDetection', w(startNetworkDetection))
  r('stopNetworkDetection', w(stopNetworkDetection))
  r('getRuntimeConfigStr', w(getRuntimeConfigStr))
  r('getRawProfileStr', w(getRawProfileStr))
  r('getCurrentProfileStr', w(getCurrentProfileStr))
  r('getOverrideProfileStr', w(getOverrideProfileStr))
  r('getRuntimeConfig', w(getRuntimeConfig))
  r('downloadAndInstallUpdate', (_e, version, tag) => w(downloadAndInstallUpdate)(version, tag))
  r('checkUpdate', w(checkUpdate))
  r('cancelUpdate', w(cancelUpdate))
  r('getVersion', () => app.getVersion())
  r('platform', () => process.platform)
  r('quitApp', () => app.quit())
  r('notDialogQuit', () => { setNotQuitDialog(); app.quit() })
}
